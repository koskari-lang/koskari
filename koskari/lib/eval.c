/*
  This library is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 3 of the License, or
  (at your option) any later version.

  This library is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this library; if not, see <http://www.gnu.org/licenses/>.
*/


/**
   koskari/lib/eval.c

   Evaluator core implementation: delimited multi-shot continuations,
   dynamic wind/unwind, and flattened async execution.

   This version uses the DynNode chain infrastructure from dynchain.h
   for unified dynamic extent tracking.

   Invariants:

   1. Context ownership: A KosContext is owned by exactly one Python
      wrapper (KosCoroutine or KosPyAsyncGen) during its lifetime.

   2. Chain ownership: The dynamic chain is owned by the context.
      On continuation capture, chains are cloned for multi-shot semantics.

   3. Suspension: When the VM suspends (AWAIT), the context's py_await_iter
      holds the iterator. Resumption injects the result onto the stack.

   4. Multi-shot: Continuations clone all state on invocation, allowing
      multiple independent resumptions.

   :author: Christopher O'Brien <obriencj@preoccupied.net>
   :license: GNU General Public License v3
   :ai-assistant: Claude Opus 4.5 via Cursor
*/


#include "lib.h"
#include "debug.h"


/* Opcode handlers are always inlined so the eval loop stays fast. */
#if defined(__GNUC__) || defined(__clang__)
# define KOS_OP_INLINE static inline __attribute__((always_inline))
# define likely(x)   __builtin_expect(!!(x), 1)
# define unlikely(x) __builtin_expect(!!(x), 0)
#else
# define KOS_OP_INLINE static inline
# define likely(x)   (x)
# define unlikely(x) (x)
#endif

/* Labels-as-values (computed goto) for direct dispatch; omit op_handlers[]
   when enabled so the compiler can inline handler bodies. Build may override
   with -DKOS_USE_COMPUTED_GOTO=0 or =1. */
#ifndef KOS_USE_COMPUTED_GOTO
# if (defined(__GNUC__) || defined(__clang__)) && !defined(__STRICT_ANSI__)
#  define KOS_USE_COMPUTED_GOTO 1
# else
#  define KOS_USE_COMPUTED_GOTO 0
# endif
#endif


#define KOS_OP_HANDLER(name)                                            \
  KOS_OP_INLINE int op_##name(KosContext *ctx, KosEvent *out_ev,        \
                              KosOp *insn)


/**
   Run the current transfer plan to completion.  Returns 0 on success (plan
   cleared or was NULL).  Returns -1 if a step failed (out_ev set to RAISE,
   caller should return 0 from koskari_eval).
*/
static int run_transfer_plan_to_completion(KosContext *ctx, KosEvent *out_ev) {
  while (ctx->plan) {
    SAYF("[eval] transfer step %zu/%zu\n",
         (size_t) ctx->plan->action_index,
         (size_t) ctx->plan->action_count);
    int plan_res = kos_run_transfer_step(ctx);
    if (plan_res < 0) {
      SAY("[eval] transfer step failed -> KOS_EV_RAISE\n");
      out_ev->kind = KOS_EV_RAISE;
      out_ev->payload = NULL;
      PyErr_Fetch(&out_ev->payload, NULL, NULL);
      ctx->flags &= ~KOS_CTX_RUNNING;
      ctx->flags |= KOS_CTX_DEAD;
      return -1;
    }
    if (plan_res == 0)
      continue;
    SAYF("[eval] transfer complete inv_seg_top=%p\n",
         (void *) ctx->dyn_top);
    ctx->invocation_segment_top = ctx->dyn_top;
    kos_free_transfer_plan(ctx->plan);
    ctx->plan = NULL;
    ctx->flags &= ~KOS_CTX_IN_TRANSFER;
    if (ctx->plan_stack)
      kos_resume_transfer_plan(ctx);
  }
  return 0;
}


/**
   Resolve insn->arg as a prompt_id constant from code.  Returns -1 on error,
   0 on success with *out_prompt_id set.
*/
static int get_prompt_id_from_insn(KosCode *code, KosOp *insn,
                                   uint32_t *out_prompt_id) {
  PyObject *cid = KosCode_GetConstant((PyObject *) code, insn->arg);
  if (! cid)
    return -1;
  long pid = PyLong_AsLong(cid);
  if (pid < 0 && PyErr_Occurred())
    return -1;
  *out_prompt_id = (uint32_t) pid;
  return 0;
}


/**
   Print current stack block address/id and length when KOS_ENABLE_DEBUG;
   also print top two slot values as integers when PyLong.
*/
static void trace_stack(const char *prefix, KosStackBlock *stack) {
  SAYF("[eval] %s stack=%p", prefix, (void *) stack);
  if (stack) {
    size_t n = KosStackBlock_LEN(stack);
    SAYF(" len=%zu", n);
    for (size_t i = (n >= 2 ? n - 2 : 0); i < n; i++) {
      PyObject *o = stack->objs[i];
      if (PyLong_Check(o))
        SAYF(" [%zu]=%ld", i, PyLong_AsLong(o));
    }
  }
  SAY("\n");
}


/* === Static string constants === */

static PyObject *str_await = NULL;
static PyObject *str_send = NULL;
static PyObject *str_throw = NULL;
static PyObject *str_close = NULL;


/* === TLS stack for active evaluator tokens === */

#define KOS_TLS_STACK_MAX 32

/* File-scope static stack.  koskari_eval is single-threaded per process
   for the current design; nested eval (e.g. hook calling back) pushes
   tokens onto this stack. */
static KosEvaluatorToken *active_token_stack[KOS_TLS_STACK_MAX];
static size_t active_token_depth = 0;


static void push_active_token(KosEvaluatorToken *token) {
  if (active_token_depth < KOS_TLS_STACK_MAX) {
    active_token_stack[active_token_depth++] = token;
  }
}


static void pop_active_token(KosEvaluatorToken *token) {
  (void) token;
  if (active_token_depth > 0)
    active_token_depth--;
}


/* === KosEvaluatorToken type === */

static uint64_t token_cookie_counter = 0;

static PyObject *token_repr(PyObject *self) {
  KosEvaluatorToken *t = (KosEvaluatorToken *) self;
  return PyUnicode_FromFormat("<KosEvaluatorToken cookie=%llu live_depth=%d>",
                              (unsigned long long) t->cookie,
                              t->live_depth);
}

PyTypeObject KosEvaluatorTokenType = {
  PyVarObject_HEAD_INIT(NULL, 0)
  .tp_name = "koskari.lib.EvaluatorToken",
  .tp_basicsize = sizeof(KosEvaluatorToken),
  .tp_itemsize = 0,
  .tp_repr = token_repr,
  .tp_flags = Py_TPFLAGS_DEFAULT,
};


KosEvaluatorToken *KosEvaluatorToken_New(void) {
  KosEvaluatorToken *token = PyObject_New(KosEvaluatorToken,
                                          &KosEvaluatorTokenType);
  if (! token)
    return NULL;
  token->cookie = (uint64_t) __sync_add_and_fetch(&token_cookie_counter, 1);
  token->live_depth = 0;
  return token;
}


void KosEvaluatorToken_IncrementLiveDepth(KosEvaluatorToken *token) {
  if (token)
    token->live_depth++;
}


void KosEvaluatorToken_DecrementLiveDepth(KosEvaluatorToken *token) {
  if (token && token->live_depth > 0)
    token->live_depth--;
}


/* === Exception types === */

PyObject *KoskariContinuationDead = NULL;
PyObject *KoskariContinuationApply = NULL;
PyObject *KoskariContinuationError = NULL;


/* === KosFrame implementation (wrapper around KosDynFrame) === */


KosFrame *kos_frame_new(
    KosCode *code,
    PyObject *ns,
    KosStackBlock *stack,
    KosDynNode *parent) {

  return kos_dynframe_new(parent, code, ns, stack);
}


KosFrame *kos_frame_clone(KosFrame *frame) {
  return kos_dynframe_clone(frame);
}


void kos_frame_free(KosFrame *frame) {
  kos_dynframe_free(frame);
}


int kos_frame_traverse(
    KosFrame *frame,
    visitproc visit,
    void *arg) {

  if (! frame)
    return 0;

  Py_VISIT(frame->ns);

  if (frame->stack) {
    KosStackBlock *b = frame->stack;
    while (b) {
      for (size_t i = b->len; i--; ) {
        Py_VISIT(b->objs[i]);
      }
      b = KosStackBlock_PARENT(b);
    }
  }

  return 0;
}


/* === KosContext implementation === */


KosContext *kos_context_new(KosDynFrame *top) {

  KosContext *ctx = PyMem_Malloc(sizeof(KosContext));
  if (! ctx) {
    PyErr_NoMemory();
    return NULL;
  }

  ctx->token = KosEvaluatorToken_New();
  if (! ctx->token) {
    PyMem_Free(ctx);
    return NULL;
  }

  ctx->dyn_top = (KosDynNode *) top;
  ctx->frame_top = top;
  ctx->globals_frame = top;
  ctx->globals_cache_version = 0;

  /* Initialize hook runner */
  ctx->hook_runner.code = NULL;
  ctx->hook_runner.pc = 0;
  ctx->hook_runner.scratch = NULL;
  ctx->hook_runner.active = 0;

  /* Initialize transfer plan state */
  ctx->plan = NULL;
  ctx->plan_stack = NULL;
  ctx->chain_version = 0;
  ctx->invocation_segment_top = NULL;

  /* Initialize Python await bridging */
  ctx->py_await_iter = NULL;
  ctx->py_last_yielded = NULL;
  ctx->py_await_inject = NULL;

  /* Initialize exception injection */
  ctx->pending_throw = 0;
  ctx->pending_exc = NULL;

  /* Initialize finally-handler stash */
  ctx->handling_raise = 0;
  ctx->handling_result = NULL;

  ctx->flags = 0;

  return ctx;
}


void kos_context_free(KosContext *ctx) {

  if (! ctx)
    return;

  Py_XDECREF(ctx->token);
  ctx->token = NULL;

  /* Free the dynamic chain */
  kos_dynchain_free(ctx->dyn_top, NULL);

  /* Free any active transfer plan */
  kos_free_transfer_plan(ctx->plan);
  while (ctx->plan_stack) {
    KosTransferPlan *p = ctx->plan_stack;
    ctx->plan_stack = p->suspended;
    kos_free_transfer_plan(p);
  }

  /* Free hook runner scratch stack */
  if (ctx->hook_runner.scratch) {
    KosStackBlock *b = ctx->hook_runner.scratch;
    while (b) {
      KosStackBlock *p = KosStackBlock_PARENT(b);
      KosStackBlock_Release(b);
      b = p;
    }
  }

  Py_XDECREF(ctx->py_await_iter);
  Py_XDECREF(ctx->py_last_yielded);
  Py_XDECREF(ctx->py_await_inject);
  Py_XDECREF(ctx->pending_exc);
  Py_XDECREF(ctx->handling_result);

  PyMem_Free(ctx);
}


int kos_context_traverse(
    KosContext *ctx,
    visitproc visit,
    void *arg) {

  if (! ctx)
    return 0;

  Py_VISIT(ctx->token);
  Py_VISIT(ctx->py_await_iter);
  Py_VISIT(ctx->py_last_yielded);
  Py_VISIT(ctx->py_await_inject);
  Py_VISIT(ctx->pending_exc);
  Py_VISIT(ctx->handling_result);

  /* Traverse the dynamic chain */
  int res = kos_dynchain_traverse(ctx->dyn_top, NULL, visit, arg);
  if (res)
    return res;

  /* Traverse transfer plan */
  res = kos_transferplan_traverse(ctx->plan, visit, arg);
  if (res)
    return res;

  /* Traverse hook runner */
  res = kos_hookrunner_traverse(&ctx->hook_runner, visit, arg);
  if (res)
    return res;

  return 0;
}


/* === GC helper for stack blocks === */


int kos_stackblock_traverse(
    KosStackBlock *b,
    visitproc visit,
    void *arg) {

  size_t i;
  while (b) {
    for (i = b->len; i--; ) {
      Py_VISIT(b->objs[i]);
    }
    b = b->back;
  }

  return 0;
}


/* === Koskari call operations === */


int kos_koscall(
    KosContext *ctx,
    KosCode *code,
    PyObject *ns) {

  if (! ctx) {
    PyErr_SetString(PyExc_RuntimeError, "NULL context for koscall");
    return -1;
  }

  KosDynFrame *frame = kos_dyn_push_frame(ctx, code, ns);
  if (! frame)
    return -1;

  return 0;
}


int kos_tail_koscall(
    KosContext *ctx,
    KosCode *code,
    PyObject *ns) {

  if (! ctx || ! ctx->frame_top) {
    PyErr_SetString(PyExc_RuntimeError, "no frame for tail_koscall");
    return -1;
  }

  KosDynFrame *old = ctx->frame_top;
  KosDynNode *parent = old->base.parent;

  /* Pop the old frame */
  ctx->dyn_top = parent;

  /* Create new frame with same parent */
  KosDynFrame *frame = kos_dynframe_new(parent, code, ns, NULL);
  if (! frame) {
    /* Restore old frame on failure */
    ctx->dyn_top = (KosDynNode *) old;
    return -1;
  }

  /* Free old frame */
  kos_dynframe_free(old);

  /* Install new frame */
  ctx->dyn_top = (KosDynNode *) frame;
  ctx->frame_top = frame;
  ctx->chain_version++;

  return 0;
}


/* === Drive loop === */


/**
   Attempt eager-step of a Python awaitable.

   This tries to get __await__() and call send(None) once.
   If the awaitable completes immediately (StopIteration), the value
   is returned. If it yields, we store the iterator and return the
   yielded value with *suspended = 1.

   :param awaitable: the Python awaitable object
   :param ctx: context to store await state if suspended
   :param suspended: output flag, set to 1 if we need to suspend
   :param yielded: output, the yielded value if suspended

   :return: completed value on immediate completion, NULL on suspend or error
*/
static PyObject *eager_step_awaitable(
    PyObject *awaitable,
    KosContext *ctx,
    int *suspended,
    PyObject **yielded) {

  *suspended = 0;
  *yielded = NULL;

  if (! awaitable) {
    PyErr_SetString(PyExc_TypeError, "eager_step_awaitable: awaitable is NULL");
    return NULL;
  }

  /* Get __await__ */
  PyObject *await_iter = NULL;

  PyObject *await_method = PyObject_GetAttr(awaitable, str_await);
  if (! await_method) {
    /* Maybe it's already an iterator? */
    PyErr_Clear();
    await_iter = awaitable;
    Py_INCREF(await_iter);
  } else {
    await_iter = PyObject_CallNoArgs(await_method);
    Py_DECREF(await_method);
    if (! await_iter)
      return NULL;
  }

  if (! await_iter) {
    PyErr_SetString(PyExc_TypeError,
                    "eager_step_awaitable: __await__() returned NULL");
    return NULL;
  }

  SAYF("[eval] eager_step_awaitable before send(None) (await_iter=%d)\n",
       await_iter ? 1 : 0);

  /* Try first send(None) */
  PyObject *result = PyObject_CallMethodObjArgs(await_iter, str_send,
                                                Py_None, NULL);

  if (result) {
    /* Yielded a value - need to suspend */
    *suspended = 1;
    *yielded = result;

    /* Store iterator for later resumption */
    Py_XDECREF(ctx->py_await_iter);
    ctx->py_await_iter = await_iter;

    return NULL;
  }

  /* Check for StopIteration (immediate completion) */
  if (PyErr_ExceptionMatches(PyExc_StopIteration)) {
    PyObject *value = NULL;
    PyGen_FetchStopIterationValue(&value);
    Py_DECREF(await_iter);

    if (! value)
      value = Py_None;

    Py_INCREF(value);
    return value;
  }

  /* Other exception */
  Py_DECREF(await_iter);
  return NULL;
}


/**
   Push val onto the frame stack, or if val is awaitable run eager_step and
   either push the result or suspend (set out_ev to KOS_EV_AWAIT and return 1).
   val is consumed (new reference).

   :return: 0 continue, 1 stop (AWAIT), -1 error
*/
static int push_or_await(KosContext *ctx, KosEvent *out_ev, PyObject *val) {

  PyTypeObject *type = Py_TYPE(val);
  SAYF("[eval] push_or_await entry is_awaitable=%d\n",
       (type->tp_as_async && type->tp_as_async->am_await) ? 1 : 0);
  if (! type->tp_as_async || ! type->tp_as_async->am_await) {
    if (KosStackBlock_PushRef(&ctx->frame_top->stack, val) < 0) {
      Py_DECREF(val);
      return -1;
    }
    Py_DECREF(val);
    return 0;
  }

  int suspended = 0;
  PyObject *yielded = NULL;
  PyObject *result = eager_step_awaitable(val, ctx, &suspended, &yielded);
  Py_DECREF(val);

  if (result) {
    if (KosStackBlock_PushRef(&ctx->frame_top->stack, result) < 0) {
      Py_DECREF(result);
      return -1;
    }
    Py_DECREF(result);
    return 0;
  }

  if (suspended && yielded) {
    ctx->flags &= ~KOS_CTX_RUNNING;
    out_ev->kind = KOS_EV_AWAIT;
    out_ev->payload = yielded;
    SAYF("[eval] push_or_await set KOS_EV_AWAIT (suspended=%d yielded=%d)\n",
         suspended, yielded ? 1 : 0);
    return 1;
  }

  return -1;
}


/**
   Push a single result onto the frame stack and maybe await it if
   awaitable. Caller passes a new reference in val; it is consumed.
   Returns same as push_or_await: 0 continue, 1 stop (AWAIT), -1 error.
*/
static int push_result_or_await(KosContext *ctx, KosEvent *out_ev, PyObject *val) {
  if (! val)
    return -1;
  return push_or_await(ctx, out_ev, val);
}


/* === Frame namespace helpers (for VAR_* opcodes) === */


/**
   Get the name constant for a VAR_* opcode. Borrowed reference.
*/
static PyObject *get_name_from_insn(KosCode *code, KosOp *insn) {
  return KosCode_GetConstant((PyObject *) code, insn->arg);
}


/**
   Deref name in the current frame namespace. New reference or NULL.
*/
static PyObject *frame_ns_deref(KosContext *ctx, PyObject *name) {
  PyObject *ns = ctx->frame_top->ns;
  if (KosNamespace_Check(ns))
    return KosNamespace_Deref(ns, name);
  if (PyDict_Check(ns)) {
    PyObject *val = PyDict_GetItemWithError(ns, name);
    if (val) {
      Py_INCREF(val);
      return val;
    }
    if (! PyErr_Occurred())
      PyErr_Format(PyExc_NameError, "name %R is not defined", name);
    return NULL;
  }
  PyErr_Format(PyExc_TypeError, "frame namespace must be Namespace or dict,"
               " got %R", ns);
  return NULL;
}


/**
   Store value into name in the current frame namespace. Caller retains
   ownership of val.
*/
static int frame_ns_store(KosContext *ctx, PyObject *name, PyObject *val) {
  PyObject *ns = ctx->frame_top->ns;
  if (KosNamespace_Check(ns))
    return KosNamespace_Store(ns, name, val);
  if (PyDict_Check(ns))
    return PyDict_SetItem(ns, name, val);
  PyErr_Format(PyExc_TypeError,
               "frame namespace must be Namespace or dict, got %R", ns);
  return -1;
}


/**
   Bind name to value in the current frame namespace. Consumes val.
*/
static int frame_ns_bind(KosContext *ctx, PyObject *name, PyObject *val) {
  PyObject *ns = ctx->frame_top->ns;
  if (KosNamespace_Check(ns)) {
    PyObject *ref = KosSymbolicRef_New(name, val);
    Py_DECREF(val);
    if (! ref)
      return -1;
    if (KosNamespace_Bind(ns, name, ref) < 0) {
      Py_DECREF(ref);
      return -1;
    }
    Py_DECREF(ref);
    return 0;
  }
  if (PyDict_Check(ns)) {
    if (PyDict_SetItem(ns, name, val) < 0) {
      Py_DECREF(val);
      return -1;
    }
    Py_DECREF(val);
    return 0;
  }
  Py_DECREF(val);
  PyErr_SetString(PyExc_TypeError,
                  "frame namespace must be Namespace or dict");
  return -1;
}


/**
   Clear name from the current frame namespace.
*/
static int frame_ns_clear(KosContext *ctx, PyObject *name) {
  PyObject *ns = ctx->frame_top->ns;
  if (KosNamespace_Check(ns))
    return KosNamespace_Clear(ns, name);
  if (PyDict_Check(ns))
    return PyDict_DelItem(ns, name);
  PyErr_Format(PyExc_TypeError,
               "frame namespace must be Namespace or dict, got %R", ns);
  return -1;
}


/**
   Get reference for name in the current frame namespace. Namespace only.
   Borrowed reference; caller must INCREF if keeping.
*/
static PyObject *frame_ns_ref(KosContext *ctx, PyObject *name) {
  PyObject *ns = ctx->frame_top->ns;
  if (! KosNamespace_Check(ns)) {
    PyErr_Format(PyExc_TypeError,
                 "frame namespace must be Namespace for VAR_REF, got %R", ns);
    return NULL;
  }
  return KosNamespace_Ref(ns, name);
}


/* === Opcode handlers (always inlined; return 0=continue 1=stop -1=error) === */


static int op_unimplemented_stub(KosContext *ctx, KosEvent *out_ev, KosOp *insn) {
  (void) ctx;
  (void) out_ev;
  PyErr_Format(PyExc_RuntimeError, "unimplemented opcode %u",
               (unsigned) insn->op);
  return -1;
}


/*
   Opcode families (for quick navigation):
   NOOP, LABEL, POSITION - no-op / debug
   CONST, DUP, POP - stack
   RETURN, AWAIT - control
   CALL, CALL_*, RECUR, TAIL_* - calls
   GLOBALS, LOCALS - namespaces
   VAR_*, FAST_* - variable access
   EXCEPTION_* - exceptions
   CHECK_* - comparison / jump
   OBJECT_* - object protocol
   PUSH_*, POP_* - blocks, regions, NS
   SHIFT, INVOKE_K, prompt - continuations
*/


/**
   NOOP: arg unused. No operation; advance to next instruction.
*/
KOS_OP_HANDLER(NOOP) {
  (void) ctx;
  (void) out_ev;
  (void) insn;
  return 0;  /* continue */
}


/**
   LABEL: arg unused. Marks a branch target; no runtime effect.
*/
KOS_OP_HANDLER(LABEL) {
  (void) ctx;
  (void) out_ev;
  (void) insn;
  return 0;
}


/**
   POSITION: arg unused. Debug/source position marker; no runtime effect.
*/
KOS_OP_HANDLER(POSITION) {
  (void) ctx;
  (void) out_ev;
  (void) insn;
  return 0;
}


/**
   CONST: arg = constant index. Push the code object constant at that index
   onto the stack.
*/
KOS_OP_HANDLER(CONST) {
  PyObject *val = KosCode_GetConstant((PyObject *) ctx->frame_top->code,
                                      insn->arg);
  if (! val) {
    SAYF("[eval] CONST GetConstant failed arg=%u\n",
         (unsigned) insn->arg);
    return -1;
  }
  if (PyLong_Check(val))
    SAYF("[eval] CONST pushed %ld\n", PyLong_AsLong(val));
  Py_INCREF(val);
  return push_result_or_await(ctx, out_ev, val);
}


/**
   DUP: arg = number of top stack slots to duplicate. Duplicate that many
   top-of-stack values (pushed again in order).
*/
KOS_OP_HANDLER(DUP) {
  int opa = (int) insn->arg;
  if (opa < 0) {
    PyErr_Format(PyExc_RuntimeError, "DUP invoked with negative"
                 " argument count: %i", opa);
    return -1;
  }

  if (KosStackBlock_Duplicate(&ctx->frame_top->stack, (size_t) opa) < 0)
    return -1;
  (void) out_ev;
  return 0;
}


/**
   POP: arg = number of stack slots to drop. Remove that many values from
   the top of the stack.
*/
KOS_OP_HANDLER(POP) {
  unsigned n = insn->arg;
  if (KosStackBlock_Drop(&ctx->frame_top->stack, (size_t) n) < 0)
    return -1;
  (void) out_ev;
  return 0;
}


/**
   RETURN: arg = 0 (None), 1 (single value), or N (tuple of N values).
   Pop the return value(s) from the stack and stop the context with
   KOS_EV_RETURN.
*/
KOS_OP_HANDLER(RETURN) {
  unsigned opa = insn->arg;
  PyObject *val = NULL;

  if (opa == 0) {
    val = Py_None;
    Py_INCREF(val);

  } else if (opa == 1) {
    val = KosStackBlock_PopRef(&ctx->frame_top->stack);
    if (! val) {
      SAY("[eval] RETURN pop underrun\n");
      return -1;
    }

  } else {
    val = KosStackBlock_PopTail(&ctx->frame_top->stack, (size_t) opa);
    if (! val) {
      SAY("[eval] RETURN PopTail underrun\n");
      return -1;
    }
  }

  SAYF("[eval] RETURN count=%u -> KOS_EV_RETURN\n", opa);
  SAYF("[eval] RETURN payload: %R\n", val);
  out_ev->kind = KOS_EV_RETURN;
  out_ev->payload = val;
  ctx->flags &= ~KOS_CTX_RUNNING;
  ctx->flags |= KOS_CTX_DEAD;
  return 1;
}


/**
   AWAIT: arg unused. Pop TOS (awaitable), start/resume awaiting it; may
   suspend with KOS_EV_AWAIT or push the result.
*/
KOS_OP_HANDLER(AWAIT) {
  (void) insn;
  PyObject *val = KosStackBlock_PopRef(&ctx->frame_top->stack);
  if (! val)
    return -1;
  return push_or_await(ctx, out_ev, val);
}


/**
   CALL: arg = number of positional arguments N. Stack has [callable, arg_0,
   ..., arg_{N-1}] (callable oldest). Call callable with those args, drop
   them, then push result or await it.
*/
KOS_OP_HANDLER(CALL) {
  unsigned n = insn->arg;
  /* Top (n+1) elements are [callable, arg_0, ..., arg_{n-1}]; callable is oldest
     of that segment (same layout as sibilant-ceval). */
  PyObject *const *tail = KosStackBlock_TailPtr(ctx->frame_top->stack, n + 1);
  if (! tail)
    return -1;
  PyObject *callable = tail[0];
  PyObject *result = NULL;

  /* Special-case DelimitedContinuation and Continuation (undelimited) */
  if (KosPyDelimitedContinuation_Check(callable)
      || KosPyContinuation_Check(callable)) {
    KosContinuation *k = KosPyDelimitedContinuation_Check(callable)
        ? ((KosPyDelimitedContinuation *) callable)->captured
        : ((KosPyContinuation *) callable)->captured;
    PyObject *resume_val = (n > 0 && tail[1]) ? tail[1] : Py_None;
    Py_INCREF(resume_val);

    if (KosStackBlock_Drop(&ctx->frame_top->stack, n + 1) < 0) {
      Py_DECREF(resume_val);
      return -1;
    }

    if (! k) {
      PyErr_SetString(PyExc_RuntimeError,
                      "continuation has no captured state");
      Py_DECREF(resume_val);
      return -1;
    }

    if (k->token_ref == (PyObject *) ctx->token) {
      /* Same evaluator: apply directly */
      if (kos_invoke_continuation(ctx, k, resume_val, 0, callable) < 0) {
        Py_DECREF(resume_val);
        return -1;
      }
      Py_DECREF(resume_val);
      (void) out_ev;
      (void) insn;
      return 0;
    }

    /* Token mismatch: raise KoskariContinuationApply for upwards propagation */
    {
      PyObject *exc = PyObject_CallNoArgs(KoskariContinuationApply);
      if (exc) {
        PyObject_SetAttrString(exc, "target", k->token_ref);
        PyObject_SetAttrString(exc, "continuation", callable);
        PyObject *args_tup = PyTuple_Pack(1, resume_val);
        if (args_tup) {
          PyObject_SetAttrString(exc, "args", args_tup);
          Py_DECREF(args_tup);
        }
        PyErr_SetObject(KoskariContinuationApply, exc);
        Py_DECREF(exc);
      }
    }
    Py_DECREF(resume_val);
    return -1;
  }

  SAYF("[CALL] callable: %R\n", callable);
  if (n == 0) {
    result = PyObject_CallNoArgs(callable);
  } else {
    result = PyObject_Vectorcall(callable, &tail[1], n, NULL);
  }

  if (KosStackBlock_Drop(&ctx->frame_top->stack, n + 1) < 0) {
    Py_XDECREF(result);
    return -1;
  }

  SAYF("[CALL] result: %R\n", result);
  if (result && Py_TYPE(result)->tp_as_async)
  SAY("[CALL] result is awaitable, passing to push_or_await\n");

  SAYF("[eval] op_CALL after PyObject_Call (result=%d awaitable=%d)\n",
       result ? 1 : 0, result && Py_TYPE(result)->tp_as_async ? 1 : 0);

  if (! result)
    return -1;
  return push_or_await(ctx, out_ev, result);
}


/**
   GLOBALS: arg unused. Push the namespace of the outermost frame in the
   dynamic chain (globals) onto the stack.
*/
KOS_OP_HANDLER(GLOBALS) {
  if (ctx->chain_version != ctx->globals_cache_version) {
    KosDynNode *n = (KosDynNode *) ctx->frame_top;
    KosDynFrame *outer = ctx->frame_top;
    while (n && n->parent) {
      n = n->parent;
      if (KosDynNode_IS_FRAME(n))
        outer = (KosDynFrame *) n;
    }
    ctx->globals_frame = outer;
    ctx->globals_cache_version = ctx->chain_version;
  }

  PyObject *ns = ctx->globals_frame->ns;
  Py_INCREF(ns);
  return push_result_or_await(ctx, out_ev, ns);
}


/**
   LOCALS: arg unused. Push the current frame's namespace (locals) onto
   the stack.
*/
KOS_OP_HANDLER(LOCALS) {
  PyObject *ns = ctx->frame_top->ns;
  Py_INCREF(ns);
  return push_result_or_await(ctx, out_ev, ns);
}


/**
   VAR: arg = constant index of a name. Look up that name in the current
   frame's namespace and push the value (or await it if async).
*/
KOS_OP_HANDLER(VAR) {
  PyObject *name = get_name_from_insn(ctx->frame_top->code, insn);
  if (! name)
    return -1;
  PyObject *val = frame_ns_deref(ctx, name);
  if (! val)
    return -1;
  return push_or_await(ctx, out_ev, val);
}


/**
   EXCEPTION: arg unused. Push the current exception instance (from
   PyErr_GetExcInfo) onto the stack, or Py_None if no exception is set.
*/
KOS_OP_HANDLER(EXCEPTION) {
  PyObject *typ = NULL, *val = NULL, *tb = NULL;
  PyErr_GetExcInfo(&typ, &val, &tb);

  if (! typ) {
    Py_INCREF(Py_None);
    return push_result_or_await(ctx, out_ev, Py_None);
  }

  if (! val)
    PyErr_NormalizeException(&typ, &val, &tb);

  Py_DECREF(typ);
  Py_XDECREF(tb);

  return push_result_or_await(ctx, out_ev, val);
}


/**
   EXCEPTION_CLEAR: arg unused. Clear the current exception and exc info.
*/
KOS_OP_HANDLER(EXCEPTION_CLEAR) {
  PyErr_Clear();
  PyErr_SetExcInfo(NULL, NULL, NULL);
  (void) ctx;
  (void) out_ev;
  (void) insn;
  return 0;
}


/**
   EXCEPTION_MATCH: arg = number of exception types on stack. Pop that many
   types; if the current exception matches any of them, push True, else False.
*/
KOS_OP_HANDLER(EXCEPTION_MATCH) {
  int opa = (int) insn->arg;
  if (opa < 1) {
    PyErr_Format(PyExc_RuntimeError, "EXCEPTION_MATCH with non-positive"
                 " count, %i", opa);
    return -1;
  }

  PyObject *ret = Py_False;
  PyObject *typ = NULL, *val = NULL, *tb = NULL;
  PyErr_GetExcInfo(&typ, &val, &tb);

  if (! typ) {
    if (KosStackBlock_Drop(&ctx->frame_top->stack, (size_t) opa) < 0)
      return -1;
    Py_INCREF(ret);
    return push_result_or_await(ctx, out_ev, ret);
  }

  PyObject *const *checks = KosStackBlock_TailPtr(ctx->frame_top->stack,
                                                  (size_t) opa);
  if (! checks) {
    Py_DECREF(typ);
    Py_XDECREF(val);
    Py_XDECREF(tb);
    return -1;
  }

  for (int i = 0; i < opa; i++) {
    if (PyErr_GivenExceptionMatches(typ, checks[i])) {
      ret = Py_True;
      break;
    }
  }

  KosStackBlock_DropNoErr(&ctx->frame_top->stack, (size_t) opa);

  Py_DECREF(typ);
  Py_XDECREF(val);
  Py_XDECREF(tb);

  Py_INCREF(ret);
  return push_result_or_await(ctx, out_ev, ret);
}


/**
   RAISE: arg = 0 (reraise current exception), 1 (raise TOS: instance or class).
   Set the current exception; control will transfer to a handler by the eval loop.
*/
KOS_OP_HANDLER(RAISE) {
  unsigned n = insn->arg;
  if (n == 0) {
    PyObject *typ = NULL, *val = NULL, *tb = NULL;
    PyErr_GetExcInfo(&typ, &val, &tb);
    if (! typ || typ == Py_None) {
      PyErr_SetString(PyExc_RuntimeError, "No active exception to reraise");
      Py_XDECREF(typ);
      Py_XDECREF(val);
      Py_XDECREF(tb);
      return -1;
    }
    PyErr_Restore(typ, val, tb);
    PyErr_SetExcInfo(NULL, NULL, NULL);

  } else if (n == 1) {
    PyObject *exc = KosStackBlock_PopRef(&ctx->frame_top->stack);
    if (! exc) {
      return -1;
    }
    if (PyExceptionInstance_Check(exc)) {
      PyObject *typ = (PyObject *) Py_TYPE(exc);
      PyErr_SetObject(typ, exc);
    } else if (PyExceptionClass_Check(exc)) {
      PyObject *inst = PyObject_CallNoArgs(exc);
      if (inst) {
        PyErr_SetObject(exc, inst);
        Py_DECREF(inst);
      }
      Py_DECREF(exc);
      if (PyErr_Occurred())
        return -1;
    } else {
      Py_DECREF(exc);
      PyErr_SetString(PyExc_TypeError, "exceptions must derive from BaseException");
      return -1;
    }
    Py_DECREF(exc);

  } else {
    PyErr_Format(PyExc_RuntimeError, "RAISE with unsupported arg count %u", n);
    return -1;
  }

  (void) out_ev;
  return 0;
}


/**
   CHECK_EXCEPTION: arg = target PC (label index). Pop TOS (exception type).
   If current exception matches that type, leave PC unchanged; else set PC to
   arg and continue (branch to handler).
*/
KOS_OP_HANDLER(CHECK_EXCEPTION) {
  PyObject *match = KosStackBlock_PopRef(&ctx->frame_top->stack);
  if (! match)
    return -1;

  int ret = 0;
  PyObject *typ = NULL, *val = NULL, *tb = NULL;
  PyErr_GetExcInfo(&typ, &val, &tb);

  if (typ)
    ret = PyErr_GivenExceptionMatches(typ, match);

  Py_XDECREF(typ);
  Py_XDECREF(val);
  Py_XDECREF(tb);
  Py_DECREF(match);

  if (ret) {
    (void) out_ev;
    return 0;
  }
  ctx->frame_top->pc = insn->arg;
  (void) out_ev;
  return 0;
}


/**
   PUSH_PROMPT: arg = constant index of prompt_id. Push a delimited prompt
   onto the dynamic chain for shift/reset.
*/
KOS_OP_HANDLER(PUSH_PROMPT) {
  uint32_t prompt_id;
  if (get_prompt_id_from_insn(ctx->frame_top->code, insn, &prompt_id) < 0)
    return -1;
  if (! kos_dyn_push_prompt(ctx, prompt_id))
    return -1;
  (void) out_ev;
  return 0;
}


/**
   SET_PROMPT_HANDLER: arg = PC (label) of handler. If dyn_top is a prompt,
   set its handler code and PC so SHIFT will jump there with the continuation.
*/
KOS_OP_HANDLER(SET_PROMPT_HANDLER) {
  if (ctx->dyn_top && KosDynNode_IS_PROMPT(ctx->dyn_top)) {
    KosDynPrompt *p = (KosDynPrompt *) ctx->dyn_top;
    p->handler_code = ctx->frame_top->code;
    p->handler_pc = insn->arg;
  }
  (void) out_ev;
  return 0;
}


/**
   POP_PROMPT: arg unused. If returning to an invoker, pop value and emit
   KOS_EV_RETURN; else pop the top prompt from the dynamic chain.
*/
KOS_OP_HANDLER(POP_PROMPT) {
  SAYF("[eval] POP_PROMPT inv_seg=%p dyn_top=%p IS_PROMPT=%d\n",
       (void *) ctx->invocation_segment_top, (void *) ctx->dyn_top,
       (ctx->dyn_top && KosDynNode_IS_PROMPT(ctx->dyn_top)) ? 1 : 0);

  if (ctx->invocation_segment_top != NULL) {
    PyObject *val = KosStackBlock_PopRef(&ctx->frame_top->stack);
    if (! val) {
      SAY("[eval] POP_PROMPT (invoke) pop underrun\n");
      return -1;
    }
    if (PyLong_Check(val))
      SAYF("[eval] POP_PROMPT return to invoker value=%ld\n",
           PyLong_AsLong(val));
    out_ev->kind = KOS_EV_RETURN;
    out_ev->payload = val;
    ctx->invocation_segment_top = NULL;
    ctx->flags &= ~KOS_CTX_RUNNING;
    (void) insn;
    return 1;
  }

  if (! ctx->dyn_top || ! KosDynNode_IS_PROMPT(ctx->dyn_top)) {
    PyErr_SetString(PyExc_RuntimeError, "POP_PROMPT: top is not a prompt");
    return -1;
  }
  kos_dyn_pop_prompt(ctx, (KosDynPrompt *) ctx->dyn_top);
  (void) insn;
  return 0;
}


/**
   PUSH_TRY: arg = target label for exception handler. Push a TRY region
   onto the dynamic chain.
*/
KOS_OP_HANDLER(PUSH_TRY) {
  if (! kos_dyn_push_region(ctx, KOS_REGION_TRY, insn->arg, 0)) {
    PyErr_NoMemory();
    return -1;
  }
  (void) out_ev;
  return 0;
}


/**
   PUSH_FINALLY: arg = target label for finally block. Push a FINALLY region
   onto the dynamic chain.
*/
KOS_OP_HANDLER(PUSH_FINALLY) {
  if (! kos_dyn_push_region(ctx, KOS_REGION_FINALLY, insn->arg, 0)) {
    PyErr_NoMemory();
    return -1;
  }
  (void) out_ev;
  return 0;
}


/**
   POP_TRY: arg unused. Verify top of dynamic chain is a TRY region, clear
   exception state, then pop the region.
*/
KOS_OP_HANDLER(POP_TRY) {
  if (! ctx->dyn_top || ! KosDynNode_IS_REGION(ctx->dyn_top)) {
    PyErr_SetString(PyExc_RuntimeError, "POP_TRY: top is not a region");
    return -1;
  }
  KosDynRegion *region = (KosDynRegion *) ctx->dyn_top;
  if (region->style != KOS_REGION_TRY) {
    PyErr_SetString(PyExc_RuntimeError, "POP_TRY: top is not a TRY region");
    return -1;
  }

  PyErr_Clear();
  PyErr_SetExcInfo(NULL, NULL, NULL);
  kos_dyn_pop_region(ctx, region);
  (void) out_ev;
  (void) insn;
  return 0;
}


/**
   POP_FINALLY: arg unused. Verify top is a FINALLY region; if we were
   handling a raise, restore the exception. Then pop the region.
*/
KOS_OP_HANDLER(POP_FINALLY) {
  if (! ctx->dyn_top || ! KosDynNode_IS_REGION(ctx->dyn_top)) {
    PyErr_SetString(PyExc_RuntimeError, "POP_FINALLY: top is not a region");
    return -1;
  }
  KosDynRegion *region = (KosDynRegion *) ctx->dyn_top;
  if (region->style != KOS_REGION_FINALLY) {
    PyErr_SetString(PyExc_RuntimeError,
                    "POP_FINALLY: top is not a FINALLY region");
    return -1;
  }

  if (ctx->handling_raise) {
    PyObject *typ = NULL, *val = NULL, *tb = NULL;
    PyErr_GetExcInfo(&typ, &val, &tb);
    PyErr_Restore(typ, val, tb);
    PyErr_SetExcInfo(NULL, NULL, NULL);
    ctx->handling_raise = 0;
  }
  kos_dyn_pop_region(ctx, region);
  (void) out_ev;
  (void) insn;
  return 0;
}


/**
   CLEAR_TRY: arg unused. Disable the top TRY region (so it will not catch).
*/
KOS_OP_HANDLER(CLEAR_TRY) {
  if (! ctx->dyn_top || ! KosDynNode_IS_REGION(ctx->dyn_top)) {
    PyErr_SetString(PyExc_RuntimeError, "CLEAR_TRY: top is not a region");
    return -1;
  }
  KosDynRegion *region = (KosDynRegion *) ctx->dyn_top;
  if (region->style != KOS_REGION_TRY) {
    PyErr_SetString(PyExc_RuntimeError, "CLEAR_TRY: top is not a TRY region");
    return -1;
  }
  region->enabled = 0;
  (void) out_ev;
  (void) insn;
  return 0;
}


/**
   CLEAR_FINALLY: arg unused. Disable the top FINALLY region.
*/
KOS_OP_HANDLER(CLEAR_FINALLY) {
  if (! ctx->dyn_top || ! KosDynNode_IS_REGION(ctx->dyn_top)) {
    PyErr_SetString(PyExc_RuntimeError, "CLEAR_FINALLY: top is not a region");
    return -1;
  }
  KosDynRegion *region = (KosDynRegion *) ctx->dyn_top;
  if (region->style != KOS_REGION_FINALLY) {
    PyErr_SetString(PyExc_RuntimeError,
                    "CLEAR_FINALLY: top is not a FINALLY region");
    return -1;
  }
  region->enabled = 0;
  (void) out_ev;
  (void) insn;
  return 0;
}


/**
   SHIFT: arg = constant index of prompt_id. Capture the continuation up to
   that prompt, unwind the dynamic chain to the prompt, push a new frame for
   the prompt's handler, and push the continuation (as a Python object) on
   the stack; handler runs with continuation at TOS.
*/
KOS_OP_HANDLER(SHIFT) {
  uint32_t prompt_id;
  KosContinuation *k = NULL;
  if (get_prompt_id_from_insn(ctx->frame_top->code, insn, &prompt_id) < 0)
    return -1;

  SAYF("[eval] SHIFT prompt_id=%u\n", (unsigned) prompt_id);
  if (kos_shift_capture(ctx, prompt_id, &k) < 0) {
    SAY("[eval] SHIFT capture failed\n");
    return -1;
  }

  KosDynPrompt *prompt = kos_find_prompt(ctx, prompt_id);
  if (! prompt) {
    kos_continuation_free(k);
    PyErr_SetString(PyExc_RuntimeError, "shift: prompt not found");
    return -1;
  }

  SAYF("[eval] SHIFT unwinding to prompt, then handler_pc=%u\n",
       (unsigned) prompt->handler_pc);
  while (ctx->dyn_top != (KosDynNode *) prompt) {
    if (kos_dyn_pop_top(ctx) != 0) {
      kos_continuation_free(k);
      return -1;
    }
  }

  PyObject *wrapper = KosPyDelimitedContinuation_New(k);
  if (! wrapper) {
    kos_continuation_free(k);
    return -1;
  }
  if (! prompt->handler_code) {
    Py_DECREF(wrapper);
    PyErr_SetString(PyExc_RuntimeError, "shift: prompt has no handler");
    return -1;
  }

  PyObject *ns = PyDict_New();
  if (! ns) {
    Py_DECREF(wrapper);
    return -1;
  }
  KosDynFrame *handler_frame = kos_dyn_push_frame(ctx, prompt->handler_code, ns);
  Py_DECREF(ns);
  if (! handler_frame) {
    Py_DECREF(wrapper);
    return -1;
  }

  ctx->frame_top->pc = prompt->handler_pc;
  return push_result_or_await(ctx, out_ev, wrapper);
}


/**
   INVOKE_K: arg unused. Pop value then continuation (Python wrapper). Invoke
   the continuation with that value, replacing current execution state.
*/
KOS_OP_HANDLER(INVOKE_K) {
  PyObject *value = KosStackBlock_PopRef(&ctx->frame_top->stack);
  if (! value) {
    SAY("[eval] INVOKE_K pop value failed\n");
    return -1;
  }

  PyObject *cont_obj = KosStackBlock_PopRef(&ctx->frame_top->stack);
  if (! cont_obj) {
    SAY("[eval] INVOKE_K pop cont failed\n");
    Py_DECREF(value);
    return -1;
  }

  if (PyLong_Check(value))
    SAYF("[eval] INVOKE_K value=%ld k=%p\n",
         PyLong_AsLong(value), (void *) cont_obj);

  if (! KosPyDelimitedContinuation_Check(cont_obj)) {
    PyErr_SetString(PyExc_TypeError, "INVOKE_K: TOS is not a DelimitedContinuation");
    Py_DECREF(value);
    Py_DECREF(cont_obj);
    return -1;
  }

  KosContinuation *k = ((KosPyDelimitedContinuation *) cont_obj)->captured;
  if (! k) {
    Py_DECREF(value);
    Py_DECREF(cont_obj);
    PyErr_SetString(PyExc_RuntimeError,
                    "INVOKE_K: continuation has no captured state");
    return -1;
  }

  /* Validate token ownership */
  if (k->token_ref != (PyObject *) ctx->token) {
    Py_DECREF(value);
    Py_DECREF(cont_obj);
    PyErr_SetString(KoskariContinuationError,
                    "INVOKE_K: continuation does not belong to current evaluator");
    return -1;
  }

  /* Validate prompt exists in current dynamic chain */
  if (! kos_find_prompt(ctx, k->prompt_id)) {
    Py_DECREF(value);
    Py_DECREF(cont_obj);
    PyErr_SetString(KoskariContinuationError,
                    "INVOKE_K: prompt not found in dynamic chain");
    return -1;
  }

  SAYF("[eval] INVOKE_K calling kos_invoke_continuation captured_chain=%p\n",
       (void *) k->captured_chain);
  if (kos_invoke_continuation(ctx, k, value, 0, cont_obj) < 0) {
    SAY("[eval] INVOKE_K kos_invoke_continuation returned -1\n");
    Py_DECREF(value);
    Py_DECREF(cont_obj);
    return -1;
  }
  Py_DECREF(value);
  (void) out_ev;
  (void) insn;
  return 0;
}


/**
   INVOKE_K_VAR: arg = constant index (name). Pop value, look up name in
   current namespace, invoke that continuation with value.
*/
KOS_OP_HANDLER(INVOKE_K_VAR) {
  KosCode *code = ctx->frame_top->code;
  PyObject *name = KosCode_GetConstant((PyObject *) code, insn->arg);
  if (! name)
    return -1;

  PyObject *value = KosStackBlock_PopRef(&ctx->frame_top->stack);
  if (! value)
    return -1;

  PyObject *ns = ctx->frame_top->ns;
  PyObject *cont_obj = NULL;
  if (KosNamespace_Check(ns)) {
    cont_obj = KosNamespace_Deref(ns, name);
  } else if (PyDict_Check(ns)) {
    cont_obj = PyDict_GetItemWithError(ns, name);
    if (cont_obj)
      Py_INCREF(cont_obj);
  } else {
    Py_DECREF(value);
    PyErr_Format(PyExc_TypeError,
                 "frame namespace must be Namespace or dict, got %R", ns);
    return -1;
  }

  if (! cont_obj) {
    Py_DECREF(value);
    return -1;
  }

  if (! KosPyDelimitedContinuation_Check(cont_obj)) {
    Py_DECREF(value);
    Py_DECREF(cont_obj);
    PyErr_SetString(PyExc_TypeError,
                    "INVOKE_K_VAR: name is not a DelimitedContinuation");
    return -1;
  }

  KosContinuation *k = ((KosPyDelimitedContinuation *) cont_obj)->captured;

  if (! k) {
    Py_DECREF(value);
    Py_DECREF(cont_obj);
    PyErr_SetString(PyExc_RuntimeError,
                    "INVOKE_K_VAR: continuation has no captured state");
    return -1;
  }

  if (k->token_ref != (PyObject *) ctx->token) {
    Py_DECREF(value);
    Py_DECREF(cont_obj);
    PyErr_SetString(KoskariContinuationError,
                    "INVOKE_K_VAR: continuation does not belong to current"
                    " evaluator");
    return -1;
  }

  if (! kos_find_prompt(ctx, k->prompt_id)) {
    Py_DECREF(value);
    Py_DECREF(cont_obj);
    PyErr_SetString(KoskariContinuationError,
                    "INVOKE_K_VAR: prompt not found in dynamic chain");
    return -1;
  }

  if (kos_invoke_continuation(ctx, k, value, 0, cont_obj) < 0) {
    Py_DECREF(value);
    Py_DECREF(cont_obj);
    return -1;
  }
  Py_DECREF(value);
  /* cont_obj not DECREF'd: invoke transfers control on success */
  (void) out_ev;
  return 0;
}


/**
   Reducing operator: pop opa values (or use TailPtr for opa > 0), call fn,
   drop opa, push result. Faithful to ceval _obj_op.
 */
static int obj_op_reducing(KosContext *ctx, KosEvent *out_ev, KosOp *insn,
                           KosOperatorReducing *fn) {
  int opa = (int) insn->arg;
  PyObject *res;

  if (opa < 0) {
    PyErr_Format(PyExc_RuntimeError,
                 "Negative count argument to reducing operator: %i", opa);
    return -1;
  }

  if (opa == 0) {
    res = fn(NULL, 0);
  } else {
    PyObject *const *args = KosStackBlock_TailPtr(ctx->frame_top->stack,
                                                  (size_t) opa);
    if (! args)
      return -1;
    res = fn(args, (size_t) opa);
    KosStackBlock_DropNoErr(&ctx->frame_top->stack, (size_t) opa);
  }

  if (! res)
    return -1;
  return push_or_await(ctx, out_ev, res);
}


/**
   OBJECT_ADD: arg = number of operands (0 or N). Pop N values, add (binary
   or reduce), push result or await it.
*/
KOS_OP_HANDLER(OBJECT_ADD) {
  return obj_op_reducing(ctx, out_ev, insn, KosOperator_Add);
}


#define OP_UNARY(name, func)                                            \
  KOS_OP_HANDLER(name) {                                                \
    PyObject *val = KosStackBlock_PopRef(&ctx->frame_top->stack);       \
    if (! val) return -1;                                              \
    PyObject *res = func(val);                                          \
    Py_DECREF(val);                                                    \
    if (! res) return -1;                                              \
    (void) insn;                                                       \
    return push_or_await(ctx, out_ev, res);                             \
  }


/**
   OBJECT_STR: arg = number of operands. Build str from top N values (e.g. for
   f-string), push result or await it.
*/
KOS_OP_HANDLER(OBJECT_STR) {
  return obj_op_reducing(ctx, out_ev, insn, KosOperator_BuildStr);
}

OP_UNARY(OBJECT_REPR, PyObject_Repr)
OP_UNARY(OBJECT_ITER, PyObject_GetIter)
OP_UNARY(OBJECT_BIT_INVERT, PyNumber_Invert)
OP_UNARY(OBJECT_DIR, PyObject_Dir)

#undef OP_UNARY


/**
   OBJECT_LEN: arg unused. Pop TOS, push len(TOS) as an integer.
*/
KOS_OP_HANDLER(OBJECT_LEN) {
  PyObject *val = KosStackBlock_PopRef(&ctx->frame_top->stack);
  if (! val)
    return -1;

  Py_ssize_t n = PyObject_Length(val);
  Py_DECREF(val);
  if (n < 0)
    return -1;

  PyObject *res = PyLong_FromSsize_t(n);
  if (! res)
    return -1;
  (void) insn;
  return push_or_await(ctx, out_ev, res);
}


/**
   OBJECT_NOT: arg unused. Pop TOS, push bool(not TOS).
*/
KOS_OP_HANDLER(OBJECT_NOT) {
  PyObject *val = KosStackBlock_PopRef(&ctx->frame_top->stack);
  if (! val)
    return -1;

  int r = PyObject_Not(val);
  Py_DECREF(val);
  if (r < 0)
    return -1;

  PyObject *res = (r ? Py_True : Py_False);
  Py_INCREF(res);
  (void) insn;
  return push_or_await(ctx, out_ev, res);
}


/**
   OBJECT_TRUTH: arg unused. Pop TOS, push bool(TOS).
*/
KOS_OP_HANDLER(OBJECT_TRUTH) {
  PyObject *val = KosStackBlock_PopRef(&ctx->frame_top->stack);
  if (! val)
    return -1;

  int r = PyObject_IsTrue(val);
  Py_DECREF(val);
  if (r < 0)
    return -1;

  PyObject *res = (r ? Py_True : Py_False);
  Py_INCREF(res);
  (void) insn;
  return push_or_await(ctx, out_ev, res);
}


/**
   OBJECT_TYPE: arg unused. Pop TOS, push type(TOS).
*/
KOS_OP_HANDLER(OBJECT_TYPE) {
  PyObject *val = KosStackBlock_PopRef(&ctx->frame_top->stack);
  if (! val)
    return -1;

  PyObject *res = (PyObject *) Py_TYPE(val);
  Py_INCREF(res);
  Py_DECREF(val);
  (void) insn;
  return push_or_await(ctx, out_ev, res);
}


/**
   OBJECT_VARS: arg unused. Pop TOS, push getattr(TOS, "__dict__").
*/
KOS_OP_HANDLER(OBJECT_VARS) {
  PyObject *val = KosStackBlock_PopRef(&ctx->frame_top->stack);
  if (! val)
    return -1;

  PyObject *d = PyObject_GetAttrString(val, "__dict__");
  Py_DECREF(val);
  if (! d) {
    PyErr_Clear();
    PyErr_SetString(PyExc_TypeError,
                    "vars argument must have __dict__ attribute");
    return -1;
  }
  (void) insn;
  return push_or_await(ctx, out_ev, d);
}


/**
   OBJECT_AITER: arg unused. Pop TOS (async iterable), call __aiter__, push
   the async iterator or await it.
*/
KOS_OP_HANDLER(OBJECT_AITER) {
  PyObject *val = KosStackBlock_PopRef(&ctx->frame_top->stack);
  if (! val)
    return -1;

  PyObject *res = NULL;
  PyTypeObject *type = Py_TYPE(val);
  if (type->tp_as_async && type->tp_as_async->am_aiter)
    res = type->tp_as_async->am_aiter(val);
  if (! res) {
    PyErr_Format(PyExc_RuntimeError, "cannot await for elements of %R", val);
    Py_DECREF(val);
    return -1;
  }
  Py_DECREF(val);
  (void) insn;
  return push_or_await(ctx, out_ev, res);
}


#define OP_BINARY_NUM(name, func)                                       \
  KOS_OP_HANDLER(name) {                                                \
    if (insn->arg != 2) {                                              \
      PyErr_SetString(PyExc_RuntimeError, #name ": only arg 2 supported"); \
      return -1;                                                        \
    }                                                                   \
    PyObject *right = KosStackBlock_PopRef(&ctx->frame_top->stack);      \
    if (! right) return -1;                                            \
    PyObject *left = KosStackBlock_PopRef(&ctx->frame_top->stack);      \
    if (! left) { Py_DECREF(right); return -1; }                       \
    PyObject *res = func(left, right);                                 \
    Py_DECREF(left);                                                  \
    Py_DECREF(right);                                                 \
    if (! res) return -1;                                             \
    return push_or_await(ctx, out_ev, res);                            \
  }


/**
   OBJECT_SUBTRACT: arg = number of operands. Pop N, subtract (binary/reduce),
   push result or await it.
*/
KOS_OP_HANDLER(OBJECT_SUBTRACT) {
  return obj_op_reducing(ctx, out_ev, insn, KosOperator_Subtract);
}


/**
   OBJECT_MULTIPLY: arg = number of operands. Pop N, multiply, push result
   or await it.
*/
KOS_OP_HANDLER(OBJECT_MULTIPLY) {
  return obj_op_reducing(ctx, out_ev, insn, KosOperator_Multiply);
}


/**
   OBJECT_FLOOR_DIVIDE: arg = number of operands. Pop N, floor-divide,
   push result or await it.
*/
KOS_OP_HANDLER(OBJECT_FLOOR_DIVIDE) {
  return obj_op_reducing(ctx, out_ev, insn, KosOperator_FloorDivide);
}


/**
   OBJECT_TRUE_DIVIDE: arg = number of operands. Pop N, true-divide,
   push result or await it.
*/
KOS_OP_HANDLER(OBJECT_TRUE_DIVIDE) {
  return obj_op_reducing(ctx, out_ev, insn, KosOperator_TrueDivide);
}


OP_BINARY_NUM(OBJECT_MODULO, PyNumber_Remainder)

OP_BINARY_NUM(OBJECT_DIVMOD, PyNumber_Divmod)


/**
   OBJECT_BIT_AND: arg = number of operands. Pop N, bitwise and, push
   result or await it.
*/
KOS_OP_HANDLER(OBJECT_BIT_AND) {
  return obj_op_reducing(ctx, out_ev, insn, KosOperator_BitwiseAnd);
}


/**
   OBJECT_BIT_OR: arg = number of operands. Pop N, bitwise or, push result
   or await it.
*/
KOS_OP_HANDLER(OBJECT_BIT_OR) {
  return obj_op_reducing(ctx, out_ev, insn, KosOperator_BitwiseOr);
}


/**
   OBJECT_BIT_XOR: arg = number of operands. Pop N, bitwise xor, push
   result or await it.
*/
KOS_OP_HANDLER(OBJECT_BIT_XOR) {
  return obj_op_reducing(ctx, out_ev, insn, KosOperator_BitwiseXor);
}


/**
   OBJECT_MATRIX_MULTIPLY: arg = number of operands. Pop N, matrix multiply
   (@), push result or await it.
*/
KOS_OP_HANDLER(OBJECT_MATRIX_MULTIPLY) {
  return obj_op_reducing(ctx, out_ev, insn, KosOperator_MatrixMultiply);
}

#undef OP_BINARY_NUM


/**
   OBJECT_POWER: arg = 2 (base, exp) or 3 (base, exp, mod). Pop operands,
   compute pow(base, exp[, mod]), push result or await it.
*/
KOS_OP_HANDLER(OBJECT_POWER) {
  int opa = (int) insn->arg;
  if (opa < 2 || opa > 3) {
    PyErr_Format(PyExc_RuntimeError,
                 "OBJECT_POWER requires 2 or 3 arguments, got %i", opa);
    return -1;
  }

  PyObject *base = NULL, *exp = NULL, *mod = Py_None;

  if (opa == 3) {
    mod = KosStackBlock_PopRef(&ctx->frame_top->stack);
    if (! mod)
      return -1;
  }

  exp = KosStackBlock_PopRef(&ctx->frame_top->stack);
  if (! exp) {
    if (opa == 3)
      Py_DECREF(mod);
    return -1;
  }

  base = KosStackBlock_PopRef(&ctx->frame_top->stack);
  if (! base) {
    Py_DECREF(exp);
    if (opa == 3)
      Py_DECREF(mod);
    return -1;
  }

  PyObject *val = PyNumber_Power(base, exp, mod);
  Py_DECREF(base);
  Py_DECREF(exp);
  if (opa == 3)
    Py_DECREF(mod);
  if (! val)
    return -1;
  return push_or_await(ctx, out_ev, val);
}


/**
   Binary op: pop right then left, call fn(left, right), push result or await.
   fn returns a new reference or NULL.
*/
static int obj_op_binary(KosContext *ctx, KosEvent *out_ev, KosOp *insn,
                         PyObject *(*fn)(PyObject *left, PyObject *right)) {
  PyObject *right = KosStackBlock_PopRef(&ctx->frame_top->stack);
  if (! right)
    return -1;
  PyObject *left = KosStackBlock_PopRef(&ctx->frame_top->stack);
  if (! left) {
    Py_DECREF(right);
    return -1;
  }
  PyObject *res = fn(left, right);
  Py_DECREF(left);
  Py_DECREF(right);
  if (! res)
    return -1;
  (void) insn;
  return push_result_or_await(ctx, out_ev, res);
}


static PyObject *binary_in_result(PyObject *left, PyObject *right) {
  int r = PySequence_Contains(right, left);
  if (r < 0)
    return NULL;
  PyObject *res = (r ? Py_True : Py_False);
  Py_INCREF(res);
  return res;
}


static PyObject *binary_not_in_result(PyObject *left, PyObject *right) {
  int r = PySequence_Contains(right, left);
  if (r < 0)
    return NULL;
  PyObject *res = (r ? Py_False : Py_True);
  Py_INCREF(res);
  return res;
}


static PyObject *binary_is_result(PyObject *left, PyObject *right) {
  PyObject *res = (left == right ? Py_True : Py_False);
  Py_INCREF(res);
  return res;
}


static PyObject *binary_is_not_result(PyObject *left, PyObject *right) {
  PyObject *res = (left != right ? Py_True : Py_False);
  Py_INCREF(res);
  return res;
}


/**
   OBJECT_BIT_LEFT: arg unused. Pop right then left, push left << right.
*/
KOS_OP_HANDLER(OBJECT_BIT_LEFT) {
  return obj_op_binary(ctx, out_ev, insn, PyNumber_Lshift);
}


/**
   OBJECT_BIT_RIGHT: arg unused. Pop right then left, push left >> right.
*/
KOS_OP_HANDLER(OBJECT_BIT_RIGHT) {
  return obj_op_binary(ctx, out_ev, insn, PyNumber_Rshift);
}


static int op_OBJECT_EQ_impl(KosContext *ctx, KosEvent *out_ev, KosOp *insn,
                             int op) {
  PyObject *right = KosStackBlock_PopRef(&ctx->frame_top->stack);
  if (! right)
    return -1;
  PyObject *left = KosStackBlock_PopRef(&ctx->frame_top->stack);
  if (! left) {
    Py_DECREF(right);
    return -1;
  }

  int cmp = PyObject_RichCompareBool(left, right, op);
  Py_DECREF(left);
  Py_DECREF(right);
  if (cmp < 0 && PyErr_Occurred())
    return -1;

  PyObject *res = (cmp ? Py_True : Py_False);
  Py_INCREF(res);
  (void) insn;
  return push_or_await(ctx, out_ev, res);
}


/**
   OBJECT_EQ: arg unused. Pop right then left, push (left == right).
*/
KOS_OP_HANDLER(OBJECT_EQ) {
  return op_OBJECT_EQ_impl(ctx, out_ev, insn, Py_EQ);
}

/**
   OBJECT_NE: arg unused. Pop right then left, push (left != right).
*/
KOS_OP_HANDLER(OBJECT_NE) {
  return op_OBJECT_EQ_impl(ctx, out_ev, insn, Py_NE);
}

/**
   OBJECT_LT: arg unused. Pop right then left, push (left < right).
*/
KOS_OP_HANDLER(OBJECT_LT) {
  return op_OBJECT_EQ_impl(ctx, out_ev, insn, Py_LT);
}

/**
   OBJECT_LE: arg unused. Pop right then left, push (left <= right).
*/
KOS_OP_HANDLER(OBJECT_LE) {
  return op_OBJECT_EQ_impl(ctx, out_ev, insn, Py_LE);
}

/**
   OBJECT_GT: arg unused. Pop right then left, push (left > right).
*/
KOS_OP_HANDLER(OBJECT_GT) {
  return op_OBJECT_EQ_impl(ctx, out_ev, insn, Py_GT);
}

/**
   OBJECT_GE: arg unused. Pop right then left, push (left >= right).
*/
KOS_OP_HANDLER(OBJECT_GE) {
  return op_OBJECT_EQ_impl(ctx, out_ev, insn, Py_GE);
}


/**
   OBJECT_IN: arg unused. Pop right (container) then left (item), push
   (left in right).
*/
KOS_OP_HANDLER(OBJECT_IN) {
  return obj_op_binary(ctx, out_ev, insn, binary_in_result);
}


/**
   OBJECT_NOT_IN: arg unused. Pop right then left, push (left not in right).
*/
KOS_OP_HANDLER(OBJECT_NOT_IN) {
  return obj_op_binary(ctx, out_ev, insn, binary_not_in_result);
}


/**
   OBJECT_IS: arg unused. Pop right then left, push (left is right).
*/
KOS_OP_HANDLER(OBJECT_IS) {
  return obj_op_binary(ctx, out_ev, insn, binary_is_result);
}


/**
   OBJECT_IS_NOT: arg unused. Pop right then left, push (left is not right).
*/
KOS_OP_HANDLER(OBJECT_IS_NOT) {
  return obj_op_binary(ctx, out_ev, insn, binary_is_not_result);
}


/**
   OBJECT_IS_INSTANCE: arg = 2 (value, class) or N (value, class, ... bases).
   Pop value and class(es), push isinstance(value, class_or_tuple).
*/
KOS_OP_HANDLER(OBJECT_IS_INSTANCE) {
  int opa = (int) insn->arg;
  PyObject *value = NULL, *match = NULL;

  if (opa < 2) {
    match = KosStackBlock_PopRef(&ctx->frame_top->stack);
    if (! match)
      return -1;
    value = KosStackBlock_PopRef(&ctx->frame_top->stack);
    if (! value) {
      Py_DECREF(match);
      return -1;
    }
  } else {
    match = KosStackBlock_PopTail(&ctx->frame_top->stack, (size_t) opa);
    if (! match)
      return -1;
    value = KosStackBlock_PopRef(&ctx->frame_top->stack);
    if (! value) {
      Py_DECREF(match);
      return -1;
    }
  }

  int res = PyObject_IsInstance(value, match);
  Py_DECREF(value);
  Py_DECREF(match);
  if (res < 0)
    return -1;

  PyObject *out = (res ? Py_True : Py_False);
  Py_INCREF(out);
  return push_or_await(ctx, out_ev, out);
}


/**
   OBJECT_IS_SUBCLASS: arg = 2 (class, base) or N (class, base, ... bases).
   Pop class and base(s), push issubclass(class, base_or_tuple).
*/
KOS_OP_HANDLER(OBJECT_IS_SUBCLASS) {
  int opa = (int) insn->arg;
  PyObject *value = NULL, *match = NULL;

  if (opa < 2) {
    match = KosStackBlock_PopRef(&ctx->frame_top->stack);
    if (! match)
      return -1;
    value = KosStackBlock_PopRef(&ctx->frame_top->stack);
    if (! value) {
      Py_DECREF(match);
      return -1;
    }
  } else {
    match = KosStackBlock_PopTail(&ctx->frame_top->stack, (size_t) opa);
    if (! match)
      return -1;
    value = KosStackBlock_PopRef(&ctx->frame_top->stack);
    if (! value) {
      Py_DECREF(match);
      return -1;
    }
  }

  int res = PyObject_IsSubclass(value, match);
  Py_DECREF(value);
  Py_DECREF(match);
  if (res < 0)
    return -1;

  PyObject *out = (res ? Py_True : Py_False);
  Py_INCREF(out);
  return push_or_await(ctx, out_ev, out);
}


/* Unimplemented opcodes: documented handlers that delegate to op_unimplemented_stub. */

/**
   THEEND: arg unused. End of code; not yet implemented.
*/
KOS_OP_HANDLER(THEEND) {
  return op_unimplemented_stub(ctx, out_ev, insn);
}


/**
   BREAK: arg = target. Exit loop; not yet implemented.
*/
KOS_OP_HANDLER(BREAK) {
  return op_unimplemented_stub(ctx, out_ev, insn);
}


/**
   CONTINUE: arg = target. Next iteration of loop; not yet implemented.
*/
KOS_OP_HANDLER(CONTINUE) {
  return op_unimplemented_stub(ctx, out_ev, insn);
}


/**
   YIELD: arg unused. Suspend and yield value; not yet implemented.
*/
KOS_OP_HANDLER(YIELD) {
  return op_unimplemented_stub(ctx, out_ev, insn);
}


/**
   YIELD_FROM: arg unused. Delegate to subgenerator; not yet implemented.
*/
KOS_OP_HANDLER(YIELD_FROM) {
  return op_unimplemented_stub(ctx, out_ev, insn);
}


/**
   ITER_NEXT: arg unused. Advance iterator, push next or raise StopIteration;
   not yet implemented.
*/
KOS_OP_HANDLER(ITER_NEXT) {
  return op_unimplemented_stub(ctx, out_ev, insn);
}


/**
   AWAIT_NEXT: arg unused. Advance async iterator; not yet implemented.
*/
KOS_OP_HANDLER(AWAIT_NEXT) {
  return op_unimplemented_stub(ctx, out_ev, insn);
}


/**
   JUMP: arg = target PC. Unconditional branch; not yet implemented.
*/
KOS_OP_HANDLER(JUMP) {
  return op_unimplemented_stub(ctx, out_ev, insn);
}


/**
   JUMP_FALSE: arg = target PC. Branch if TOS is false; not yet implemented.
*/
KOS_OP_HANDLER(JUMP_FALSE) {
  return op_unimplemented_stub(ctx, out_ev, insn);
}


/**
   JUMP_TRUE: arg = target PC. Branch if TOS is true; not yet implemented.
*/
KOS_OP_HANDLER(JUMP_TRUE) {
  return op_unimplemented_stub(ctx, out_ev, insn);
}


/**
   JUMP_AND: arg = target PC. Short-circuit and; not yet implemented.
*/
KOS_OP_HANDLER(JUMP_AND) {
  return op_unimplemented_stub(ctx, out_ev, insn);
}


/**
   JUMP_OR: arg = target PC. Short-circuit or; not yet implemented.
*/
KOS_OP_HANDLER(JUMP_OR) {
  return op_unimplemented_stub(ctx, out_ev, insn);
}


/**
   CHECK_EQ: arg = target PC. Branch if comparison fails (==); not yet implemented.
*/
KOS_OP_HANDLER(CHECK_EQ) {
  return op_unimplemented_stub(ctx, out_ev, insn);
}


/**
   CHECK_GE: arg = target PC. Branch if comparison fails (>=); not yet implemented.
*/
KOS_OP_HANDLER(CHECK_GE) {
  return op_unimplemented_stub(ctx, out_ev, insn);
}


/**
   CHECK_GT: arg = target PC. Branch if comparison fails (>); not yet implemented.
*/
KOS_OP_HANDLER(CHECK_GT) {
  return op_unimplemented_stub(ctx, out_ev, insn);
}


/**
   CHECK_LE: arg = target PC. Branch if comparison fails (<=); not yet implemented.
*/
KOS_OP_HANDLER(CHECK_LE) {
  return op_unimplemented_stub(ctx, out_ev, insn);
}


/**
   CHECK_LT: arg = target PC. Branch if comparison fails (<); not yet implemented.
*/
KOS_OP_HANDLER(CHECK_LT) {
  return op_unimplemented_stub(ctx, out_ev, insn);
}


/**
   CHECK_NE: arg = target PC. Branch if comparison fails (!=); not yet implemented.
*/
KOS_OP_HANDLER(CHECK_NE) {
  return op_unimplemented_stub(ctx, out_ev, insn);
}


/**
   CHECK_IN: arg = target PC. Branch if "in" check fails; not yet implemented.
*/
KOS_OP_HANDLER(CHECK_IN) {
  return op_unimplemented_stub(ctx, out_ev, insn);
}


/**
   CHECK_NOT_IN: arg = target PC. Branch if "not in" check fails; not yet implemented.
*/
KOS_OP_HANDLER(CHECK_NOT_IN) {
  return op_unimplemented_stub(ctx, out_ev, insn);
}


/**
   CHECK_IS: arg = target PC. Branch if "is" check fails; not yet implemented.
*/
KOS_OP_HANDLER(CHECK_IS) {
  return op_unimplemented_stub(ctx, out_ev, insn);
}


/**
   CHECK_IS_NOT: arg = target PC. Branch if "is not" check fails; not yet implemented.
*/
KOS_OP_HANDLER(CHECK_IS_NOT) {
  return op_unimplemented_stub(ctx, out_ev, insn);
}


/**
   CHECK_IS_INSTANCE: arg = target PC. Branch if isinstance check fails;
   not yet implemented.
*/
KOS_OP_HANDLER(CHECK_IS_INSTANCE) {
  return op_unimplemented_stub(ctx, out_ev, insn);
}


/**
   CHECK_IS_SUBCLASS: arg = target PC. Branch if issubclass check fails;
   not yet implemented.
*/
KOS_OP_HANDLER(CHECK_IS_SUBCLASS) {
  return op_unimplemented_stub(ctx, out_ev, insn);
}


/**
   VAR_BIND: arg = constant index (name). Bind name in current namespace to
   the value at TOS (pop value, then bind).
*/
KOS_OP_HANDLER(VAR_BIND) {
  PyObject *name = get_name_from_insn(ctx->frame_top->code, insn);
  if (! name)
    return -1;
  PyObject *val = KosStackBlock_PopRef(&ctx->frame_top->stack);
  if (! val)
    return -1;
  if (frame_ns_bind(ctx, name, val) < 0)
    return -1;
  (void) out_ev;
  return 0;
}


/**
   VAR_CLEAR: arg = constant index (name). Delete name from namespace.
*/
KOS_OP_HANDLER(VAR_CLEAR) {
  PyObject *name = get_name_from_insn(ctx->frame_top->code, insn);
  if (! name)
    return -1;
  if (frame_ns_clear(ctx, name) < 0)
    return -1;
  (void) out_ev;
  return 0;
}


/**
   VAR_REF: arg = constant index (name). Push name reference (for closure).
*/
KOS_OP_HANDLER(VAR_REF) {
  PyObject *name = get_name_from_insn(ctx->frame_top->code, insn);
  if (! name)
    return -1;
  PyObject *ref = frame_ns_ref(ctx, name);
  if (! ref)
    return -1;
  Py_INCREF(ref);
  return push_result_or_await(ctx, out_ev, ref);
}


/**
   VAR_STORE: arg = constant index (name). Pop TOS and store into name in
   the current frame namespace.
*/
KOS_OP_HANDLER(VAR_STORE) {
  PyObject *name = get_name_from_insn(ctx->frame_top->code, insn);
  if (! name)
    return -1;
  PyObject *val = KosStackBlock_PopRef(&ctx->frame_top->stack);
  if (! val)
    return -1;
  if (frame_ns_store(ctx, name, val) < 0) {
    Py_DECREF(val);
    return -1;
  }
  Py_DECREF(val);
  (void) out_ev;
  return 0;
}


/**
   FAST: arg = slot index. Push value from fast local slot.
*/
KOS_OP_HANDLER(FAST) {
  PyObject *ns = ctx->frame_top->ns;
  if (! KosNamespace_SupportsFast(ns)) {
    PyErr_Format(PyExc_RuntimeError, "FAST invoked on %R", ns);
    return -1;
  }
  size_t idx = (size_t) insn->arg;
  PyObject *val = KosNamespace_FastDeref(ns, idx);
  if (! val)
    return -1;
  return push_or_await(ctx, out_ev, val);
}


/**
   FAST_BIND: arg = slot index. Bind name to fast slot.
*/
KOS_OP_HANDLER(FAST_BIND) {
  PyObject *ns = ctx->frame_top->ns;
  if (! KosNamespace_SupportsFast(ns)) {
    PyErr_Format(PyExc_RuntimeError, "FAST_BIND invoked on %R", ns);
    return -1;
  }
  size_t idx = (size_t) insn->arg;
  if (idx >= KosNamespace_FAST_COUNT(ns)) {
    PyErr_Format(PyExc_NameError, "Namespace fast index out of bounds, %zu",
                 idx);
    return -1;
  }
  PyObject *val = KosStackBlock_PopRef(&ctx->frame_top->stack);
  if (! val)
    return -1;
  KosNameBind *nb = KosNamespace_GET_FAST(ns, idx);
  PyObject *ref = KosSymbolicRef_New(nb->name, val);
  Py_DECREF(val);
  if (! ref)
    return -1;
  int res = KosNamespace_FastBind(ns, idx, ref);
  Py_DECREF(ref);
  if (res < 0)
    return -1;
  (void) out_ev;
  return 0;
}


/**
   FAST_CLEAR: arg = slot index. Clear fast slot; not yet implemented.
*/
KOS_OP_HANDLER(FAST_CLEAR) {
  return op_unimplemented_stub(ctx, out_ev, insn);
}


/**
   FAST_REF: arg = slot index. Push reference to fast slot.
*/
KOS_OP_HANDLER(FAST_REF) {
  PyObject *ns = ctx->frame_top->ns;
  if (! KosNamespace_SupportsFast(ns)) {
    PyErr_Format(PyExc_RuntimeError, "FAST_REF invoked on %R", ns);
    return -1;
  }
  size_t idx = (size_t) insn->arg;
  PyObject *ref = KosNamespace_FastRef(ns, idx);
  if (! ref)
    return -1;
  Py_INCREF(ref);
  return push_result_or_await(ctx, out_ev, ref);
}


/**
   FAST_STORE: arg = slot index. Store TOS into fast slot.
*/
KOS_OP_HANDLER(FAST_STORE) {
  PyObject *ns = ctx->frame_top->ns;
  if (! KosNamespace_SupportsFast(ns)) {
    PyErr_Format(PyExc_RuntimeError, "FAST_STORE invoked on %R", ns);
    return -1;
  }
  size_t idx = (size_t) insn->arg;
  PyObject *val = KosStackBlock_PopRef(&ctx->frame_top->stack);
  if (! val)
    return -1;
  int res = KosNamespace_FastStore(ns, idx, val);
  Py_DECREF(val);
  if (res < 0)
    return -1;
  (void) out_ev;
  return 0;
}


/**
   ATTR: arg = constant index (name). Push getattr(TOS, name); not yet implemented.
*/
KOS_OP_HANDLER(ATTR) {
  return op_unimplemented_stub(ctx, out_ev, insn);
}


/**
   ATTR_CLEAR: arg = constant index (name). delattr(TOS, name); not yet implemented.
*/
KOS_OP_HANDLER(ATTR_CLEAR) {
  return op_unimplemented_stub(ctx, out_ev, insn);
}


/**
   ATTR_REF: arg = constant index (name). Push attribute reference; not yet implemented.
*/
KOS_OP_HANDLER(ATTR_REF) {
  return op_unimplemented_stub(ctx, out_ev, insn);
}


/**
   ATTR_STORE: arg = constant index (name). setattr(TOS, name, value); not yet implemented.
*/
KOS_OP_HANDLER(ATTR_STORE) {
  return op_unimplemented_stub(ctx, out_ev, insn);
}


/**
   ITEM: arg unused. Push TOS1[TOS] (subscript); not yet implemented.
*/
KOS_OP_HANDLER(ITEM) {
  return op_unimplemented_stub(ctx, out_ev, insn);
}


/**
   ITEM_CLEAR: arg unused. del TOS1[TOS]; not yet implemented.
*/
KOS_OP_HANDLER(ITEM_CLEAR) {
  return op_unimplemented_stub(ctx, out_ev, insn);
}


/**
   ITEM_REF: arg unused. Push item reference for subscription; not yet implemented.
*/
KOS_OP_HANDLER(ITEM_REF) {
  return op_unimplemented_stub(ctx, out_ev, insn);
}


/**
   ITEM_STORE: arg unused. TOS1[TOS] = value; not yet implemented.
*/
KOS_OP_HANDLER(ITEM_STORE) {
  return op_unimplemented_stub(ctx, out_ev, insn);
}


/**
   CONST_ITEM: arg = constant index (key). Push TOS[key]; not yet implemented.
*/
KOS_OP_HANDLER(CONST_ITEM) {
  return op_unimplemented_stub(ctx, out_ev, insn);
}


/**
   CONST_ITEM_CLEAR: arg = constant index (key). del TOS[key]; not yet implemented.
*/
KOS_OP_HANDLER(CONST_ITEM_CLEAR) {
  return op_unimplemented_stub(ctx, out_ev, insn);
}


/**
   CONST_ITEM_REF: arg = constant index (key). Push constant-key item reference;
   not yet implemented.
*/
KOS_OP_HANDLER(CONST_ITEM_REF) {
  return op_unimplemented_stub(ctx, out_ev, insn);
}


/**
   CONST_ITEM_STORE: arg = constant index (key). TOS[key] = value; not yet implemented.
*/
KOS_OP_HANDLER(CONST_ITEM_STORE) {
  return op_unimplemented_stub(ctx, out_ev, insn);
}


/**
   BUILD_ARGS: arg unused. Build positional args tuple from stack; not yet implemented.
*/
KOS_OP_HANDLER(BUILD_ARGS) {
  return op_unimplemented_stub(ctx, out_ev, insn);
}


/**
   BUILD_FUNCTION: arg = constant index or similar. Build function/callable from stack;
   not yet implemented.
*/
KOS_OP_HANDLER(BUILD_FUNCTION) {
  return op_unimplemented_stub(ctx, out_ev, insn);
}


/**
   BUILD_KWDS: arg unused. Build keyword dict from stack; not yet implemented.
*/
KOS_OP_HANDLER(BUILD_KWDS) {
  return op_unimplemented_stub(ctx, out_ev, insn);
}


/**
   BUILD_KWDS_NAMES: arg = constant (names). Build keyword dict from names and stack;
   not yet implemented.
*/
KOS_OP_HANDLER(BUILD_KWDS_NAMES) {
  return op_unimplemented_stub(ctx, out_ev, insn);
}


/**
   COMBINE_ARGS: arg unused. Combine positional args for call; not yet implemented.
*/
KOS_OP_HANDLER(COMBINE_ARGS) {
  return op_unimplemented_stub(ctx, out_ev, insn);
}


/**
   COMBINE_KWDS: arg unused. Combine keyword args for call; not yet implemented.
*/
KOS_OP_HANDLER(COMBINE_KWDS) {
  return op_unimplemented_stub(ctx, out_ev, insn);
}


/**
   CALL_NAMES: arg = N. Call with keyword names on stack; not yet implemented.
*/
KOS_OP_HANDLER(CALL_NAMES) {
  return op_unimplemented_stub(ctx, out_ev, insn);
}


/**
   CALL_STAR: arg = N. Call with *args; not yet implemented.
*/
KOS_OP_HANDLER(CALL_STAR) {
  return op_unimplemented_stub(ctx, out_ev, insn);
}


/**
   CALL_ATTR: arg = constant (name). Call TOS1.name(*args); not yet implemented.
*/
KOS_OP_HANDLER(CALL_ATTR) {
  return op_unimplemented_stub(ctx, out_ev, insn);
}


/**
   CALL_ATTR_NAMES: arg = N. Call TOS.name(..., **kwds); not yet implemented.
*/
KOS_OP_HANDLER(CALL_ATTR_NAMES) {
  return op_unimplemented_stub(ctx, out_ev, insn);
}


/**
   CALL_ATTR_STAR: arg = N. Call TOS.name(*args, ...); not yet implemented.
*/
KOS_OP_HANDLER(CALL_ATTR_STAR) {
  return op_unimplemented_stub(ctx, out_ev, insn);
}


/**
   RECUR: arg = N. Recursive call (same function); not yet implemented.
*/
KOS_OP_HANDLER(RECUR) {
  return op_unimplemented_stub(ctx, out_ev, insn);
}


/**
   RECUR_NAMES: arg = N. Recursive call with keyword names; not yet implemented.
*/
KOS_OP_HANDLER(RECUR_NAMES) {
  return op_unimplemented_stub(ctx, out_ev, insn);
}


/**
   RECUR_STAR: arg = N. Recursive call with *args; not yet implemented.
*/
KOS_OP_HANDLER(RECUR_STAR) {
  return op_unimplemented_stub(ctx, out_ev, insn);
}


/**
   TAIL_CALL: arg = N. Tail call (replace frame); not yet implemented.
*/
KOS_OP_HANDLER(TAIL_CALL) {
  return op_unimplemented_stub(ctx, out_ev, insn);
}


/**
   TAIL_CALL_NAMES: arg = N. Tail call with keyword names; not yet implemented.
*/
KOS_OP_HANDLER(TAIL_CALL_NAMES) {
  return op_unimplemented_stub(ctx, out_ev, insn);
}


/**
   TAIL_CALL_STAR: arg = N. Tail call with *args; not yet implemented.
*/
KOS_OP_HANDLER(TAIL_CALL_STAR) {
  return op_unimplemented_stub(ctx, out_ev, insn);
}


/**
   TAIL_RECUR: arg = N. Tail recursive call; not yet implemented.
*/
KOS_OP_HANDLER(TAIL_RECUR) {
  return op_unimplemented_stub(ctx, out_ev, insn);
}


/**
   TAIL_RECUR_NAMES: arg = N. Tail recur with keyword names; not yet implemented.
*/
KOS_OP_HANDLER(TAIL_RECUR_NAMES) {
  return op_unimplemented_stub(ctx, out_ev, insn);
}


/**
   TAIL_RECUR_STAR: arg = N. Tail recur with *args; not yet implemented.
*/
KOS_OP_HANDLER(TAIL_RECUR_STAR) {
  return op_unimplemented_stub(ctx, out_ev, insn);
}


/**
   PUSH_FOREACH: arg = target. Push for-each/iteration context; not yet implemented.
*/
KOS_OP_HANDLER(PUSH_FOREACH) {
  return op_unimplemented_stub(ctx, out_ev, insn);
}


/**
   PUSH_WITH: arg = target. Push with/context-manager; not yet implemented.
*/
KOS_OP_HANDLER(PUSH_WITH) {
  return op_unimplemented_stub(ctx, out_ev, insn);
}


/**
   PUSH_BEGIN: arg = target. Push begin block; not yet implemented.
*/
KOS_OP_HANDLER(PUSH_BEGIN) {
  return op_unimplemented_stub(ctx, out_ev, insn);
}


/**
   PUSH_CLASS: arg unused. Push class-building context; not yet implemented.
*/
KOS_OP_HANDLER(PUSH_CLASS) {
  return op_unimplemented_stub(ctx, out_ev, insn);
}


/**
   PUSH_LOOP: arg = target. Push loop context (for break/continue); not yet implemented.
*/
KOS_OP_HANDLER(PUSH_LOOP) {
  return op_unimplemented_stub(ctx, out_ev, insn);
}


/**
   PUSH_NS: arg = count (opa) of values to bind. Stack layout:
   opa == 0: [NSPEC]
   opa > 0:  [NSPEC, val0, val1, ..., val(N-1), NAMES]
   NSPEC from constant pool; NAMES = tuple of names. Creates child namespace
   from NSPEC, optionally binds values to names, pushes KOS_DYN_NS.
*/
KOS_OP_HANDLER(PUSH_NS) {
  int opa = (int) insn->arg;
  PyObject *nspec = NULL;
  PyObject *names = NULL;
  PyObject *const *args = NULL;
  PyObject *parent = ctx->frame_top->ns;
  PyObject *new_ns = NULL;
  KosDynNs *ns_node = NULL;

  if (opa < 0) {
    PyErr_Format(PyExc_RuntimeError,
                 "PUSH_NS called with negative count: %i", opa);
    return -1;
  }

  if (opa == 0) {
    nspec = KosStackBlock_PopRef(&ctx->frame_top->stack);
    if (! nspec) {
      PyErr_SetString(PyExc_RuntimeError, "PUSH_NS stack underrun");
      return -1;
    }
  } else {
    names = KosStackBlock_PopRef(&ctx->frame_top->stack);
    if (! names) {
      PyErr_SetString(PyExc_RuntimeError, "PUSH_NS stack underrun");
      return -1;
    }
    if (! PyTuple_CheckExact(names)) {
      Py_DECREF(names);
      PyErr_Format(PyExc_RuntimeError,
                   "PUSH_NS names must be tuple, not %R", names);
      return -1;
    }
    args = KosStackBlock_TailPtr(ctx->frame_top->stack, (size_t) opa + 1);
    if (! args) {
      Py_DECREF(names);
      PyErr_SetString(PyExc_RuntimeError, "PUSH_NS stack underrun");
      return -1;
    }
    nspec = args[0];
    Py_INCREF(nspec);
  }

  if (! KosNamespaceSpec_CheckExact(nspec)) {
    Py_DECREF(nspec);
    Py_XDECREF(names);
    PyErr_Format(PyExc_RuntimeError,
                 "PUSH_NS spec must be a NamespaceSpec, not %R", nspec);
    return -1;
  }

  {
    int i = names ? (int) PyTuple_GET_SIZE(names) : 0;
    if (i != opa) {
      Py_DECREF(nspec);
      Py_XDECREF(names);
      PyErr_Format(PyExc_RuntimeError,
                   "PUSH_NS names length (%i) must match value count (%i)",
                   i, opa);
      return -1;
    }
  }

  /*
   * Use parent only if it is a KosNamespace (supports closure lookup).
   * When frame has a dict (e.g. shift handler), use NULL so nspec
   * must have empty closure_syms.
   */
  PyObject *spec_parent = KosNamespace_Check(parent) ? parent : NULL;
  new_ns = KosNamespace_FromSpec(nspec, spec_parent);
  Py_DECREF(nspec);
  if (! new_ns) {
    Py_XDECREF(names);
    return -1;
  }

  if (opa > 0) {
    for (int i = 0; i < opa; i++) {
      PyObject *name = PyTuple_GET_ITEM(names, i);
      PyObject *val = args[i + 1];
      if (KosNamespace_Store(new_ns, name, val) < 0) {
        Py_DECREF(names);
        Py_DECREF(new_ns);
        return -1;
      }
    }
    KosStackBlock_DropNoErr(&ctx->frame_top->stack, (size_t) opa + 1);
    Py_DECREF(names);
  }

  Py_INCREF(parent);
  ns_node = kos_dyn_push_ns(ctx, parent);
  if (! ns_node) {
    Py_DECREF(parent);
    Py_DECREF(new_ns);
    return -1;
  }

  Py_DECREF(ctx->frame_top->ns);
  ctx->frame_top->ns = new_ns;

  (void) out_ev;
  return 0;
}


/**
   CLEAR_LOOP: arg unused. Mark loop as exited; not yet implemented.
*/
KOS_OP_HANDLER(CLEAR_LOOP) {
  return op_unimplemented_stub(ctx, out_ev, insn);
}


/**
   POP_WITH: arg unused. Pop with context; not yet implemented.
*/
KOS_OP_HANDLER(POP_WITH) {
  return op_unimplemented_stub(ctx, out_ev, insn);
}


/**
   POP_BEGIN: arg unused. Pop begin block; not yet implemented.
*/
KOS_OP_HANDLER(POP_BEGIN) {
  return op_unimplemented_stub(ctx, out_ev, insn);
}


/**
   POP_LOOP: arg unused. Pop loop context; not yet implemented.
*/
KOS_OP_HANDLER(POP_LOOP) {
  return op_unimplemented_stub(ctx, out_ev, insn);
}


/**
   POP_NS: arg unused. Verify dyn_top is KOS_DYN_NS, restore frame ns from
   saved_ns, pop the NS node.
*/
KOS_OP_HANDLER(POP_NS) {
  if (! ctx->dyn_top || ! KosDynNode_IS_NS(ctx->dyn_top)) {
    PyErr_SetString(PyExc_RuntimeError, "POP_NS: top is not a namespace scope");
    return -1;
  }

  KosDynNs *ns_node = (KosDynNs *) ctx->dyn_top;
  PyObject *prior = ns_node->saved_ns;
  Py_INCREF(prior);
  Py_DECREF(ctx->frame_top->ns);
  ctx->frame_top->ns = prior;
  kos_dyn_pop_ns(ctx, ns_node);

  (void) out_ev;
  (void) insn;
  return 0;
}


/**
   PUSH_WIND: arg unused. Push dynamic-wind before thunk; not yet implemented.
*/
KOS_OP_HANDLER(PUSH_WIND) {
  return op_unimplemented_stub(ctx, out_ev, insn);
}


/**
   POP_WIND: arg unused. Pop dynamic-wind; not yet implemented.
*/
KOS_OP_HANDLER(POP_WIND) {
  return op_unimplemented_stub(ctx, out_ev, insn);
}


/**
   END_WIND: arg unused. End wind scope; not yet implemented.
*/
KOS_OP_HANDLER(END_WIND) {
  return op_unimplemented_stub(ctx, out_ev, insn);
}


/**
   PUSH_REGION: arg = style/label. Push generic region; not yet implemented.
*/
KOS_OP_HANDLER(PUSH_REGION) {
  return op_unimplemented_stub(ctx, out_ev, insn);
}


/**
   POP_REGION: arg unused. Pop region; not yet implemented.
*/
KOS_OP_HANDLER(POP_REGION) {
  return op_unimplemented_stub(ctx, out_ev, insn);
}


/* Dispatch table generated from opcodes.incl; every op_##name must be
   defined (implemented handler).  Omitted when KOS_USE_COMPUTED_GOTO so
   the compiler can inline handler bodies at the jump targets. */
#if !KOS_USE_COMPUTED_GOTO
static int (*const op_handlers[])(KosContext *, KosEvent *, KosOp *) = {
#define OP(name, style) op_##name,
#include "opcodes.incl"
#undef OP
};
#endif


/**
   Try to handle an exception locally by finding a TRY or FINALLY region
   and jumping to its handler.  Call when PyErr_Occurred() is set.

   :param ctx: execution context
   :param out_ev: event to fill if no handler is found

   :return: 1 if a handler was applied (caller should continue the loop),
            0 if no handler (out_ev set to KOS_EV_RAISE, caller should return)
*/
static int try_local_exception_handler(KosContext *ctx, KosEvent *out_ev) {
  KosDynRegion *region = kos_find_region_for_raise(ctx);
  if (! region) {
    PyObject *typ = NULL, *val = NULL, *tb = NULL;
    PyErr_Fetch(&typ, &val, &tb);
    out_ev->kind = KOS_EV_RAISE;
    out_ev->payload = val;  /* exception instance (caller DECREFs) */
    Py_XDECREF(typ);
    Py_XDECREF(tb);
    ctx->flags &= ~KOS_CTX_RUNNING;
    ctx->flags |= KOS_CTX_DEAD;
    return 0;
  }

  uint32_t handler_pc = region->a_label;
  {
    KosTryHandler th;
    if (kos_try_handler_for((PyObject *) ctx->frame_top->code,
                            ctx->frame_top->pc, &th) == 0) {
      if (region->style == KOS_REGION_FINALLY && th.finally_pc != KOS_EXTENT_NONE) {
        handler_pc = th.finally_pc;
      }
      else if (th.except_pc != KOS_EXTENT_NONE) {
        handler_pc = th.except_pc;
      }
    }
  }

  ctx->frame_top->pc = handler_pc;
  region->enabled = 0;

  /* Set aside exception in excinfo and clear so handler runs without PyErr set */
  {
    PyObject *typ = NULL, *val = NULL, *tb = NULL;
    PyErr_Fetch(&typ, &val, &tb);
    PyErr_SetExcInfo(typ, val, tb);
    PyErr_Clear();
  }

  if (region->style == KOS_REGION_FINALLY)
    ctx->handling_raise = 1;

  return 1;
}


int koskari_eval(KosContext *ctx, KosEvent *out_ev) {
  int eval_ret = 0;

  if (! ctx) {
    PyErr_SetString(PyExc_RuntimeError, "NULL context for koskari_eval");
    return -1;
  }

  if (! out_ev) {
    PyErr_SetString(PyExc_RuntimeError, "NULL output event");
    return -1;
  }

  /* Mark context as running */
  ctx->flags |= KOS_CTX_RUNNING;

  /* Push token onto active stack and increment live_depth */
  push_active_token(ctx->token);
  KosEvaluatorToken_IncrementLiveDepth(ctx->token);

  /* Handle pending exception injection */
  if (ctx->pending_throw && ctx->pending_exc) {
    PyErr_SetObject((PyObject *) Py_TYPE(ctx->pending_exc), ctx->pending_exc);
    Py_CLEAR(ctx->pending_exc);
    ctx->pending_throw = 0;

    out_ev->kind = KOS_EV_RAISE;
    out_ev->payload = NULL;
    PyErr_Fetch(&out_ev->payload, NULL, NULL);

    ctx->flags &= ~KOS_CTX_RUNNING;
    ctx->flags |= KOS_CTX_DEAD;
    eval_ret = 0;
    goto koskari_eval_cleanup;
  }

  if (run_transfer_plan_to_completion(ctx, out_ev) < 0) {
    eval_ret = 0;
    goto koskari_eval_cleanup;
  }

  if (kos_hook_active(ctx)) {
    /* Minimal: no hook execution; return */
    out_ev->kind = KOS_EV_RETURN;
    out_ev->payload = Py_None;
    Py_INCREF(Py_None);
    ctx->flags &= ~KOS_CTX_RUNNING;
    ctx->flags |= KOS_CTX_DEAD;
    eval_ret = 0;
    goto koskari_eval_cleanup;
  }

  if (! ctx->frame_top) {
    out_ev->kind = KOS_EV_RETURN;
    out_ev->payload = Py_None;
    Py_INCREF(Py_None);
    ctx->flags &= ~KOS_CTX_RUNNING;
    ctx->flags |= KOS_CTX_DEAD;
    eval_ret = 0;
    goto koskari_eval_cleanup;
  }

  /* Re-entry: inject value from completed Python await */
  if (ctx->py_await_inject) {
    if (KosStackBlock_PushRef(&ctx->frame_top->stack, ctx->py_await_inject) < 0) {
      Py_DECREF(ctx->py_await_inject);
      ctx->py_await_inject = NULL;
      eval_ret = -1;
      goto koskari_eval_cleanup;
    }
    Py_CLEAR(ctx->py_await_inject);
  }

#if KOS_USE_COMPUTED_GOTO
  static const void *const optable[] = {
#define OP(name, style) &&TARGET_##name,
#include "opcodes.incl"
#undef OP
  };
#endif

  /* Opcode dispatch loop */
  for (;;) {
#if KOS_USE_COMPUTED_GOTO
    next_insn:
#endif
    if (ctx->plan && run_transfer_plan_to_completion(ctx, out_ev) < 0) {
      eval_ret = 0;
      goto koskari_eval_cleanup;
    }

    if (PyErr_Occurred()) {
      /* Check for KoskariContinuationApply before local exception handler */
      if (KoskariContinuationApply
          && PyErr_ExceptionMatches(KoskariContinuationApply)) {
        PyObject *typ = NULL, *val = NULL, *tb = NULL;
        PyErr_Fetch(&typ, &val, &tb);
        PyObject *exc_target = val
            ? PyObject_GetAttrString(val, "target") : NULL;
        if (exc_target) {
          int is_own = (exc_target == (PyObject *) ctx->token);
          Py_DECREF(exc_target);
          if (is_own) {
            /* Apply continuation: extract from exception and invoke */
            PyObject *cont_obj = val
                ? PyObject_GetAttrString(val, "continuation") : NULL;
            PyObject *args_obj = val
                ? PyObject_GetAttrString(val, "args") : NULL;
            Py_XDECREF(typ);
            Py_XDECREF(tb);
            if (cont_obj && PyObject_TypeCheck(cont_obj, &KosPyContinuationType)) {
              KosContinuation *k = ((KosPyContinuation *) cont_obj)->captured;
              PyObject *resume_val = Py_None;
              if (args_obj && PyTuple_Check(args_obj)
                  && PyTuple_GET_SIZE(args_obj) > 0) {
                resume_val = PyTuple_GET_ITEM(args_obj, 0);
                Py_INCREF(resume_val);
              }
              Py_XDECREF(args_obj);
              args_obj = NULL;
              PyErr_Clear();
              if (k && kos_invoke_continuation(ctx, k, resume_val, 0,
                                               cont_obj) == 0) {
                Py_XDECREF(resume_val);
                Py_DECREF(val);
                continue;
              }
              Py_XDECREF(resume_val);
              Py_DECREF(cont_obj);
            } else {
              Py_XDECREF(cont_obj);
              Py_XDECREF(args_obj);
            }
            Py_DECREF(val);
            PyErr_SetString(PyExc_RuntimeError,
                            "KoskariContinuationApply: invalid continuation");
            eval_ret = -1;
            goto koskari_eval_cleanup;
          }
        }
        PyErr_Restore(typ, val, tb);
        eval_ret = 0;
        goto koskari_eval_cleanup;
      }
      if (try_local_exception_handler(ctx, out_ev))
        continue;
      eval_ret = 0;
      goto koskari_eval_cleanup;
    }

    KosCode *code = ctx->frame_top->code;
    uint32_t pc = ctx->frame_top->pc;

    if (unlikely(pc >= code->code_len)) {
      SAYF("[eval] pc=%u OUT OF RANGE (code_len=%u)\n",
           (unsigned) pc, (unsigned) code->code_len);
      PyErr_SetString(PyExc_RuntimeError, "pc out of range");
      eval_ret = -1;
      goto koskari_eval_cleanup;
    }

    KosOp *insn = &code->code[pc];
    SAYF("[eval] pc=%u op=%u arg=%u\n",
         (unsigned) pc, (unsigned) insn->op, (unsigned) insn->arg);
    trace_stack("  before", ctx->frame_top->stack);
    ctx->frame_top->pc = pc + 1;

    if (unlikely((KosOpCode) insn->op > KOS_OP_THEEND)) {
      PyErr_Format(PyExc_RuntimeError, "invalid opcode %u",
                   (unsigned) insn->op);
      eval_ret = -1;
      goto koskari_eval_cleanup;
    }
    {
      int op_res;
#if KOS_USE_COMPUTED_GOTO
      goto *optable[insn->op];
#define OP(name, style) TARGET_##name: op_res = op_##name(ctx, out_ev, insn); if (op_res != 0) goto handle_op_result; goto next_insn;
#include "opcodes.incl"
#undef OP
      handle_op_result:
#else
      op_res = op_handlers[insn->op](ctx, out_ev, insn);
#endif
      if (unlikely(op_res != 0)) {
        if (PyErr_Occurred()) {
          if (try_local_exception_handler(ctx, out_ev))
            continue;
          eval_ret = 0;
          goto koskari_eval_cleanup;
        }
        eval_ret = (op_res < 0) ? -1 : 0;
        goto koskari_eval_cleanup;
      }
    }
    trace_stack("  after ", ctx->frame_top->stack);
  }

koskari_eval_cleanup:
  KosEvaluatorToken_DecrementLiveDepth(ctx->token);
  pop_active_token(ctx->token);
  return eval_ret;
}


/* === Unit lifecycle === */


/**
   Create a Koskari exception type and add it to the module. On success,
   the module owns the reference (PyModule_AddObject steals).
*/
static int register_koskari_exception(PyObject *mod,
                                       const char *full_name,
                                       const char *attr_name,
                                       PyObject **out_exc) {
  PyObject *base = PyTuple_Pack(1, PyExc_BaseException);
  if (! base)
    return -1;
  *out_exc = PyErr_NewException(full_name, base, NULL);
  Py_DECREF(base);
  if (! *out_exc)
    return -1;
  return PyModule_AddObject(mod, attr_name, *out_exc);
}


int koskari_eval_init(PyObject *mod) {

  PyObject *dict = PyModule_GetDict(mod);
  if (! dict)
    return -1;

  /* Initialize string constants */
  STR_CONST(str_await, "__await__");
  STR_CONST(str_send, "send");
  STR_CONST(str_throw, "throw");
  STR_CONST(str_close, "close");

  /* Initialize KosEvaluatorToken type */
  if (PyType_Ready(&KosEvaluatorTokenType) < 0)
    return -1;
  Py_INCREF(&KosEvaluatorTokenType);
  if (PyModule_AddObject(mod, "EvaluatorToken",
                         (PyObject *) &KosEvaluatorTokenType) < 0) {
    Py_DECREF(&KosEvaluatorTokenType);
    return -1;
  }

  /* Initialize exception types (subclass BaseException) */
  if (register_koskari_exception(mod,
                                 "koskari.lib.KoskariContinuationDead",
                                 "KoskariContinuationDead",
                                 &KoskariContinuationDead) < 0)
    return -1;
  if (register_koskari_exception(mod,
                                 "koskari.lib.KoskariContinuationApply",
                                 "KoskariContinuationApply",
                                 &KoskariContinuationApply) < 0)
    return -1;
  if (register_koskari_exception(mod,
                                 "koskari.lib.KoskariContinuationError",
                                 "KoskariContinuationError",
                                 &KoskariContinuationError) < 0)
    return -1;

  /* Initialize dynchain module */
  if (koskari_dynchain_init(mod))
    return -1;

  return 0;
}


int koskari_eval_clear(PyObject *mod) {

  Py_CLEAR(str_await);
  Py_CLEAR(str_send);
  Py_CLEAR(str_throw);
  Py_CLEAR(str_close);

  /* Null out exception type pointers (module dict owns the refs) */
  KoskariContinuationDead = NULL;
  KoskariContinuationApply = NULL;
  KoskariContinuationError = NULL;

  koskari_dynchain_clear(mod);

  return 0;
}


// The end.
