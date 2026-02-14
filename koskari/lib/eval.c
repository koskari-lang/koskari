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

   5. In-loop flattening: Koskari-to-Koskari CALL and async gen stepping
      run in the same koskari_eval loop. When the current frame has a
      parent, YIELD and RETURN detach and push value onto the parent
      (no boundary event); only the top-level frame produces KOS_EV_*
      on yield/return/raise/await.

   :author: Christopher O'Brien <obriencj@preoccupied.net>
   :license: GNU General Public License v3
   :ai-assistant: Claude Opus 4.5 via Cursor
*/


#include "lib.h"
#include "debug.h"

#include <stdint.h>

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
  while (KosContext_PLAN(ctx)) {
    SAYF("[eval] transfer step %zu/%zu\n",
         (size_t) KosContext_PLAN(ctx)->action_index,
         (size_t) KosContext_PLAN(ctx)->action_count);
    int plan_res = kos_run_transfer_step(ctx);
    if (plan_res < 0) {
      SAY("[eval] transfer step failed -> KOS_EV_RAISE\n");
      out_ev->kind = KOS_EV_RAISE;
      out_ev->payload = NULL;
      PyErr_Fetch(&out_ev->payload, NULL, NULL);
      KosContext_FLAGS(ctx) &= ~KOS_CTX_RUNNING;
      KosContext_FLAGS(ctx) |= KOS_CTX_DEAD;
      return -1;
    }
    if (plan_res == 0)
      continue;
    SAYF("[eval] transfer complete inv_seg_top=%p\n",
         (void *) KosContext_DYN_TOP(ctx));
    KosContext_INVOCATION_SEGMENT_TOP(ctx) = KosContext_DYN_TOP(ctx);
    /* Clear plan before freeing so any re-entrant eval (e.g. from
     * Py_XDECREF(continuation_ref) triggering Python dealloc/__del__)
     * does not see the plan and touch it while it is being freed. */
    {
      KosTransferPlan *p = KosContext_PLAN(ctx);
      KosContext_PLAN(ctx) = NULL;
      KosContext_FLAGS(ctx) &= ~KOS_CTX_IN_TRANSFER;
      kos_free_transfer_plan(p);
    }
    if (KosContext_PLAN_STACK(ctx))
      kos_resume_transfer_plan(ctx);
  }
  return 0;
}


/**
   Unwind pending async gen on error. If ctx has a pending async gen (NEXT or
   ITER_NEXT), and the top frame is the gen frame, detach it, close the gen,
   and clear state. Call when propagating an exception so the gen is properly
   closed.
*/
static void clear_pending_agen_on_error(KosContext *ctx) {
  if (! KosContext_FRAME_TOP(ctx) || KosDynNode_PARENT(KosContext_FRAME_TOP(ctx)) == NULL)
    return;
  if (! KosContext_PENDING_AGEN(ctx))
    return;
  KosPyAsyncGen *agen = (KosPyAsyncGen *) KosContext_PENDING_AGEN(ctx);
  KosDynFrame *detached_frame = NULL;
  if (kos_dyn_detach_top_frame(ctx, &detached_frame) == 0 && detached_frame) {
    agen->closed = 1;
    agen->in_progress = 0;
  }
  KosContext_PENDING_AGEN(ctx) = NULL;
  Py_XDECREF(KosContext_PENDING_AGEN_DEFAULT(ctx));
  KosContext_PENDING_AGEN_DEFAULT(ctx) = NULL;
}


/**
   Handle KoskariContinuationApply exception. Call when PyErr_ExceptionMatches
   (KoskariContinuationApply). Fetches the exception, extracts target/continuation
   from the exception value, and invokes if same-token.

   :return: 1 to continue the eval loop; -1 to goto koskari_eval_cleanup
*/
static int handle_continuation_apply(KosContext *ctx) {
  PyObject *typ = NULL, *val = NULL, *tb = NULL;
  PyErr_Fetch(&typ, &val, &tb);
  PyObject *exc_target = val
      ? PyObject_GetAttrString(val, "target") : NULL;
  if (exc_target) {
    int is_own = (exc_target == (PyObject *) KosContext_TOKEN(ctx));
    Py_DECREF(exc_target);
    if (is_own) {
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
          return 1;
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
      return -1;
    }
  }
  PyErr_Restore(typ, val, tb);
  return -1;
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

  Py_VISIT(KosDynFrame_NS(frame));

  if (KosDynFrame_STACK(frame)) {
    KosStackBlock *b = KosDynFrame_STACK(frame);
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

#define CONTEXT_MAX_FREE 16  /* cap for cached KosContext; tunable */

static size_t context_freecount = 0;
static KosContext *context_freelist = NULL;


KosContext *kos_context_new(KosDynFrame *top) {

  KosContext *ctx = context_freelist;
  if (ctx) {
    context_freelist = KosContext_FREELIST_NEXT(ctx);
    context_freecount--;
    memset(ctx, 0, sizeof(KosContext));
  } else {
    ctx = PyMem_Malloc(sizeof(KosContext));
    if (! ctx) {
      PyErr_NoMemory();
      return NULL;
    }
    memset(ctx, 0, sizeof(KosContext));
  }

  KosContext_TOKEN(ctx) = KosEvaluatorToken_New();
  if (! KosContext_TOKEN(ctx)) {
    if (context_freecount < CONTEXT_MAX_FREE) {
      memset(ctx, 0, sizeof(KosContext));
      KosContext_FREELIST_NEXT(ctx) = context_freelist;
      context_freelist = ctx;
      context_freecount++;
    } else {
      PyMem_Free(ctx);
    }
    return NULL;
  }

  KosContext_DYN_TOP(ctx) = (KosDynNode *) top;
  KosContext_FRAME_TOP(ctx) = top;
  KosContext_GLOBALS_FRAME(ctx) = top;
  KosContext_GLOBALS_CACHE_VERSION(ctx) = 0;

  /* Initialize hook runner */
  KosContext_HOOK_RUNNER(ctx)->code = NULL;
  KosContext_HOOK_RUNNER(ctx)->pc = 0;
  KosContext_HOOK_RUNNER(ctx)->scratch = NULL;
  KosContext_HOOK_RUNNER(ctx)->active = 0;

  /* Initialize transfer plan state */
  KosContext_PLAN(ctx) = NULL;
  KosContext_PLAN_STACK(ctx) = NULL;
  KosContext_CHAIN_VERSION(ctx) = 0;
  KosContext_INVOCATION_SEGMENT_TOP(ctx) = NULL;

  /* Initialize Python await bridging */
  KosContext_PY_AWAIT_ITER(ctx) = NULL;
  KosContext_PY_LAST_YIELDED(ctx) = NULL;
  KosContext_PY_RESUME_INJECT(ctx) = NULL;
  KosContext_PY_RESUME_KIND(ctx) = KOS_RESUME_NONE;

  /* Initialize exception injection */
  KosContext_INJECT_MODE(ctx) = KOS_INJECT_NONE;
  KosContext_INJECT_VALUE(ctx) = NULL;

  /* Pending async gen (NEXT and ITER_NEXT) */
  KosContext_PENDING_AGEN(ctx) = NULL;
  KosContext_PENDING_AGEN_DEFAULT(ctx) = NULL;
  KosContext_PENDING_AGEN_N(ctx) = 0;

  KosContext_FLAGS(ctx) = 0;

  return ctx;
}


void kos_context_free(KosContext *ctx) {

  if (! ctx)
    return;

  Py_XDECREF(KosContext_TOKEN(ctx));
  KosContext_TOKEN(ctx) = NULL;

  /* Free the dynamic chain */
  kos_dynchain_free(KosContext_DYN_TOP(ctx), NULL);

  /* Free any active transfer plan */
  kos_free_transfer_plan(KosContext_PLAN(ctx));
  while (KosContext_PLAN_STACK(ctx)) {
    KosTransferPlan *p = KosContext_PLAN_STACK(ctx);
    KosContext_PLAN_STACK(ctx) = p->suspended;
    kos_free_transfer_plan(p);
  }

  /* Free hook runner scratch stack */
  if (KosContext_HOOK_RUNNER(ctx)->scratch) {
    KosStackBlock *b = KosContext_HOOK_RUNNER(ctx)->scratch;
    while (b) {
      KosStackBlock *p = KosStackBlock_PARENT(b);
      KosStackBlock_Release(b);
      b = p;
    }
  }

  Py_XDECREF(KosContext_PY_AWAIT_ITER(ctx));
  Py_XDECREF(KosContext_PY_LAST_YIELDED(ctx));
  Py_XDECREF(KosContext_PY_RESUME_INJECT(ctx));
  Py_XDECREF(KosContext_INJECT_VALUE(ctx));
  Py_XDECREF(KosContext_PENDING_AGEN_DEFAULT(ctx));

  if (context_freecount < CONTEXT_MAX_FREE) {
    memset(ctx, 0, sizeof(KosContext));
    KosContext_FREELIST_NEXT(ctx) = context_freelist;
    context_freelist = ctx;
    context_freecount++;
  } else {
    PyMem_Free(ctx);
  }
}


int kos_context_traverse(
    KosContext *ctx,
    visitproc visit,
    void *arg) {

  if (! ctx)
    return 0;

  Py_VISIT(KosContext_TOKEN(ctx));
  Py_VISIT(KosContext_PY_AWAIT_ITER(ctx));
  Py_VISIT(KosContext_PY_LAST_YIELDED(ctx));
  Py_VISIT(KosContext_PY_RESUME_INJECT(ctx));
  Py_VISIT(KosContext_INJECT_VALUE(ctx));
  Py_VISIT(KosContext_PENDING_AGEN_DEFAULT(ctx));

  /* Traverse the dynamic chain */
  int res = kos_dynchain_traverse(KosContext_DYN_TOP(ctx), NULL, visit, arg);
  if (res)
    return res;

  /* Traverse transfer plan */
  res = kos_transferplan_traverse(KosContext_PLAN(ctx), visit, arg);
  if (res)
    return res;

  /* Traverse hook runner */
  res = kos_hookrunner_traverse(KosContext_HOOK_RUNNER(ctx), visit, arg);
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

  if (! ctx || ! KosContext_FRAME_TOP(ctx)) {
    PyErr_SetString(PyExc_RuntimeError, "no frame for tail_koscall");
    return -1;
  }

  KosDynFrame *old = KosContext_FRAME_TOP(ctx);
  KosDynNode *parent = KosDynNode_PARENT(old);

  /* Pop the old frame */
  KosContext_DYN_TOP(ctx) = parent;

  /* Create new frame with same parent */
  KosDynFrame *frame = kos_dynframe_new(parent, code, ns, NULL);
  if (! frame) {
    /* Restore old frame on failure */
    KosContext_DYN_TOP(ctx) = (KosDynNode *) old;
    return -1;
  }

  /* Free old frame */
  kos_dynframe_free(old);

  /* Install new frame */
  KosContext_DYN_TOP(ctx) = (KosDynNode *) frame;
  KosContext_FRAME_TOP(ctx) = frame;
  KosContext_CHAIN_VERSION(ctx)++;

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
    Py_XDECREF(KosContext_PY_AWAIT_ITER(ctx));
    KosContext_PY_AWAIT_ITER(ctx) = await_iter;

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
   Get the awaitable for one step of an async iterator (__anext__).
   Mirror of sibilant-ceval get_await_next: tp_as_async->am_anext then
   PyObject_GetAwaitable. Caller must Py_DECREF the result.

   :return: new reference to awaitable, or NULL on error or if not async
*/
static PyObject *get_await_next(PyObject *val) {
  PyTypeObject *type = Py_TYPE(val);
  unaryfunc anext = NULL;
  if (type->tp_as_async)
    anext = type->tp_as_async->am_anext;
  if (! anext)
    return NULL;
  PyObject *thing = anext(val);
  if (! thing)
    return NULL;
  PyObject *awaitable = PyObject_GetAwaitable(thing);
  Py_DECREF(thing);
  return awaitable;
}


/**
   Push val onto the frame stack. No auto-await; val is used as-is.
   val is consumed (new reference). Use this for all values that are not
   the direct result of a call (e.g. CONST, load, getattr, subscript).

   :return: 0 continue, -1 error
*/
static int push_result(KosContext *ctx, PyObject *val) {

  if (! val)
    return -1;
  if (KosStackBlock_PushRef(&KosDynFrame_STACK(KosContext_FRAME_TOP(ctx)), val) < 0) {
    Py_DECREF(val);
    return -1;
  }
  Py_DECREF(val);
  return 0;
}


/**
   Push val, or if val is awaitable run eager_step and either push the
   result or suspend (KOS_EV_AWAIT). Use only for (1) the result of a
   call (CALL opcode: callable returned a coroutine) and (2) the AWAIT
   opcode (explicitly awaiting TOS). All other paths must use push_result
   so we do not auto-await values that are merely loaded or computed.

   val is consumed (new reference).

   :return: 0 continue, 1 stop (AWAIT), -1 error
*/
static int push_or_await(KosContext *ctx, KosEvent *out_ev, PyObject *val) {

  PyTypeObject *type = Py_TYPE(val);
  SAYF("[eval] push_or_await entry is_awaitable=%d\n",
       (type->tp_as_async && type->tp_as_async->am_await) ? 1 : 0);
  if (! type->tp_as_async || ! type->tp_as_async->am_await) {
    if (KosStackBlock_PushRef(&KosDynFrame_STACK(KosContext_FRAME_TOP(ctx)), val) < 0) {
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
    if (KosStackBlock_PushRef(&KosDynFrame_STACK(KosContext_FRAME_TOP(ctx)), result) < 0) {
      Py_DECREF(result);
      return -1;
    }
    Py_DECREF(result);
    return 0;
  }

  if (suspended && yielded) {
    KosContext_FLAGS(ctx) &= ~KOS_CTX_RUNNING;
    out_ev->kind = KOS_EV_AWAIT;
    out_ev->payload = yielded;
    SAYF("[eval] push_or_await set KOS_EV_AWAIT (suspended=%d yielded=%d)\n",
         suspended, yielded ? 1 : 0);
    return 1;
  }

  return -1;
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
  PyObject *ns = KosDynFrame_NS(KosContext_FRAME_TOP(ctx));
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
  PyObject *ns = KosDynFrame_NS(KosContext_FRAME_TOP(ctx));
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
  PyObject *ns = KosDynFrame_NS(KosContext_FRAME_TOP(ctx));
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
  PyObject *ns = KosDynFrame_NS(KosContext_FRAME_TOP(ctx));
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
  PyObject *ns = KosDynFrame_NS(KosContext_FRAME_TOP(ctx));
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
  PyObject *val = KosCode_GetConstant((PyObject *) KosDynFrame_CODE(KosContext_FRAME_TOP(ctx)),
                                      insn->arg);
  if (! val) {
    SAYF("[eval] CONST GetConstant failed arg=%u\n",
         (unsigned) insn->arg);
    return -1;
  }
  if (PyLong_Check(val))
    SAYF("[eval] CONST pushed %ld\n", PyLong_AsLong(val));
  Py_INCREF(val);
  return push_result(ctx, val);
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

  if (KosStackBlock_Duplicate(&KosDynFrame_STACK(KosContext_FRAME_TOP(ctx)), (size_t) opa) < 0)
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
  if (KosStackBlock_Drop(&KosDynFrame_STACK(KosContext_FRAME_TOP(ctx)), (size_t) n) < 0)
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
    val = KosStackBlock_PopRef(&KosDynFrame_STACK(KosContext_FRAME_TOP(ctx)));
    if (! val) {
      SAY("[eval] RETURN pop underrun\n");
      return -1;
    }

  } else {
    val = KosStackBlock_PopTail(&KosDynFrame_STACK(KosContext_FRAME_TOP(ctx)), (size_t) opa);
    if (! val) {
      SAY("[eval] RETURN PopTail underrun\n");
      return -1;
    }
  }

  /* Frame has parent: return into parent in same loop. */
  if (KosDynNode_PARENT(KosContext_FRAME_TOP(ctx)) != NULL) {
    int was_pending_ctx = (KosContext_PENDING_AGEN(ctx) != NULL);
    KosDynFrame *detached_frame = NULL;
    if (kos_dyn_detach_top_frame(ctx, &detached_frame) < 0) {
      Py_DECREF(val);
      return -1;
    }
    KosPyAsyncGen *agen = was_pending_ctx ? (KosPyAsyncGen *) KosContext_PENDING_AGEN(ctx) : NULL;

    if (was_pending_ctx && agen) {
      agen->closed = 1;
      agen->in_progress = 0;
      if (KosContext_PENDING_AGEN_N(ctx) == 0) {
        /* ITER_NEXT case: exhausted; jump to else (region and else_pc on region) */
        KosDynRegion *r = kos_find_region_for_loop(ctx);
        if (r) {
          KosDynRegion_EXHAUSTED(r) = 1;
          KosDynFrame_PC(KosContext_FRAME_TOP(ctx)) = KosDynRegion_ITER_NEXT_ELSE_PC(r);
        }
        KosContext_PENDING_AGEN(ctx) = NULL;
        KosContext_PENDING_AGEN_DEFAULT(ctx) = NULL;
        Py_DECREF(val);
        return 0;
      }
      if (KosContext_PENDING_AGEN_N(ctx) == 2 && KosContext_PENDING_AGEN_DEFAULT(ctx)) {
        if (KosStackBlock_PushRef(&KosDynFrame_STACK(KosContext_FRAME_TOP(ctx)),
                                  KosContext_PENDING_AGEN_DEFAULT(ctx)) < 0) {
          Py_XDECREF(KosContext_PENDING_AGEN_DEFAULT(ctx));
          KosContext_PENDING_AGEN_DEFAULT(ctx) = NULL;
          KosContext_PENDING_AGEN(ctx) = NULL;
          Py_DECREF(val);
          return -1;
        }
        Py_XDECREF(KosContext_PENDING_AGEN_DEFAULT(ctx));
        Py_DECREF(val);
      } else if (KosContext_PENDING_AGEN_N(ctx) == 1) {
        PyErr_SetNone(PyExc_StopIteration);
        Py_DECREF(val);
        KosContext_PENDING_AGEN(ctx) = NULL;
        KosContext_PENDING_AGEN_DEFAULT(ctx) = NULL;
        return -1;
      } else {
        Py_DECREF(val);
      }
      KosContext_PENDING_AGEN(ctx) = NULL;
      KosContext_PENDING_AGEN_DEFAULT(ctx) = NULL;
      return 0;
    }
    /* Normal CALL return: push value onto parent. */
    if (KosStackBlock_PushRef(&KosDynFrame_STACK(KosContext_FRAME_TOP(ctx)), val) < 0) {
      Py_DECREF(val);
      return -1;
    }
    Py_DECREF(val);
    return 0;
  }

  SAYF("[eval] RETURN count=%u -> KOS_EV_RETURN\n", opa);
  SAYF("[eval] RETURN payload: %R\n", val);
  out_ev->kind = KOS_EV_RETURN;
  out_ev->payload = val;
  KosContext_FLAGS(ctx) &= ~KOS_CTX_RUNNING;
  KosContext_FLAGS(ctx) |= KOS_CTX_DEAD;
  return 1;
}


/**
   AWAIT: arg unused. Pop TOS (awaitable), start/resume awaiting it; may
   suspend with KOS_EV_AWAIT or push the result.
*/
KOS_OP_HANDLER(AWAIT) {
  (void) insn;
  PyObject *val = KosStackBlock_PopRef(&KosDynFrame_STACK(KosContext_FRAME_TOP(ctx)));
  if (! val)
    return -1;
  return push_or_await(ctx, out_ev, val);
}


/*
   CALL strategy: PopTail(n) and PopRef() give the handler owned callable and
   args (tuple or NULL). Handler passes them to one helper as borrowed refs;
   helpers do not take ownership and must not DECREF them. The handler does
   Py_DECREF(callable) and Py_XDECREF(args) once after dispatch returns.
*/

/**
   call_call_continuation: callable and args are borrowed. First element of
   args is resume_val for the continuation (we INCREF it; we own that ref only).
   Plan INCREFs callable on same-evaluator path; handler releases callable and args.
*/
static int call_call_continuation(
    KosContext *ctx,
    PyObject *callable,
    PyObject *args,
    KosEvent *out_ev,
    KosOp *insn) {

  KosContinuation *k = KosPyDelimitedContinuation_Check(callable)
      ? ((KosPyDelimitedContinuation *) callable)->captured
      : ((KosPyContinuation *) callable)->captured;

  Py_ssize_t n = args ? PyTuple_GET_SIZE(args) : 0;
  PyObject *resume_val = (n > 0) ? PyTuple_GET_ITEM(args, 0) : Py_None;
  Py_INCREF(resume_val);

  /* Defensive: wrappers are only created with non-NULL k at capture time. */
  if (! k) {
    PyErr_SetString(PyExc_RuntimeError,
                    "continuation has no captured state");
    Py_DECREF(resume_val);
    return -1;
  }

  if (k->prompt_id != KOS_PROMPT_ID_UNDELIMITED
      && ! kos_find_prompt(ctx, k->prompt_id)) {
    Py_DECREF(resume_val);
    PyErr_SetString(KoskariContinuationError,
                    "continuation: prompt not found in dynamic chain");
    return -1;
  }

  if (k->token_ref == (PyObject *) KosContext_TOKEN(ctx)
      || (k->prompt_id == KOS_PROMPT_ID_UNDELIMITED && ! k->token_ref)) {
    if (kos_invoke_continuation(ctx, k, resume_val, 0, callable) < 0) {
      Py_DECREF(resume_val);
      return -1;
    }
    Py_DECREF(resume_val);
    (void) out_ev;
    (void) insn;
    return 0;
  }

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


/**
   call_native_flattened_expr: callable is borrowed (KosExpression, n is 0).
   Push a new frame for that expression's code/ns. Handler releases callable.
*/
static int call_native_flattened_expr(
    KosContext *ctx,
    PyObject *callable,
    KosEvent *out_ev) {

  PyObject *code = KosExpression_GetCode(callable);
  PyObject *ns = KosExpression_GetNs(callable);
  Py_INCREF(ns);
  if (kos_dyn_push_frame(ctx, (KosCode *) code, ns) == NULL) {
    Py_DECREF(ns);
    return -1;
  }
  (void) out_ev;
  return 0;
}


/**
   call_native_flattened_function: callable and args are borrowed. Pass args
   to PrepareCallNamespace, push frame. Handler releases callable and args.
*/
static int call_native_flattened_function(
    KosContext *ctx,
    PyObject *callable,
    PyObject *args,
    KosEvent *out_ev) {

  PyObject *frame_ns = KosFunction_PrepareCallNamespace(
      callable, args, NULL);
  if (! frame_ns)
    return -1;
  PyObject *fn_code = KosFunction_GetCode(callable);
  if (kos_dyn_push_frame(ctx, (KosCode *) fn_code, frame_ns) == NULL) {
    Py_DECREF(frame_ns);
    return -1;
  }
  (void) out_ev;
  return 0;
}


/**
   call_pyobject_call: callable and args are borrowed. CallNoArgs or Call(tuple).
   Push the result; if it is a coroutine (call returned awaitable), we await it
   here (push_or_await). Handler releases callable and args.
*/
static int call_pyobject_call(
    KosContext *ctx,
    PyObject *callable,
    PyObject *args,
    KosEvent *out_ev) {

  PyObject *result = (! args || PyTuple_GET_SIZE(args) == 0)
      ? PyObject_CallNoArgs(callable)
      : PyObject_Call(callable, args, NULL);
  if (! result)
    return -1;
  return push_or_await(ctx, out_ev, result);
}


/**
   CALL: arg = number of positional arguments N. Stack has [callable, arg_0,
   ..., arg_{N-1}] (callable oldest). Call callable with those args, drop
   them, then push result or await it.
*/
KOS_OP_HANDLER(CALL) {
  unsigned n = insn->arg;

  KosDynFrame *frame_top = KosContext_FRAME_TOP(ctx);

  PyObject *args = NULL;
  if (n > 0) {
    args = KosStackBlock_PopTail(&KosDynFrame_STACK(frame_top), n);
    if (! args)
      return -1;
  }

  PyObject *callable = KosStackBlock_PopRef(&KosDynFrame_STACK(frame_top));
  if (! callable)
    return -1;

  int r;
  if (KosPyDelimitedContinuation_Check(callable)
      || KosPyContinuation_Check(callable)) {
    r = call_call_continuation(ctx, callable, args, out_ev, insn);

  } else if (Py_TYPE(callable) == &KosExpressionType) {
    if (n > 0) {
      PyErr_SetString(PyExc_TypeError, "Expression takes no arguments");
      r = -1;
    } else {
      r = call_native_flattened_expr(ctx, callable, out_ev);
    }

  } else if (Py_TYPE(callable) == &KosFunctionType) {
    r = call_native_flattened_function(ctx, callable, args, out_ev);

  } else {
    r = call_pyobject_call(ctx, callable, args, out_ev);
  }

  Py_DECREF(callable);
  Py_XDECREF(args);
  return r;
}


/**
   GLOBALS: arg unused. Push the namespace of the outermost frame in the
   dynamic chain (globals) onto the stack.
*/
KOS_OP_HANDLER(GLOBALS) {
  if (KosContext_CHAIN_VERSION(ctx) != KosContext_GLOBALS_CACHE_VERSION(ctx)) {
    KosDynNode *n = (KosDynNode *) KosContext_FRAME_TOP(ctx);
    KosDynFrame *outer = KosContext_FRAME_TOP(ctx);
    while (n && n->parent) {
      n = n->parent;
      if (KosDynNode_IS_FRAME(n))
        outer = (KosDynFrame *) n;
    }
    KosContext_GLOBALS_FRAME(ctx) = outer;
    KosContext_GLOBALS_CACHE_VERSION(ctx) = KosContext_CHAIN_VERSION(ctx);
  }

  PyObject *ns = KosDynFrame_NS(KosContext_GLOBALS_FRAME(ctx));
  Py_INCREF(ns);
  return push_result(ctx, ns);
}


/**
   LOCALS: arg unused. Push the current frame's namespace (locals) onto
   the stack.
*/
KOS_OP_HANDLER(LOCALS) {
  PyObject *ns = KosDynFrame_NS(KosContext_FRAME_TOP(ctx));
  Py_INCREF(ns);
  return push_result(ctx, ns);
}


/**
   VAR: arg = constant index of a name. Look up that name in the current
   frame's namespace and push the value (or await it if async).
*/
KOS_OP_HANDLER(VAR) {
  PyObject *name = get_name_from_insn(KosDynFrame_CODE(KosContext_FRAME_TOP(ctx)), insn);
  if (! name)
    return -1;
  PyObject *val = frame_ns_deref(ctx, name);
  if (! val)
    return -1;
  return push_result(ctx, val);
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
    return push_result(ctx, Py_None);
  }

  if (! val)
    PyErr_NormalizeException(&typ, &val, &tb);

  Py_DECREF(typ);
  Py_XDECREF(tb);

  return push_result(ctx, val);
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
    if (KosStackBlock_Drop(&KosDynFrame_STACK(KosContext_FRAME_TOP(ctx)), (size_t) opa) < 0)
      return -1;
    Py_INCREF(ret);
    return push_result(ctx, ret);
  }

  PyObject *const *checks = KosStackBlock_TailPtr(KosDynFrame_STACK(KosContext_FRAME_TOP(ctx)),
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

  KosStackBlock_DropNoErr(&KosDynFrame_STACK(KosContext_FRAME_TOP(ctx)), (size_t) opa);

  Py_DECREF(typ);
  Py_XDECREF(val);
  Py_XDECREF(tb);

  Py_INCREF(ret);
  return push_result(ctx, ret);
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
    PyObject *exc = KosStackBlock_PopRef(&KosDynFrame_STACK(KosContext_FRAME_TOP(ctx)));
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
  PyObject *match = KosStackBlock_PopRef(&KosDynFrame_STACK(KosContext_FRAME_TOP(ctx)));
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
  KosDynFrame_PC(KosContext_FRAME_TOP(ctx)) = insn->arg;
  (void) out_ev;
  return 0;
}


/**
   PUSH_PROMPT: arg = constant index of prompt_id. Push a delimited prompt
   onto the dynamic chain for shift/reset.
*/
KOS_OP_HANDLER(PUSH_PROMPT) {
  uint32_t prompt_id;
  if (get_prompt_id_from_insn(KosDynFrame_CODE(KosContext_FRAME_TOP(ctx)), insn, &prompt_id) < 0)
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
  if (KosContext_DYN_TOP(ctx) && KosDynNode_IS_PROMPT(KosContext_DYN_TOP(ctx))) {
    KosDynPrompt *p = (KosDynPrompt *) KosContext_DYN_TOP(ctx);
    KosDynPrompt_HANDLER_CODE(p) = KosDynFrame_CODE(KosContext_FRAME_TOP(ctx));
    KosDynPrompt_HANDLER_PC(p) = insn->arg;
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
       (void *) KosContext_INVOCATION_SEGMENT_TOP(ctx), (void *) KosContext_DYN_TOP(ctx),
       (KosContext_DYN_TOP(ctx) && KosDynNode_IS_PROMPT(KosContext_DYN_TOP(ctx))) ? 1 : 0);

  if (KosContext_INVOCATION_SEGMENT_TOP(ctx) != NULL) {
    PyObject *val = KosStackBlock_PopRef(&KosDynFrame_STACK(KosContext_FRAME_TOP(ctx)));
    if (! val) {
      SAY("[eval] POP_PROMPT (invoke) pop underrun\n");
      return -1;
    }
    if (PyLong_Check(val))
      SAYF("[eval] POP_PROMPT return to invoker value=%ld\n",
           PyLong_AsLong(val));
    out_ev->kind = KOS_EV_RETURN;
    out_ev->payload = val;
    KosContext_INVOCATION_SEGMENT_TOP(ctx) = NULL;
    KosContext_FLAGS(ctx) &= ~KOS_CTX_RUNNING;
    (void) insn;
    return 1;
  }

  if (! KosContext_DYN_TOP(ctx) || ! KosDynNode_IS_PROMPT(KosContext_DYN_TOP(ctx))) {
    PyErr_SetString(PyExc_RuntimeError, "POP_PROMPT: top is not a prompt");
    return -1;
  }
  kos_dyn_pop_prompt(ctx, (KosDynPrompt *) KosContext_DYN_TOP(ctx));
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
  if (! KosContext_DYN_TOP(ctx) || ! KosDynNode_IS_REGION(KosContext_DYN_TOP(ctx))) {
    PyErr_SetString(PyExc_RuntimeError, "POP_TRY: top is not a region");
    return -1;
  }
  KosDynRegion *region = (KosDynRegion *) KosContext_DYN_TOP(ctx);
  if (KosDynRegion_STYLE(region) != KOS_REGION_TRY) {
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
  if (! KosContext_DYN_TOP(ctx) || ! KosDynNode_IS_REGION(KosContext_DYN_TOP(ctx))) {
    PyErr_SetString(PyExc_RuntimeError, "POP_FINALLY: top is not a region");
    return -1;
  }
  KosDynRegion *region = (KosDynRegion *) KosContext_DYN_TOP(ctx);
  if (KosDynRegion_STYLE(region) != KOS_REGION_FINALLY) {
    PyErr_SetString(PyExc_RuntimeError,
                    "POP_FINALLY: top is not a FINALLY region");
    return -1;
  }

  if (KosDynRegion_HANDLING_RAISE(region)) {
    PyObject *typ = NULL, *val = NULL, *tb = NULL;
    PyErr_GetExcInfo(&typ, &val, &tb);
    PyErr_Restore(typ, val, tb);
    PyErr_SetExcInfo(NULL, NULL, NULL);
    KosDynRegion_HANDLING_RAISE(region) = 0;
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
  if (! KosContext_DYN_TOP(ctx) || ! KosDynNode_IS_REGION(KosContext_DYN_TOP(ctx))) {
    PyErr_SetString(PyExc_RuntimeError, "CLEAR_TRY: top is not a region");
    return -1;
  }
  KosDynRegion *region = (KosDynRegion *) KosContext_DYN_TOP(ctx);
  if (KosDynRegion_STYLE(region) != KOS_REGION_TRY) {
    PyErr_SetString(PyExc_RuntimeError, "CLEAR_TRY: top is not a TRY region");
    return -1;
  }
  KosDynRegion_ENABLED(region) = 0;
  (void) out_ev;
  (void) insn;
  return 0;
}


/**
   CLEAR_FINALLY: arg unused. Disable the top FINALLY region.
*/
KOS_OP_HANDLER(CLEAR_FINALLY) {
  if (! KosContext_DYN_TOP(ctx) || ! KosDynNode_IS_REGION(KosContext_DYN_TOP(ctx))) {
    PyErr_SetString(PyExc_RuntimeError, "CLEAR_FINALLY: top is not a region");
    return -1;
  }
  KosDynRegion *region = (KosDynRegion *) KosContext_DYN_TOP(ctx);
  if (KosDynRegion_STYLE(region) != KOS_REGION_FINALLY) {
    PyErr_SetString(PyExc_RuntimeError,
                    "CLEAR_FINALLY: top is not a FINALLY region");
    return -1;
  }
  KosDynRegion_ENABLED(region) = 0;
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
  if (get_prompt_id_from_insn(KosDynFrame_CODE(KosContext_FRAME_TOP(ctx)), insn, &prompt_id) < 0)
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
       (unsigned) KosDynPrompt_HANDLER_PC(prompt));
  while (KosContext_DYN_TOP(ctx) != (KosDynNode *) prompt) {
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
  if (! KosDynPrompt_HANDLER_CODE(prompt)) {
    Py_DECREF(wrapper);
    PyErr_SetString(PyExc_RuntimeError, "shift: prompt has no handler");
    return -1;
  }

  PyObject *ns = PyDict_New();
  if (! ns) {
    Py_DECREF(wrapper);
    return -1;
  }
  KosDynFrame *handler_frame = kos_dyn_push_frame(ctx, KosDynPrompt_HANDLER_CODE(prompt), ns);
  Py_DECREF(ns);
  if (! handler_frame) {
    Py_DECREF(wrapper);
    return -1;
  }

  KosDynFrame_PC(KosContext_FRAME_TOP(ctx)) = KosDynPrompt_HANDLER_PC(prompt);
  return push_result(ctx, wrapper);
}


/**
   PUSH_CC: no arg. Push a CC block onto the dynamic chain. The resume PC
   (instruction after the matching POP_CC) is taken from the block table
   (extent pop_pc + 1), not from an instruction argument.
*/
KOS_OP_HANDLER(PUSH_CC) {
  KosCode *code = KosDynFrame_CODE(KosContext_FRAME_TOP(ctx));
  uint32_t pc = (uint32_t)(insn - KosCode_CODE(code));
  uint32_t eid = kos_extent_find((PyObject *) code, pc, KOS_EXT_CC);
  if (eid == KOS_EXTENT_NONE) {
    PyErr_SetString(PyExc_RuntimeError, "PUSH_CC: no CC extent at this PC");
    return -1;
  }
  KosExtentDesc *ext = kos_extent_desc((PyObject *) code, eid);
  if (! ext) {
    PyErr_SetString(PyExc_RuntimeError, "PUSH_CC: invalid extent");
    return -1;
  }
  uint32_t resume_pc = ext->pop_pc + 1u;
  if (! kos_dyn_push_cc(ctx, resume_pc)) {
    return -1;
  }
  (void) out_ev;
  return 0;
}


/**
   POP_CC: arg unused. Pop the CC block from the dynamic chain. The next
   instruction is the continuation's resume point.
*/
KOS_OP_HANDLER(POP_CC) {
  if (! KosContext_DYN_TOP(ctx) || ! KosDynNode_IS_CC(KosContext_DYN_TOP(ctx))) {
    PyErr_SetString(PyExc_RuntimeError, "POP_CC: top is not a CC block");
    return -1;
  }
  kos_dyn_pop_cc(ctx, (KosDynCC *) KosContext_DYN_TOP(ctx));
  (void) out_ev;
  (void) insn;
  return 0;
}


/**
   CAPTURE_CC: arg unused. Capture the undelimited continuation up to the
   current CC block and push it (KosPyContinuation) on the stack.
*/
KOS_OP_HANDLER(CAPTURE_CC) {
  KosContinuation *k = NULL;
  if (kos_capture_cc(ctx, &k) < 0) {
    return -1;
  }
  PyObject *wrapper = KosPyContinuation_New(k);
  if (! wrapper) {
    kos_continuation_free(k);
    return -1;
  }
  return push_result(ctx, wrapper);
}


/**
   INVOKE_K: arg unused. Pop value then continuation (Python wrapper). Invoke
   the continuation with that value, replacing current execution state.
*/
KOS_OP_HANDLER(INVOKE_K) {
  PyObject *value = KosStackBlock_PopRef(&KosDynFrame_STACK(KosContext_FRAME_TOP(ctx)));
  if (! value) {
    SAY("[eval] INVOKE_K pop value failed\n");
    return -1;
  }

  PyObject *cont_obj = KosStackBlock_PopRef(&KosDynFrame_STACK(KosContext_FRAME_TOP(ctx)));
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
  if (k->token_ref != (PyObject *) KosContext_TOKEN(ctx)) {
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
  /* cont_obj not DECREF'd: invoke transfers control on success */
  (void) out_ev;
  (void) insn;
  return 0;
}


/**
   INVOKE_K_VAR: arg = constant index (name). Pop value, look up name in
   current namespace, invoke that continuation with value.
*/
KOS_OP_HANDLER(INVOKE_K_VAR) {
  KosCode *code = KosDynFrame_CODE(KosContext_FRAME_TOP(ctx));
  PyObject *name = KosCode_GetConstant((PyObject *) code, insn->arg);
  if (! name)
    return -1;

  PyObject *value = KosStackBlock_PopRef(&KosDynFrame_STACK(KosContext_FRAME_TOP(ctx)));
  if (! value)
    return -1;

  PyObject *ns = KosDynFrame_NS(KosContext_FRAME_TOP(ctx));
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

  if (k->token_ref != (PyObject *) KosContext_TOKEN(ctx)) {
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
  (void) insn;
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
    PyObject *const *args = KosStackBlock_TailPtr(KosDynFrame_STACK(KosContext_FRAME_TOP(ctx)),
                                                  (size_t) opa);
    if (! args)
      return -1;
    res = fn(args, (size_t) opa);
    KosStackBlock_DropNoErr(&KosDynFrame_STACK(KosContext_FRAME_TOP(ctx)), (size_t) opa);
  }

  if (! res)
    return -1;
  return push_result(ctx, res);
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
    PyObject *val = KosStackBlock_PopRef(&KosDynFrame_STACK(KosContext_FRAME_TOP(ctx)));       \
    if (! val) return -1;                                              \
    PyObject *res = func(val);                                          \
    Py_DECREF(val);                                                    \
    if (! res) return -1;                                              \
    (void) insn;                                                       \
    return push_result(ctx, res);                                      \
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
  PyObject *val = KosStackBlock_PopRef(&KosDynFrame_STACK(KosContext_FRAME_TOP(ctx)));
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
  return push_result(ctx, res);
}


/**
   OBJECT_NOT: arg unused. Pop TOS, push bool(not TOS).
*/
KOS_OP_HANDLER(OBJECT_NOT) {
  PyObject *val = KosStackBlock_PopRef(&KosDynFrame_STACK(KosContext_FRAME_TOP(ctx)));
  if (! val)
    return -1;

  int r = PyObject_Not(val);
  Py_DECREF(val);
  if (r < 0)
    return -1;

  PyObject *res = (r ? Py_True : Py_False);
  Py_INCREF(res);
  (void) insn;
  return push_result(ctx, res);
}


/**
   OBJECT_TRUTH: arg unused. Pop TOS, push bool(TOS).
*/
KOS_OP_HANDLER(OBJECT_TRUTH) {
  PyObject *val = KosStackBlock_PopRef(&KosDynFrame_STACK(KosContext_FRAME_TOP(ctx)));
  if (! val)
    return -1;

  int r = PyObject_IsTrue(val);
  Py_DECREF(val);
  if (r < 0)
    return -1;

  PyObject *res = (r ? Py_True : Py_False);
  Py_INCREF(res);
  (void) insn;
  return push_result(ctx, res);
}


/**
   OBJECT_TYPE: arg unused. Pop TOS, push type(TOS).
*/
KOS_OP_HANDLER(OBJECT_TYPE) {
  PyObject *val = KosStackBlock_PopRef(&KosDynFrame_STACK(KosContext_FRAME_TOP(ctx)));
  if (! val)
    return -1;

  PyObject *res = (PyObject *) Py_TYPE(val);
  Py_INCREF(res);
  Py_DECREF(val);
  (void) insn;
  return push_result(ctx, res);
}


/**
   OBJECT_VARS: arg unused. Pop TOS, push getattr(TOS, "__dict__").
*/
KOS_OP_HANDLER(OBJECT_VARS) {
  PyObject *val = KosStackBlock_PopRef(&KosDynFrame_STACK(KosContext_FRAME_TOP(ctx)));
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
  return push_result(ctx, d);
}


/**
   OBJECT_AITER: arg unused. Pop TOS (async iterable), call __aiter__, push
   the async iterator or await it.
*/
KOS_OP_HANDLER(OBJECT_AITER) {
  PyObject *val = KosStackBlock_PopRef(&KosDynFrame_STACK(KosContext_FRAME_TOP(ctx)));
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
  return push_result(ctx, res);
}


#define OP_BINARY_NUM(name, func)                                       \
  KOS_OP_HANDLER(name) {                                                \
    if (insn->arg != 2) {                                              \
      PyErr_SetString(PyExc_RuntimeError, #name ": only arg 2 supported"); \
      return -1;                                                        \
    }                                                                   \
    PyObject *right = KosStackBlock_PopRef(&KosDynFrame_STACK(KosContext_FRAME_TOP(ctx)));      \
    if (! right) return -1;                                            \
    PyObject *left = KosStackBlock_PopRef(&KosDynFrame_STACK(KosContext_FRAME_TOP(ctx)));      \
    if (! left) { Py_DECREF(right); return -1; }                       \
    PyObject *res = func(left, right);                                 \
    Py_DECREF(left);                                                  \
    Py_DECREF(right);                                                 \
    if (! res) return -1;                                             \
    return push_result(ctx, res);                                    \
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
    mod = KosStackBlock_PopRef(&KosDynFrame_STACK(KosContext_FRAME_TOP(ctx)));
    if (! mod)
      return -1;
  }

  exp = KosStackBlock_PopRef(&KosDynFrame_STACK(KosContext_FRAME_TOP(ctx)));
  if (! exp) {
    if (opa == 3)
      Py_DECREF(mod);
    return -1;
  }

  base = KosStackBlock_PopRef(&KosDynFrame_STACK(KosContext_FRAME_TOP(ctx)));
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
  return push_result(ctx, val);
}


/**
   Binary op: pop right then left, call fn(left, right), push result or await.
   fn returns a new reference or NULL.
*/
static int obj_op_binary(
    KosContext *ctx,
    KosEvent *out_ev,
    KosOp *insn,
    PyObject *(*fn)(PyObject *left, PyObject *right)) {

  PyObject *right = KosStackBlock_PopRef(&KosDynFrame_STACK(KosContext_FRAME_TOP(ctx)));
  if (! right)
    return -1;
  PyObject *left = KosStackBlock_PopRef(&KosDynFrame_STACK(KosContext_FRAME_TOP(ctx)));
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
  return push_result(ctx, res);
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


static int op_OBJECT_EQ_impl(
    KosContext *ctx,
    KosEvent *out_ev,
    KosOp *insn,
    int op) {

  PyObject *right = KosStackBlock_PopRef(&KosDynFrame_STACK(KosContext_FRAME_TOP(ctx)));
  if (! right)
    return -1;
  PyObject *left = KosStackBlock_PopRef(&KosDynFrame_STACK(KosContext_FRAME_TOP(ctx)));
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
  return push_result(ctx, res);
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
    match = KosStackBlock_PopRef(&KosDynFrame_STACK(KosContext_FRAME_TOP(ctx)));
    if (! match)
      return -1;
    value = KosStackBlock_PopRef(&KosDynFrame_STACK(KosContext_FRAME_TOP(ctx)));
    if (! value) {
      Py_DECREF(match);
      return -1;
    }
  } else {
    match = KosStackBlock_PopTail(&KosDynFrame_STACK(KosContext_FRAME_TOP(ctx)), (size_t) opa);
    if (! match)
      return -1;
    value = KosStackBlock_PopRef(&KosDynFrame_STACK(KosContext_FRAME_TOP(ctx)));
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
  return push_result(ctx, out);
}


/**
   OBJECT_IS_SUBCLASS: arg = 2 (class, base) or N (class, base, ... bases).
   Pop class and base(s), push issubclass(class, base_or_tuple).
*/
KOS_OP_HANDLER(OBJECT_IS_SUBCLASS) {
  int opa = (int) insn->arg;
  PyObject *value = NULL, *match = NULL;

  if (opa < 2) {
    match = KosStackBlock_PopRef(&KosDynFrame_STACK(KosContext_FRAME_TOP(ctx)));
    if (! match)
      return -1;
    value = KosStackBlock_PopRef(&KosDynFrame_STACK(KosContext_FRAME_TOP(ctx)));
    if (! value) {
      Py_DECREF(match);
      return -1;
    }
  } else {
    match = KosStackBlock_PopTail(&KosDynFrame_STACK(KosContext_FRAME_TOP(ctx)), (size_t) opa);
    if (! match)
      return -1;
    value = KosStackBlock_PopRef(&KosDynFrame_STACK(KosContext_FRAME_TOP(ctx)));
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
  return push_result(ctx, out);
}


/* Helper for CHECK_EQ/GE/GT/LE/LT/NE: pop right then left, RichCompareBool;
   jump to insn->arg if comparison fails (res == 0). */
static int kos_check_cmp(
    KosContext *ctx,
    KosEvent *out_ev,
    KosOp *insn,
    int cmp_op) {

  PyObject *right = KosStackBlock_PopRef(&KosDynFrame_STACK(KosContext_FRAME_TOP(ctx)));
  if (! right)
    return -1;
  PyObject *left = KosStackBlock_PopRef(&KosDynFrame_STACK(KosContext_FRAME_TOP(ctx)));
  if (! left) {
    Py_DECREF(right);
    return -1;
  }
  int res = PyObject_RichCompareBool(left, right, cmp_op);
  Py_DECREF(left);
  Py_DECREF(right);
  if (res < 0)
    return -1;
  if (res == 0)
    KosDynFrame_PC(KosContext_FRAME_TOP(ctx)) = insn->arg;
  (void) out_ev;
  return 0;
}


/* THEEND: no runtime implementation; hardcoded error (not a "stub" to be implemented). */

/**
   THEEND: arg unused. End-of-code sentinel; has no runtime implementation.
   If execution reaches THEEND, raise a hardcoded unimplemented error.
*/
KOS_OP_HANDLER(THEEND) {
  (void) ctx;
  (void) out_ev;
  (void) insn;
  PyErr_SetString(PyExc_RuntimeError, "THEEND has no runtime implementation");
  return -1;
}


/**
   BREAK: arg = result count (0 = None, 1 = single value, N = tuple of N).
   Exit innermost loop/foreach; jump to its break target. Pops the loop
   region and pushes the result value to the parent stack as the loop
   expression value. Raises RuntimeError if no LOOP/FOREACH region on chain.
*/
KOS_OP_HANDLER(BREAK) {
  unsigned opa = insn->arg;
  PyObject *result = NULL;

  /* Pop result value(s) from stack before any unwinding */
  if (opa == 0) {
    result = Py_None;
    Py_INCREF(result);
  } else if (opa == 1) {
    result = KosStackBlock_PopRef(&KosDynFrame_STACK(KosContext_FRAME_TOP(ctx)));
    if (! result) {
      PyErr_SetString(PyExc_RuntimeError, "BREAK pop underrun");
      return -1;
    }
  } else {
    result = KosStackBlock_PopTail(&KosDynFrame_STACK(KosContext_FRAME_TOP(ctx)), (size_t) opa);
    if (! result) {
      PyErr_SetString(PyExc_RuntimeError, "BREAK PopTail underrun");
      return -1;
    }
  }

  KosDynRegion *region = kos_find_region_for_loop(ctx);
  if (! region) {
    Py_DECREF(result);
    PyErr_SetString(PyExc_RuntimeError, "break outside loop");
    return -1;
  }

  uint32_t break_pc = KosDynRegion_BREAK_PC(region);

  /* Unwind inner regions until we reach the target loop region */
  while (KosContext_DYN_TOP(ctx) != (KosDynNode *) region) {
    KosDynNode *top = KosContext_DYN_TOP(ctx);
    if (KosDynNode_IS_REGION(top)) {
      KosDynRegion *r = (KosDynRegion *) top;
      if (KosDynRegion_STYLE(r) == KOS_REGION_LOOP || KosDynRegion_STYLE(r) == KOS_REGION_FOREACH) {
        kos_dyn_pop_region(ctx, r);
        if (KosStackBlock_DropBlock(&KosDynFrame_STACK(KosContext_FRAME_TOP(ctx))) < 0) {
          Py_DECREF(result);
          return -1;
        }
      } else {
        if (kos_dyn_pop_top(ctx) < 0) {
          Py_DECREF(result);
          return -1;
        }
      }
    } else {
      if (kos_dyn_pop_top(ctx) < 0) {
        Py_DECREF(result);
        return -1;
      }
    }
  }

  /* Pop the target loop region and its stack block */
  kos_dyn_pop_region(ctx, region);
  if (KosStackBlock_DropBlock(&KosDynFrame_STACK(KosContext_FRAME_TOP(ctx))) < 0) {
    Py_DECREF(result);
    return -1;
  }

  /* Push result to parent stack as the loop expression value */
  if (KosStackBlock_PushRef(&KosDynFrame_STACK(KosContext_FRAME_TOP(ctx)), result) < 0) {
    Py_DECREF(result);
    return -1;
  }
  Py_DECREF(result);

  KosDynFrame_PC(KosContext_FRAME_TOP(ctx)) = break_pc;
  (void) out_ev;
  return 0;
}


/**
   CONTINUE: arg = result count (0 = None, 1 = single value, N = tuple of N).
   Jump to innermost loop/foreach continue target. Updates the region result
   and clears the loop stack block before jumping. Raises RuntimeError if
   no LOOP/FOREACH region on the chain.
*/
KOS_OP_HANDLER(CONTINUE) {
  unsigned opa = insn->arg;
  PyObject *result = NULL;

  /* Pop result value(s) from stack before any unwinding */
  if (opa == 0) {
    result = Py_None;
    Py_INCREF(result);
  } else if (opa == 1) {
    result = KosStackBlock_PopRef(&KosDynFrame_STACK(KosContext_FRAME_TOP(ctx)));
    if (! result) {
      PyErr_SetString(PyExc_RuntimeError, "CONTINUE pop underrun");
      return -1;
    }
  } else {
    result = KosStackBlock_PopTail(&KosDynFrame_STACK(KosContext_FRAME_TOP(ctx)), (size_t) opa);
    if (! result) {
      PyErr_SetString(PyExc_RuntimeError, "CONTINUE PopTail underrun");
      return -1;
    }
  }

  KosDynRegion *region = kos_find_region_for_loop(ctx);
  if (! region) {
    Py_DECREF(result);
    PyErr_SetString(PyExc_RuntimeError, "continue outside loop");
    return -1;
  }

  /* Update region result with continue value */
  Py_XDECREF(KosDynRegion_RESULT(region));
  KosDynRegion_RESULT(region) = result;

  uint32_t continue_pc = KosDynRegion_CONTINUE_PC(region);

  /* Unwind inner regions until we reach the target loop region */
  while (KosContext_DYN_TOP(ctx) != (KosDynNode *) region) {
    KosDynNode *top = KosContext_DYN_TOP(ctx);
    if (KosDynNode_IS_REGION(top)) {
      KosDynRegion *r = (KosDynRegion *) top;
      if (KosDynRegion_STYLE(r) == KOS_REGION_LOOP || KosDynRegion_STYLE(r) == KOS_REGION_FOREACH) {
        kos_dyn_pop_region(ctx, r);
        if (KosStackBlock_DropBlock(&KosDynFrame_STACK(KosContext_FRAME_TOP(ctx))) < 0)
          return -1;
      } else {
        if (kos_dyn_pop_top(ctx) < 0)
          return -1;
      }
    } else {
      if (kos_dyn_pop_top(ctx) < 0)
        return -1;
    }
  }

  /* Clear the loop stack block contents for next iteration */
  if (KosStackBlock_ClearBlock(&KosDynFrame_STACK(KosContext_FRAME_TOP(ctx))) < 0)
    return -1;

  KosDynFrame_PC(KosContext_FRAME_TOP(ctx)) = continue_pc;
  (void) out_ev;
  return 0;
}


/**
   YIELD: arg = 0 (None), 1 (single value), or N (tuple of N values).
   Pop the value(s) from the stack and suspend with KOS_EV_YIELD.
   Resumption (send value) is handled by py_resume_inject at re-entry.
*/
KOS_OP_HANDLER(YIELD) {
  unsigned opa = insn->arg;
  PyObject *val = NULL;

  if (opa == 0) {
    val = Py_None;
    Py_INCREF(val);

  } else if (opa == 1) {
    val = KosStackBlock_PopRef(&KosDynFrame_STACK(KosContext_FRAME_TOP(ctx)));
    if (! val) {
      SAY("[eval] YIELD pop underrun\n");
      return -1;
    }

  } else {
    val = KosStackBlock_PopTail(&KosDynFrame_STACK(KosContext_FRAME_TOP(ctx)), (size_t) opa);
    if (! val) {
      SAY("[eval] YIELD PopTail underrun\n");
      return -1;
    }
  }

  /* Frame has parent: return into parent in same loop (CALL return or async gen step). */
  if (KosDynNode_PARENT(KosContext_FRAME_TOP(ctx)) != NULL) {
    int was_pending_ctx = (KosContext_PENDING_AGEN(ctx) != NULL);
    KosPyAsyncGen *agen = was_pending_ctx ? (KosPyAsyncGen *) KosContext_PENDING_AGEN(ctx) : NULL;
    if (was_pending_ctx && agen)
      agen->in_progress = 0;
    KosDynFrame *detached = NULL;
    if (kos_dyn_detach_top_frame(ctx, &detached) < 0) {
      Py_DECREF(val);
      return -1;
    }
    if (was_pending_ctx && agen) {
      KosContext_PENDING_AGEN(ctx) = NULL;
      Py_XDECREF(KosContext_PENDING_AGEN_DEFAULT(ctx));
      KosContext_PENDING_AGEN_DEFAULT(ctx) = NULL;
    }
    if (KosStackBlock_PushRef(&KosDynFrame_STACK(KosContext_FRAME_TOP(ctx)), val) < 0) {
      Py_DECREF(val);
      return -1;
    }
    Py_DECREF(val);
    return 0;
  }

  out_ev->kind = KOS_EV_YIELD;
  out_ev->payload = val;
  KosContext_FLAGS(ctx) &= ~KOS_CTX_RUNNING;
  KosContext_FLAGS(ctx) |= KOS_CTX_DEAD;
  return 1;
}


/* Yield-from delegate kinds (must match normalizer and handler). */
enum {
  KOS_YF_SYNC_ITER = 0,
  KOS_YF_SYNC_GEN = 1,
  KOS_YF_PY_ASYNC_ITER = 2,
  KOS_YF_PY_ASYNC_GEN = 3,
  KOS_YF_KOS_ASYNC_GEN = 4,
};


/* Yield-from pending action (STEP / SEND / THROW / CLOSE). */
enum {
  KOS_YF_STEP = 0,
  KOS_YF_SEND = 1,
  KOS_YF_THROW = 2,
  KOS_YF_CLOSE = 3,
};


void kos_yieldfrom_close_delegate(KosDynYieldFrom *node) {

  if (! node)
    return;

  PyObject *delegate = KosDynYieldFrom_DELEGATE(node);
  if (! delegate)
    return;

  uint8_t kind = KosDynYieldFrom_KIND(node);
  PyObject *inflight = KosDynYieldFrom_INFLIGHT(node);

  switch (kind) {
  case KOS_YF_SYNC_ITER:
    break;
  case KOS_YF_SYNC_GEN: {
    PyObject *meth = PyUnicode_FromString("close");
    if (meth) {
      PyObject *res = PyObject_CallMethodObjArgs(delegate, meth, NULL);
      Py_XDECREF(meth);
      Py_XDECREF(res);
    }
    PyErr_Clear();
    break;
  }
  case KOS_YF_PY_ASYNC_ITER:
  case KOS_YF_PY_ASYNC_GEN:
    Py_XDECREF(inflight);
    KosDynYieldFrom_INFLIGHT(node) = NULL;
    {
      PyObject *meth = PyUnicode_FromString("aclose");
      if (meth) {
        PyObject *res = PyObject_CallMethodObjArgs(delegate, meth, NULL);
        Py_XDECREF(meth);
        Py_XDECREF(res);
      }
      PyErr_Clear();
    }
    break;
  case KOS_YF_KOS_ASYNC_GEN:
    if (KosPyAsyncGen_Check(delegate))
      ((KosPyAsyncGen *) delegate)->closed = 1;
    break;
  default:
    break;
  }
}


/**
   Normalize TOS to a (delegate, kind) for yield-from. On success returns 0
   and sets *delegate_out (new ref) and *kind_out. On failure returns -1.

   Yield-from operates on iterables and async-iterables only. Coroutines
   are rejected (same idea as iter(1) being invalid). We classify by type
   so the handler can drive the delegate with the right protocol.
*/
static int yield_from_normalize(
    PyObject *obj,
    PyObject **delegate_out,
    uint8_t *kind_out) {

  if (! obj) {
    PyErr_SetString(PyExc_TypeError, "yield-from requires an object");
    return -1;
  }

  /* --- Reject non-iterables ---
     Coroutines are awaitable, not iterable. Using yield-from on a
     coroutine is a type error (like yield from 1). */
  if (KosCoroutine_Check(obj)
#ifdef PyCoro_CheckExact
      || PyCoro_CheckExact(obj)
#endif
      ) {
    PyErr_SetString(PyExc_TypeError,
                    "coroutine object is not async iterable");
    return -1;
  }

  /* --- Koskari async gen ---
     We drive it in-process via koskari_eval; no Python protocol. */
  if (KosPyAsyncGen_Check(obj)) {
    Py_INCREF(obj);
    *delegate_out = obj;
    *kind_out = KOS_YF_KOS_ASYNC_GEN;
    return 0;
  }

  /* --- Python sync generator ---
     Has send/throw/close; we use send(None) for step, send(val) for send. */
  if (PyGen_Check(obj)) {
    Py_INCREF(obj);
    *delegate_out = obj;
    *kind_out = KOS_YF_SYNC_GEN;
    return 0;
  }

  /* --- Python async iterator / async gen ---
     Has __anext__; async gen also has asend/athrow/aclose. We use
     __anext__ for step and asend/athrow/aclose when needed. */
  if (PyObject_HasAttrString(obj, "__anext__")) {
    Py_INCREF(obj);
    *delegate_out = obj;
    *kind_out = PyObject_HasAttrString(obj, "asend")
        ? KOS_YF_PY_ASYNC_GEN
        : KOS_YF_PY_ASYNC_ITER;
    return 0;
  }

  /* --- Sync iterator ---
     PyIter_Next for step; no send/close. */
  if (PyIter_Check(obj)) {
    Py_INCREF(obj);
    *delegate_out = obj;
    *kind_out = KOS_YF_SYNC_ITER;
    return 0;
  }

  /* --- Callable (e.g. generator function) ---
     Call once to get the actual iterator/gen; then classify as above.
     Reject if the call returns a coroutine. */
  if (PyCallable_Check(obj)) {
    PyObject *gen = PyObject_CallFunctionObjArgs(obj, NULL);
    if (! gen)
      return -1;
    if (KosCoroutine_Check(gen)
#ifdef PyCoro_CheckExact
        || PyCoro_CheckExact(gen)
#endif
        ) {
      Py_DECREF(gen);
      PyErr_SetString(PyExc_TypeError,
                      "coroutine object is not async iterable");
      return -1;
    }
    if (PyGen_Check(gen)) {
      *delegate_out = gen;
      *kind_out = KOS_YF_SYNC_GEN;
      return 0;
    }
    if (PyObject_HasAttrString(gen, "__anext__")) {
      *delegate_out = gen;
      *kind_out = PyObject_HasAttrString(gen, "asend")
          ? KOS_YF_PY_ASYNC_GEN
          : KOS_YF_PY_ASYNC_ITER;
      return 0;
    }
    if (KosPyAsyncGen_Check(gen)) {
      *delegate_out = gen;
      *kind_out = KOS_YF_KOS_ASYNC_GEN;
      return 0;
    }
    Py_DECREF(gen);
    PyErr_SetString(PyExc_TypeError,
                    "yield-from callable did not return a generator");
    return -1;
  }

  /* --- Fallback: sync iteration via iter(obj) ---
     For any other object that supports __iter__. */
  {
    PyObject *it = PyObject_GetIter(obj);
    if (! it)
      return -1;
    *delegate_out = it;
    *kind_out = KOS_YF_SYNC_ITER;
    return 0;
  }
}


/**
   Set frame PC back to the YIELD_FROM instruction so the next loop
   iteration re-dispatches to YIELD_FROM (used when suspending).
*/
static void yield_from_rollback_pc(KosContext *ctx, KosOp *insn) {

  KosDynFrame *frame = KosContext_FRAME_TOP(ctx);
  KosCode *code = KosDynFrame_CODE(frame);
  uint32_t pc = (uint32_t)(insn - KosCode_CODE(code));
  KosDynFrame_PC(frame) = pc;
}


/**
   Fetch the value from the currently set StopIteration exception.
   Call only when PyErr_ExceptionMatches(PyExc_StopIteration) is true.
   Consumes the exception. Returns a new reference (value or Py_None).
*/
static PyObject *yield_from_fetch_stop_iteration_value(void) {

  PyObject *value = NULL;
  PyGen_FetchStopIterationValue(&value);
  if (! value)
    value = Py_None;
  Py_INCREF(value);
  return value;
}


/**
   Emit KOS_EV_YIELD using the value at TOS. Call after pushing the
   yielded value onto the frame stack. Sets DEAD and returns 1.
*/
static int yield_from_emit_yield(
    KosContext *ctx,
    KosDynFrame *frame,
    KosEvent *out_ev,
    KosOp *insn) {

  yield_from_rollback_pc(ctx, insn);
  {
    PyObject *const *tos = KosStackBlock_TailPtr(
        KosDynFrame_STACK(frame), 1);
    out_ev->kind = KOS_EV_YIELD;
    out_ev->payload = tos ? *tos : NULL;
    Py_XINCREF(out_ev->payload);
  }
  KosContext_FLAGS(ctx) &= ~KOS_CTX_RUNNING;
  KosContext_FLAGS(ctx) |= KOS_CTX_DEAD;
  return 1;
}


/**
   Emit KOS_EV_RETURN for delegate exhaustion. Pops the yield_from node,
   takes ownership of final_val (caller must not DECREF), sets out_ev and
   DEAD, returns 1.
*/
static int yield_from_emit_return(
    KosContext *ctx,
    KosDynYieldFrom *yf,
    PyObject *final_val,
    KosEvent *out_ev) {

  kos_dyn_pop_yield_from(ctx, yf);
  out_ev->kind = KOS_EV_RETURN;
  out_ev->payload = final_val;
  KosContext_FLAGS(ctx) &= ~KOS_CTX_RUNNING;
  KosContext_FLAGS(ctx) |= KOS_CTX_DEAD;
  return 1;
}


/**
   YIELD_FROM: delegate to a sub-iterator or sub-generator; PC-stable
   state machine. First time we see this insn, TOS is the delegate
   (result of the yield-from expression); we normalize it, push a
   yield_from dyn node, and roll back PC so we re-dispatch and drive.
   On subsequent iterations we have that dyn node and we drive one step
   (or handle close/throw). We branch by delegate kind so each protocol
   is handled in one place.
*/
KOS_OP_HANDLER(YIELD_FROM) {

  KosDynNode *dyn_top = KosContext_DYN_TOP(ctx);
  KosDynFrame *frame = KosContext_FRAME_TOP(ctx);

  if (! KosDynNode_IS_YIELD_FROM(dyn_top)) {
    /* First entry: TOS is the yield-from expression result. Pop it,
       normalize to (delegate, kind), push the yield_from node, roll
       back PC so the next iteration runs this handler again and we
       drive the delegate. */
    PyObject *subexpr = KosStackBlock_PopRef(&KosDynFrame_STACK(frame));
    if (! subexpr) {
      return -1;
    }
    PyObject *delegate = NULL;
    uint8_t kind = 0;
    if (yield_from_normalize(subexpr, &delegate, &kind) < 0) {
      Py_DECREF(subexpr);
      return -1;
    }
    Py_DECREF(subexpr);
    if (! kos_dyn_push_yield_from(ctx, delegate, kind)) {
      Py_DECREF(delegate);
      return -1;
    }
    Py_DECREF(delegate);
    KosDynYieldFrom_PENDING_ACTION(
        (KosDynYieldFrom *) KosContext_DYN_TOP(ctx)) = KOS_YF_STEP;
    yield_from_rollback_pc(ctx, insn);
    return 0;
  }

  /* Resume: we have a yield_from node. If the loop pushed an inject
     (from a previous yield or await), pop it into the node and set
     action; then drive one step according to kind. */
  KosDynYieldFrom *yf = (KosDynYieldFrom *) dyn_top;
  uint8_t action = KosDynYieldFrom_PENDING_ACTION(yf);

  if (KosContext_PY_RESUME_KIND(ctx) != KOS_RESUME_NONE) {
    uint8_t resume_kind = KosContext_PY_RESUME_KIND(ctx);
    KosContext_PY_RESUME_KIND(ctx) = KOS_RESUME_NONE;
    PyObject *inject = KosStackBlock_PopRef(&KosDynFrame_STACK(frame));
    if (! inject)
      return -1;
    Py_XDECREF(KosDynYieldFrom_PENDING_ARG(yf));
    KosDynYieldFrom_PENDING_ARG(yf) = inject;
    action = (resume_kind == KOS_RESUME_AWAIT) ? KOS_YF_STEP : KOS_YF_SEND;
    KosDynYieldFrom_PENDING_ACTION(yf) = action;
  }
  if (KosContext_INJECT_MODE(ctx) == KOS_INJECT_THROW) {
    PyObject *exc = KosContext_INJECT_VALUE(ctx);
    KosContext_INJECT_MODE(ctx) = KOS_INJECT_NONE;
    KosContext_INJECT_VALUE(ctx) = NULL;
    Py_XDECREF(KosDynYieldFrom_PENDING_ARG(yf));
    KosDynYieldFrom_PENDING_ARG(yf) = exc;
    Py_INCREF(exc);
    action = KOS_YF_THROW;
    KosDynYieldFrom_PENDING_ACTION(yf) = action;
  }

  uint8_t kind = KosDynYieldFrom_KIND(yf);
  PyObject *delegate = KosDynYieldFrom_DELEGATE(yf);

  /* Drive one step. Each branch handles one delegate kind and either
     returns (event or error) or, for exhaustion with a value, pushes
     and returns 0 so the YIELD_FROM completes. */

  /* --- SYNC_ITER ---
     Drive with PyIter_Next. No send/close; throw is ignored and we continue
     stepping. Exhaustion is StopIteration (possibly with .value). */
  if (kind == KOS_YF_SYNC_ITER) {
    if (action != KOS_YF_STEP && action != KOS_YF_SEND) {
      if (action == KOS_YF_CLOSE)
        ;  /* no-op for sync iter */
      if (action == KOS_YF_THROW) {
        Py_XDECREF(KosDynYieldFrom_PENDING_ARG(yf));
        KosDynYieldFrom_PENDING_ARG(yf) = NULL;
        KosDynYieldFrom_PENDING_ACTION(yf) = KOS_YF_STEP;
      }
    }
    if (action == KOS_YF_SEND && KosDynYieldFrom_PENDING_ARG(yf) != Py_None) {
      PyErr_SetString(PyExc_TypeError,
                      "send() to sync iterator only accepts None");
      return -1;
    }
    PyObject *n = PyIter_Next(delegate);
    if (n) {
      if (KosStackBlock_PushRef(&KosDynFrame_STACK(frame), n) < 0) {
        Py_DECREF(n);
        return -1;
      }
      Py_DECREF(n);
      return yield_from_emit_yield(ctx, frame, out_ev, insn);
    }
    if (! PyErr_Occurred())
      return -1;
    if (! PyErr_ExceptionMatches(PyExc_StopIteration))
      return -1;
    {
      PyObject *final_val = yield_from_fetch_stop_iteration_value();
      return yield_from_emit_return(ctx, yf, final_val, out_ev);
    }
  }

  /* --- SYNC_GEN ---
     Drive with send(None) for step, send(val) for send, throw/close as
     needed. Generator return raises StopIteration(value); we treat that
     as exhaustion and emit RETURN. Check exhaustion before treating a
     non-NULL return as a yielded value. */
  if (kind == KOS_YF_SYNC_GEN) {
    PyObject *send_val = Py_None;
    if (action == KOS_YF_SEND && KosDynYieldFrom_PENDING_ARG(yf))
      send_val = KosDynYieldFrom_PENDING_ARG(yf);

    if (action == KOS_YF_THROW) {
      PyObject *exc = KosDynYieldFrom_PENDING_ARG(yf);
      PyObject *meth = PyUnicode_FromString("throw");
      if (meth && exc) {
        PyObject *res = PyObject_CallMethodObjArgs(delegate, meth,
            exc, Py_None, Py_None, NULL);
        Py_XDECREF(meth);
        if (res) {
          KosDynYieldFrom_PENDING_ACTION(yf) = KOS_YF_STEP;
          Py_XDECREF(KosDynYieldFrom_PENDING_ARG(yf));
          KosDynYieldFrom_PENDING_ARG(yf) = NULL;
          if (KosStackBlock_PushRef(&KosDynFrame_STACK(frame), res) < 0) {
            Py_DECREF(res);
            return -1;
          }
          Py_DECREF(res);
          return yield_from_emit_yield(ctx, frame, out_ev, insn);
        }
      }
      kos_yieldfrom_close_delegate(yf);
      kos_dyn_pop_yield_from(ctx, yf);
      return -1;
    }
    if (action == KOS_YF_CLOSE) {
      PyObject *meth = PyUnicode_FromString("close");
      if (meth) {
        PyObject *res = PyObject_CallMethodObjArgs(delegate, meth, NULL);
        Py_XDECREF(meth);
        Py_XDECREF(res);
      }
      PyErr_Clear();
      kos_dyn_pop_yield_from(ctx, yf);
      PyErr_SetNone(PyExc_GeneratorExit);
      return -1;
    }
    {
      PyObject *meth = PyUnicode_FromString("send");
      if (! meth)
        return -1;
      PyObject *res = PyObject_CallMethodObjArgs(delegate, meth, send_val, NULL);
      Py_XDECREF(meth);
      if (PyErr_ExceptionMatches(PyExc_StopIteration)) {
        Py_XDECREF(res);
        PyObject *final_val = yield_from_fetch_stop_iteration_value();
        return yield_from_emit_return(ctx, yf, final_val, out_ev);
      }
      if (res) {
        if (KosStackBlock_PushRef(&KosDynFrame_STACK(frame), res) < 0) {
          Py_DECREF(res);
          return -1;
        }
        Py_DECREF(res);
        return yield_from_emit_yield(ctx, frame, out_ev, insn);
      }
      return -1;
    }
  }

  /* --- PY_ASYNC_ITER / PY_ASYNC_GEN ---
     Drive via __anext__ (step), asend/athrow/aclose for async gen. We
     obtain an awaitable, run eager_step_awaitable; if it suspends we
     emit AWAIT; if it completes with a value we emit YIELD; if
     StopAsyncIteration we push the value and return 0 (YIELD_FROM
     completes with that value). Coroutines are rejected at normalize
     so we never see immediate completion of a coroutine here. */
  if (kind == KOS_YF_PY_ASYNC_ITER || kind == KOS_YF_PY_ASYNC_GEN) {
    /* Resuming from await: we had suspended with INFLIGHT set; the
       result was stashed in PENDING_ARG. Push it and emit YIELD. */
    if (KosDynYieldFrom_INFLIGHT(yf)) {
      PyObject *aw_result = KosDynYieldFrom_PENDING_ARG(yf);
      Py_XDECREF(KosDynYieldFrom_INFLIGHT(yf));
      KosDynYieldFrom_INFLIGHT(yf) = NULL;
      KosDynYieldFrom_PENDING_ARG(yf) = NULL;
      KosDynYieldFrom_PENDING_ACTION(yf) = KOS_YF_STEP;
      if (! aw_result) {
        PyErr_SetString(PyExc_RuntimeError,
                        "yield-from resume with no await result");
        return -1;
      }
      if (KosStackBlock_PushRef(&KosDynFrame_STACK(frame), aw_result) < 0) {
        Py_DECREF(aw_result);
        return -1;
      }
      Py_DECREF(aw_result);
      return yield_from_emit_yield(ctx, frame, out_ev, insn);
    }

    /* Throw(StopAsyncIteration): delegate was closed with that; pop,
       push the value, return 0 so YIELD_FROM completes with it. */
    if (action == KOS_YF_THROW && KosDynYieldFrom_PENDING_ARG(yf)) {
      PyObject *exc = KosDynYieldFrom_PENDING_ARG(yf);
      if (PyObject_IsInstance(exc, (PyObject *) PyExc_StopAsyncIteration)) {
        PyObject *final_val = StopAsyncIteration_VALUE(exc);
        Py_XDECREF(KosDynYieldFrom_PENDING_ARG(yf));
        KosDynYieldFrom_PENDING_ARG(yf) = NULL;
        KosDynYieldFrom_PENDING_ACTION(yf) = KOS_YF_STEP;
        kos_dyn_pop_yield_from(ctx, yf);
        if (KosStackBlock_PushRef(&KosDynFrame_STACK(frame), final_val) < 0) {
          Py_DECREF(final_val);
          return -1;
        }
        Py_DECREF(final_val);
        return 0;
      }
    }

    /* Build the awaitable for this step: __anext__, asend, athrow, or aclose. */
    PyObject *awaitable = NULL;
    if (action == KOS_YF_STEP || (action == KOS_YF_SEND && ! KosDynYieldFrom_PENDING_ARG(yf))) {
      awaitable = get_await_next(delegate);
    } else if (kind == KOS_YF_PY_ASYNC_GEN && action == KOS_YF_SEND) {
      PyObject *send_val = KosDynYieldFrom_PENDING_ARG(yf);
      PyObject *name = PyUnicode_FromString("asend");
      if (name) {
        PyObject *tmp = PyObject_CallMethodObjArgs(delegate, name, send_val, NULL);
        Py_DECREF(name);
        if (tmp) {
          awaitable = PyObject_GetAwaitable(tmp);
          Py_DECREF(tmp);
        }
      }
    } else if (kind == KOS_YF_PY_ASYNC_GEN && action == KOS_YF_THROW) {
      PyObject *exc = KosDynYieldFrom_PENDING_ARG(yf);
      PyObject *meth = PyUnicode_FromString("athrow");
      if (meth && exc) {
        PyObject *typ = (PyObject *) Py_TYPE(exc);
        PyObject *tmp = PyObject_CallMethodObjArgs(delegate, meth, typ, exc,
                                                   Py_None, NULL);
        Py_DECREF(meth);
        if (tmp) {
          awaitable = PyObject_GetAwaitable(tmp);
          Py_DECREF(tmp);
        }
      } else {
        Py_XDECREF(meth);
      }
    } else if (kind == KOS_YF_PY_ASYNC_GEN && action == KOS_YF_CLOSE) {
      PyObject *meth = PyUnicode_FromString("aclose");
      if (meth) {
        PyObject *tmp = PyObject_CallMethodObjArgs(delegate, meth, NULL);
        Py_DECREF(meth);
        if (tmp) {
          awaitable = PyObject_GetAwaitable(tmp);
          Py_DECREF(tmp);
        }
      }
    } else if (action == KOS_YF_CLOSE) {
      /* PY_ASYNC_ITER has no aclose; treat as exhausted with None. */
      kos_dyn_pop_yield_from(ctx, yf);
      if (KosStackBlock_PushRef(&KosDynFrame_STACK(frame), Py_None) < 0)
        return -1;
      return 0;
    }

    if (! awaitable) {
      if (PyErr_ExceptionMatches(PyExc_StopAsyncIteration)) {
        PyObject *exc = PyErr_GetRaisedException();
        PyObject *final_val = StopAsyncIteration_VALUE(exc);
        Py_DECREF(exc);
        kos_dyn_pop_yield_from(ctx, yf);
        if (KosStackBlock_PushRef(&KosDynFrame_STACK(frame), final_val) < 0) {
          Py_DECREF(final_val);
          return -1;
        }
        Py_DECREF(final_val);
        return 0;
      }
      return -1;
    }

    int suspended = 0;
    PyObject *yielded = NULL;
    PyObject *result = eager_step_awaitable(awaitable, ctx, &suspended, &yielded);

    if (suspended && yielded) {
      KosDynYieldFrom_INFLIGHT(yf) = awaitable;
      yield_from_rollback_pc(ctx, insn);
      out_ev->kind = KOS_EV_AWAIT;
      out_ev->payload = yielded;
      KosContext_FLAGS(ctx) &= ~KOS_CTX_RUNNING;
      KosContext_FLAGS(ctx) |= KOS_CTX_DEAD;
      return 1;
    }

    Py_DECREF(awaitable);
    if (result) {
      KosDynYieldFrom_PENDING_ACTION(yf) = KOS_YF_STEP;
      Py_XDECREF(KosDynYieldFrom_PENDING_ARG(yf));
      KosDynYieldFrom_PENDING_ARG(yf) = NULL;
      if (KosStackBlock_PushRef(&KosDynFrame_STACK(frame), result) < 0) {
        Py_DECREF(result);
        return -1;
      }
      Py_DECREF(result);
      return yield_from_emit_yield(ctx, frame, out_ev, insn);
    }
    if (PyErr_ExceptionMatches(PyExc_StopAsyncIteration)) {
      PyObject *exc = PyErr_GetRaisedException();
      PyObject *final_val = StopAsyncIteration_VALUE(exc);
      Py_DECREF(exc);
      kos_dyn_pop_yield_from(ctx, yf);
      if (KosStackBlock_PushRef(&KosDynFrame_STACK(frame), final_val) < 0) {
        Py_DECREF(final_val);
        return -1;
      }
      Py_DECREF(final_val);
      return 0;
    }
    return -1;
  }

  /* --- KOS_ASYNC_GEN ---
     Delegate is our own async gen; we drive it with koskari_eval. Resume
     from await by injecting the result into the inner context and
     re-running. Events: YIELD (relay), RETURN (exhaustion), AWAIT
     (suspend), RAISE (propagate). */
  if (kind == KOS_YF_KOS_ASYNC_GEN) {
    KosPyAsyncGen *agen = (KosPyAsyncGen *) delegate;

    /* Resuming from await: inject the result into the inner context
       so the next koskari_eval run continues from the AWAIT. */
    if (KosDynYieldFrom_INFLIGHT(yf)) {
      KosContext *inner = agen->ctx;
      PyObject *aw_result = KosDynYieldFrom_PENDING_ARG(yf);
      Py_XDECREF(KosDynYieldFrom_INFLIGHT(yf));
      KosDynYieldFrom_INFLIGHT(yf) = NULL;
      KosDynYieldFrom_PENDING_ARG(yf) = NULL;
      KosDynYieldFrom_PENDING_ACTION(yf) = KOS_YF_STEP;
      if (! aw_result) {
        PyErr_SetString(PyExc_RuntimeError,
                        "yield-from resume with no await result");
        return -1;
      }
      Py_XDECREF(KosContext_PY_RESUME_INJECT(inner));
      KosContext_PY_RESUME_INJECT(inner) = aw_result;
      KosContext_PY_RESUME_KIND(inner) = KOS_RESUME_AWAIT;
    }

    if (action == KOS_YF_CLOSE) {
      agen->closed = 1;
      kos_dyn_pop_yield_from(ctx, yf);
      PyErr_SetNone(PyExc_GeneratorExit);
      return -1;
    }

    if (action == KOS_YF_THROW && KosDynYieldFrom_PENDING_ARG(yf)) {
      PyObject *exc = KosDynYieldFrom_PENDING_ARG(yf);
      if (PyObject_IsInstance(exc, (PyObject *) PyExc_StopAsyncIteration)) {
        PyObject *final_val = StopAsyncIteration_VALUE(exc);
        Py_XDECREF(KosDynYieldFrom_PENDING_ARG(yf));
        KosDynYieldFrom_PENDING_ARG(yf) = NULL;
        KosDynYieldFrom_PENDING_ACTION(yf) = KOS_YF_STEP;
        kos_dyn_pop_yield_from(ctx, yf);
        if (KosStackBlock_PushRef(&KosDynFrame_STACK(frame), final_val) < 0) {
          Py_DECREF(final_val);
          return -1;
        }
        Py_DECREF(final_val);
        return 0;
      }
    }

    if (! agen->ctx || agen->closed) {
      kos_dyn_pop_yield_from(ctx, yf);
      if (KosStackBlock_PushRef(&KosDynFrame_STACK(frame), Py_None) < 0)
        return -1;
      return 0;
    }

    if (action == KOS_YF_SEND && KosDynYieldFrom_PENDING_ARG(yf)) {
      KosContext *inner = agen->ctx;
      PyObject *send_val = KosDynYieldFrom_PENDING_ARG(yf);
      Py_XDECREF(KosContext_PY_RESUME_INJECT(inner));
      KosContext_PY_RESUME_INJECT(inner) = send_val;
      KosContext_PY_RESUME_KIND(inner) = KOS_RESUME_YIELD;
    }
    if (action == KOS_YF_THROW && KosDynYieldFrom_PENDING_ARG(yf)) {
      KosContext *inner = agen->ctx;
      PyObject *exc = KosDynYieldFrom_PENDING_ARG(yf);
      Py_XDECREF(KosContext_INJECT_VALUE(inner));
      KosContext_INJECT_VALUE(inner) = exc;
      Py_INCREF(exc);
      KosContext_INJECT_MODE(inner) = KOS_INJECT_THROW;
    }

    agen->in_progress = 1;
    KosEvent ev = {0};
    int res = koskari_eval(agen->ctx, &ev);
    agen->in_progress = 0;

    {
      PyObject *pending = KosDynYieldFrom_PENDING_ARG(yf);
      KosDynYieldFrom_PENDING_ARG(yf) = NULL;
      KosDynYieldFrom_PENDING_ACTION(yf) = KOS_YF_STEP;
      if (action == KOS_YF_THROW && pending)
        Py_DECREF(pending);
    }

    if (res < 0) {
      agen->closed = 1;
      kos_dyn_pop_yield_from(ctx, yf);
      return -1;
    }

    switch (ev.kind) {
    case KOS_EV_YIELD:
      if (KosStackBlock_PushRef(&KosDynFrame_STACK(frame), ev.payload) < 0) {
        Py_DECREF(ev.payload);
        return -1;
      }
      Py_DECREF(ev.payload);
      return yield_from_emit_yield(ctx, frame, out_ev, insn);

    case KOS_EV_RETURN: {
      PyObject *payload = ev.payload ? ev.payload : Py_None;
      Py_INCREF(payload);
      if (ev.payload)
        Py_DECREF(ev.payload);
      return yield_from_emit_return(ctx, yf, payload, out_ev);
    }

    case KOS_EV_AWAIT:
      Py_INCREF(ev.payload);
      KosDynYieldFrom_INFLIGHT(yf) = ev.payload;
      yield_from_rollback_pc(ctx, insn);
      out_ev->kind = KOS_EV_AWAIT;
      out_ev->payload = ev.payload;
      KosContext_FLAGS(ctx) &= ~KOS_CTX_RUNNING;
      KosContext_FLAGS(ctx) |= KOS_CTX_DEAD;
      return 1;

    case KOS_EV_RAISE:
      agen->closed = 1;
      kos_dyn_pop_yield_from(ctx, yf);
      if (ev.payload) {
        PyErr_SetObject((PyObject *) Py_TYPE(ev.payload), ev.payload);
        Py_DECREF(ev.payload);
      } else {
        PyErr_SetNone(PyExc_StopAsyncIteration);
      }
      return -1;

    default:
      agen->closed = 1;
      kos_dyn_pop_yield_from(ctx, yf);
      PyErr_SetString(PyExc_RuntimeError, "unexpected event from Koskari async gen");
      return -1;
    }
  }

  return op_unimplemented_stub(ctx, out_ev, insn);
}


typedef enum {
  GET_NEXT_VALUE,
  GET_NEXT_EXHAUSTED,
  GET_NEXT_SUSPEND,
  GET_NEXT_ERROR
} KosGetNextResult;


/**
   Get next value from a Python async iterator using the given __anext__
   awaitable. Caller must have obtained awaitable via get_await_next(iter);
   this helper consumes it (DECREF). *result_out is set with a new
   reference on GET_NEXT_VALUE.
*/
static KosGetNextResult next_from_python_async_iter(
    KosContext *ctx,
    KosEvent *out_ev,
    PyObject *iter,
    PyObject **result_out,
    PyObject *awaitable) {

  (void) iter;
  int suspended = 0;
  PyObject *yielded = NULL;
  PyObject *result = eager_step_awaitable(awaitable, ctx, &suspended,
                                          &yielded);
  Py_DECREF(awaitable);
  if (suspended && yielded) {
    KosContext_FLAGS(ctx) &= ~KOS_CTX_RUNNING;
    out_ev->kind = KOS_EV_AWAIT;
    out_ev->payload = yielded;
    return GET_NEXT_SUSPEND;
  }
  if (PyErr_ExceptionMatches(PyExc_StopAsyncIteration)) {
    PyErr_Clear();
    return GET_NEXT_EXHAUSTED;
  }
  if (result) {
    *result_out = result;
    return GET_NEXT_VALUE;
  }
  return GET_NEXT_ERROR;
}


/**
   Get next value from a Python sync iterator (PyIter_Next).
   *result_out is set with a new reference on GET_NEXT_VALUE.
*/
static KosGetNextResult next_from_python_sync_iter(
    KosContext *ctx,
    KosEvent *out_ev,
    PyObject *iter,
    PyObject **result_out) {

  (void) ctx;
  (void) out_ev;
  PyObject *n = PyIter_Next(iter);
  if (n) {
    *result_out = n;
    return GET_NEXT_VALUE;
  }
  if (PyErr_Occurred())
    return GET_NEXT_ERROR;
  return GET_NEXT_EXHAUSTED;
}


/**
   Dispatcher: get next from iter (Koskari async gen, Python async iter,
   or Python sync iter). Does not push; does not perform GetIter.
   *result_out set only on GET_NEXT_VALUE (caller pushes and DECREF).
*/
static KosGetNextResult op_get_next_from_iter(
    KosContext *ctx,
    KosEvent *out_ev,
    PyObject *iter,
    PyObject **result_out) {

  /* Koskari async gen is handled in NEXT/ITER_NEXT via in-loop attach; never here. */

  PyObject *awaitable = get_await_next(iter);
  if (awaitable) {
    KosGetNextResult r = next_from_python_async_iter(ctx, out_ev, iter,
                                                      result_out, awaitable);
    return r;
  }

  if (PyIter_Check(iter))
    return next_from_python_sync_iter(ctx, out_ev, iter, result_out);

  PyErr_SetString(PyExc_TypeError, "not an iterator");
  return GET_NEXT_ERROR;
}


/**
   Performs gen_frame validation, push Py_None, kos_dyn_attach_frame.
   Caller must have already: checked in_progress, handled closed/ctx.
   Caller must have set KosContext_PENDING_AGEN(ctx) and mode-specific fields before
   calling. On error: clears pending_agen, resets in_progress; for NEXT
   mode also DECREFs iter and default_val.
*/
static int op_attach_koskari_agen_frame(
    KosContext *ctx,
    KosPyAsyncGen *agen,
    PyObject *iter,
    PyObject *default_val) {

  KosDynFrame *gen_frame = KosContext_FRAME_TOP(agen->ctx);
  if (! gen_frame) {
    KosContext_PENDING_AGEN(ctx) = NULL;
    Py_XDECREF(KosContext_PENDING_AGEN_DEFAULT(ctx));
    KosContext_PENDING_AGEN_DEFAULT(ctx) = NULL;
    Py_DECREF(iter);
    Py_DECREF(default_val);
    agen->in_progress = 0;
    PyErr_SetString(PyExc_RuntimeError, "async generator has no frame");
    return -1;
  }
  if (KosStackBlock_PushRef(&KosDynFrame_STACK(gen_frame), Py_None) < 0) {
    KosContext_PENDING_AGEN(ctx) = NULL;
    Py_XDECREF(KosContext_PENDING_AGEN_DEFAULT(ctx));
    KosContext_PENDING_AGEN_DEFAULT(ctx) = NULL;
    Py_DECREF(iter);
    Py_DECREF(default_val);
    agen->in_progress = 0;
    return -1;
  }
  if (kos_dyn_attach_frame(ctx, gen_frame) < 0) {
    (void) KosStackBlock_PopRef(&KosDynFrame_STACK(gen_frame));
    KosContext_PENDING_AGEN(ctx) = NULL;
    Py_XDECREF(KosContext_PENDING_AGEN_DEFAULT(ctx));
    KosContext_PENDING_AGEN_DEFAULT(ctx) = NULL;
    Py_DECREF(iter);
    Py_DECREF(default_val);
    agen->in_progress = 0;
    return -1;
  }
  return 0;
}


/**
   ITER_NEXT variant: attach gen frame; on error clear context pending_agen.
*/
static int op_attach_koskari_agen_frame_for_iter_next(
    KosContext *ctx,
    KosPyAsyncGen *agen,
    KosDynRegion *region) {

  KosDynFrame *gen_frame = KosContext_FRAME_TOP(agen->ctx);
  if (! gen_frame) {
    agen->in_progress = 0;
    PyErr_SetString(PyExc_RuntimeError, "async generator has no frame");
    return -1;
  }
  if (KosStackBlock_PushRef(&KosDynFrame_STACK(gen_frame), Py_None) < 0) {
    agen->in_progress = 0;
    return -1;
  }
  if (kos_dyn_attach_frame(ctx, gen_frame) < 0) {
    (void) KosStackBlock_PopRef(&KosDynFrame_STACK(gen_frame));
    agen->in_progress = 0;
    return -1;
  }
  return 0;
}


/**
   NEXT: arg = count (1 or 2). Pops iterator (and optionally default) from
   stack, advances, pushes value or default on StopIteration. 1-arg: let
   StopIteration raise. 2-arg: TOS = default, below = iterator. Auto-await.
*/
KOS_OP_HANDLER(NEXT) {
  uint16_t n = insn->arg;
  if (n != 1 && n != 2) {
    PyErr_Format(PyExc_ValueError, "NEXT arg must be 1 or 2, got %u", n);
    return -1;
  }
  PyObject *default_val = NULL;
  PyObject *iter = NULL;
  if (n == 2) {
    default_val = KosStackBlock_PopRef(&KosDynFrame_STACK(KosContext_FRAME_TOP(ctx)));
    if (! default_val)
      return -1;
  }
  iter = KosStackBlock_PopRef(&KosDynFrame_STACK(KosContext_FRAME_TOP(ctx)));
  if (! iter) {
    Py_XDECREF(default_val);
    return -1;
  }
  /* If not already an iterator, get one (NEXT allows iterables). */
  if (! KosPyAsyncGen_Check(iter)) {
    PyObject *awaitable = get_await_next(iter);
    if (awaitable) {
      Py_DECREF(awaitable);
    } else if (! PyIter_Check(iter)) {
      PyObject *it = PyObject_GetIter(iter);
      Py_DECREF(iter);
      if (! it) {
        Py_XDECREF(default_val);
        return -1;
      }
      iter = it;
    }
  }

  /* Koskari async gen: flatten into same eval loop (attach gen frame, return 0). */
  if (KosPyAsyncGen_Check(iter)) {
    KosPyAsyncGen *agen = (KosPyAsyncGen *) iter;
    if (agen->in_progress) {
      PyErr_SetString(PyExc_RuntimeError,
                      "async generator already being iterated");
      Py_DECREF(iter);
      Py_XDECREF(default_val);
      return -1;
    }
    if (agen->closed || ! agen->ctx) {
      Py_DECREF(iter);
      if (n == 2 && default_val) {
        if (KosStackBlock_PushRef(&KosDynFrame_STACK(KosContext_FRAME_TOP(ctx)), default_val) < 0) {
          Py_XDECREF(default_val);
          return -1;
        }
        Py_XDECREF(default_val);
        return 0;
      }
      Py_XDECREF(default_val);
      PyErr_SetNone(PyExc_StopIteration);
      return -1;
    }
    agen->in_progress = 1;
    KosContext_PENDING_AGEN(ctx) = (void *) agen;
    if (default_val) {
      Py_INCREF(default_val);
      KosContext_PENDING_AGEN_DEFAULT(ctx) = default_val;
    } else {
      KosContext_PENDING_AGEN_DEFAULT(ctx) = NULL;
    }
    KosContext_PENDING_AGEN_N(ctx) = (int) n;
    if (op_attach_koskari_agen_frame(ctx, agen, iter, default_val) < 0)
      return -1;
    Py_DECREF(iter);
    Py_XDECREF(default_val);
    return 0;
  }

  PyObject *value = NULL;
  KosGetNextResult get_res = op_get_next_from_iter(ctx, out_ev, iter, &value);
  switch (get_res) {
    case GET_NEXT_VALUE:
      if (KosStackBlock_PushRef(&KosDynFrame_STACK(KosContext_FRAME_TOP(ctx)), value) < 0) {
        Py_DECREF(value);
        Py_DECREF(iter);
        Py_XDECREF(default_val);
        return -1;
      }
      Py_DECREF(value);
      Py_DECREF(iter);
      Py_XDECREF(default_val);
      return 0;
    case GET_NEXT_EXHAUSTED:
      Py_DECREF(iter);
      if (default_val) {
        if (KosStackBlock_PushRef(&KosDynFrame_STACK(KosContext_FRAME_TOP(ctx)), default_val) < 0) {
          Py_XDECREF(default_val);
          return -1;
        }
        Py_XDECREF(default_val);
        return 0;
      }
      Py_XDECREF(default_val);
      PyErr_SetNone(PyExc_StopIteration);
      return -1;
    case GET_NEXT_SUSPEND:
      if (n == 2) {
        if (KosStackBlock_PushRef(&KosDynFrame_STACK(KosContext_FRAME_TOP(ctx)), default_val) < 0) {
          Py_DECREF(default_val);
          Py_DECREF(iter);
          return -1;
        }
      }
      if (KosStackBlock_PushRef(&KosDynFrame_STACK(KosContext_FRAME_TOP(ctx)), iter) < 0) {
        Py_DECREF(iter);
        return -1;
      }
      Py_DECREF(iter);
      Py_XDECREF(default_val);
      return 1;
    case GET_NEXT_ERROR:
      Py_DECREF(iter);
      Py_XDECREF(default_val);
      return -1;
    default:
      Py_DECREF(iter);
      Py_XDECREF(default_val);
      return -1;
  }
}


/**
   ITER_NEXT: arg = else PC. Must be inside PUSH_FOREACH block. Uses
   KosDynRegion_ITER(region), advances, pushes next or jumps to else. Auto-await.
*/
KOS_OP_HANDLER(ITER_NEXT) {
  KosDynRegion *region = kos_find_region_for_loop(ctx);
  if (! region || KosDynRegion_STYLE(region) != KOS_REGION_FOREACH) {
    PyErr_SetString(PyExc_RuntimeError,
                    "ITER_NEXT must be used inside a PUSH_FOREACH block");
    return -1;
  }
  PyObject *iter = KosDynRegion_ITER(region);
  if (KosPyAsyncGen_Check(iter)) {
    KosPyAsyncGen *agen = (KosPyAsyncGen *) iter;
    if (agen->in_progress) {
      PyErr_SetString(PyExc_RuntimeError,
                      "async generator already being iterated");
      return -1;
    }
    if (agen->closed || ! agen->ctx) {
      KosDynRegion_EXHAUSTED(region) = 1;
      KosDynFrame_PC(KosContext_FRAME_TOP(ctx)) = insn->arg;
      return 0;
    }
    agen->in_progress = 1;
    KosContext_PENDING_AGEN(ctx) = (void *) agen;
    KosContext_PENDING_AGEN_DEFAULT(ctx) = NULL;
    KosContext_PENDING_AGEN_N(ctx) = 0;
    KosDynRegion_ITER_NEXT_ELSE_PC(region) = insn->arg;
    if (op_attach_koskari_agen_frame_for_iter_next(ctx, agen, region) < 0) {
      agen->in_progress = 0;
      KosContext_PENDING_AGEN(ctx) = NULL;
      return -1;
    }
    return 0;
  }
  PyObject *value = NULL;
  KosGetNextResult get_res = op_get_next_from_iter(ctx, out_ev, iter, &value);
  switch (get_res) {
    case GET_NEXT_VALUE:
      if (KosStackBlock_PushRef(&KosDynFrame_STACK(KosContext_FRAME_TOP(ctx)), value) < 0) {
        Py_DECREF(value);
        return -1;
      }
      Py_DECREF(value);
      return 0;
    case GET_NEXT_EXHAUSTED:
      KosDynRegion_EXHAUSTED(region) = 1;
      KosDynFrame_PC(KosContext_FRAME_TOP(ctx)) = insn->arg;
      return 0;
    case GET_NEXT_SUSPEND:
      return 1;
    case GET_NEXT_ERROR:
      return -1;
    default:
      return -1;
  }
}


/**
   JUMP: arg = target PC. Unconditional branch.
*/
KOS_OP_HANDLER(JUMP) {
  KosDynFrame_PC(KosContext_FRAME_TOP(ctx)) = insn->arg;
  (void) out_ev;
  return 0;
}


/**
   JUMP_FALSE: arg = target PC. Branch if TOS is false.
*/
KOS_OP_HANDLER(JUMP_FALSE) {
  PyObject *val = KosStackBlock_PopRef(&KosDynFrame_STACK(KosContext_FRAME_TOP(ctx)));
  if (! val)
    return -1;
  int res = PyObject_Not(val);
  Py_DECREF(val);
  if (res < 0)
    return -1;
  if (res)
    KosDynFrame_PC(KosContext_FRAME_TOP(ctx)) = insn->arg;
  (void) out_ev;
  return 0;
}


/**
   JUMP_TRUE: arg = target PC. Branch if TOS is true.
*/
KOS_OP_HANDLER(JUMP_TRUE) {
  PyObject *val = KosStackBlock_PopRef(&KosDynFrame_STACK(KosContext_FRAME_TOP(ctx)));
  if (! val)
    return -1;
  int res = PyObject_IsTrue(val);
  Py_DECREF(val);
  if (res < 0)
    return -1;
  if (res)
    KosDynFrame_PC(KosContext_FRAME_TOP(ctx)) = insn->arg;
  (void) out_ev;
  return 0;
}


/**
   JUMP_AND: arg = target PC. Short-circuit and: if TOS is falsy, leave on
   stack and jump; else pop and continue.
*/
KOS_OP_HANDLER(JUMP_AND) {
  PyObject *val = KosStackBlock_PopRef(&KosDynFrame_STACK(KosContext_FRAME_TOP(ctx)));
  if (! val)
    return -1;
  int res = PyObject_Not(val);
  if (res < 0) {
    Py_DECREF(val);
    return -1;
  }
  if (res) {
    if (KosStackBlock_PushRef(&KosDynFrame_STACK(KosContext_FRAME_TOP(ctx)), val) < 0) {
      Py_DECREF(val);
      return -1;
    }
    Py_DECREF(val);
    KosDynFrame_PC(KosContext_FRAME_TOP(ctx)) = insn->arg;
  } else {
    Py_DECREF(val);
  }
  (void) out_ev;
  return 0;
}


/**
   JUMP_OR: arg = target PC. Short-circuit or: if TOS is truthy, leave on
   stack and jump; else pop and continue.
*/
KOS_OP_HANDLER(JUMP_OR) {
  PyObject *val = KosStackBlock_PopRef(&KosDynFrame_STACK(KosContext_FRAME_TOP(ctx)));
  if (! val)
    return -1;
  int res = PyObject_IsTrue(val);
  if (res < 0) {
    Py_DECREF(val);
    return -1;
  }
  if (res) {
    if (KosStackBlock_PushRef(&KosDynFrame_STACK(KosContext_FRAME_TOP(ctx)), val) < 0) {
      Py_DECREF(val);
      return -1;
    }
    Py_DECREF(val);
    KosDynFrame_PC(KosContext_FRAME_TOP(ctx)) = insn->arg;
  } else {
    Py_DECREF(val);
  }
  (void) out_ev;
  return 0;
}


/**
   CHECK_EQ: arg = target PC. Branch if comparison fails (==).
*/
KOS_OP_HANDLER(CHECK_EQ) {
  return kos_check_cmp(ctx, out_ev, insn, Py_EQ);
}


/**
   CHECK_GE: arg = target PC. Branch if comparison fails (>=).
*/
KOS_OP_HANDLER(CHECK_GE) {
  return kos_check_cmp(ctx, out_ev, insn, Py_GE);
}


/**
   CHECK_GT: arg = target PC. Branch if comparison fails (>).
*/
KOS_OP_HANDLER(CHECK_GT) {
  return kos_check_cmp(ctx, out_ev, insn, Py_GT);
}


/**
   CHECK_LE: arg = target PC. Branch if comparison fails (<=).
*/
KOS_OP_HANDLER(CHECK_LE) {
  return kos_check_cmp(ctx, out_ev, insn, Py_LE);
}


/**
   CHECK_LT: arg = target PC. Branch if comparison fails (<).
*/
KOS_OP_HANDLER(CHECK_LT) {
  return kos_check_cmp(ctx, out_ev, insn, Py_LT);
}


/**
   CHECK_NE: arg = target PC. Branch if comparison fails (!=).
*/
KOS_OP_HANDLER(CHECK_NE) {
  return kos_check_cmp(ctx, out_ev, insn, Py_NE);
}


/**
   CHECK_IN: arg = target PC. Branch if "in" check fails. Stack: container, value (TOS).
*/
KOS_OP_HANDLER(CHECK_IN) {
  PyObject *value = KosStackBlock_PopRef(&KosDynFrame_STACK(KosContext_FRAME_TOP(ctx)));
  if (! value)
    return -1;
  PyObject *container = KosStackBlock_PopRef(&KosDynFrame_STACK(KosContext_FRAME_TOP(ctx)));
  if (! container) {
    Py_DECREF(value);
    return -1;
  }
  int cmp = PySequence_Contains(container, value);
  Py_DECREF(value);
  Py_DECREF(container);
  if (unlikely(cmp < 0))
    return -1;
  if (cmp == 0)
    KosDynFrame_PC(KosContext_FRAME_TOP(ctx)) = insn->arg;
  (void) out_ev;
  return 0;
}


/**
   CHECK_NOT_IN: arg = target PC. Branch if "not in" check fails. Stack: container, value (TOS).
*/
KOS_OP_HANDLER(CHECK_NOT_IN) {
  PyObject *value = KosStackBlock_PopRef(&KosDynFrame_STACK(KosContext_FRAME_TOP(ctx)));
  if (! value)
    return -1;
  PyObject *container = KosStackBlock_PopRef(&KosDynFrame_STACK(KosContext_FRAME_TOP(ctx)));
  if (! container) {
    Py_DECREF(value);
    return -1;
  }
  int cmp = PySequence_Contains(container, value);
  Py_DECREF(value);
  Py_DECREF(container);
  if (unlikely(cmp < 0))
    return -1;
  if (cmp == 1)
    KosDynFrame_PC(KosContext_FRAME_TOP(ctx)) = insn->arg;
  (void) out_ev;
  return 0;
}


/**
   CHECK_IS: arg = target PC. Branch if "is" check fails. Stack: left, right.
*/
KOS_OP_HANDLER(CHECK_IS) {
  PyObject *right = KosStackBlock_PopRef(&KosDynFrame_STACK(KosContext_FRAME_TOP(ctx)));
  if (! right)
    return -1;
  PyObject *left = KosStackBlock_PopRef(&KosDynFrame_STACK(KosContext_FRAME_TOP(ctx)));
  if (! left) {
    Py_DECREF(right);
    return -1;
  }
  int cmp = (left == right);
  Py_DECREF(left);
  Py_DECREF(right);
  if (cmp == 0)
    KosDynFrame_PC(KosContext_FRAME_TOP(ctx)) = insn->arg;
  (void) out_ev;
  return 0;
}


/**
   CHECK_IS_NOT: arg = target PC. Branch if "is not" check fails. Stack: left, right.
*/
KOS_OP_HANDLER(CHECK_IS_NOT) {
  PyObject *right = KosStackBlock_PopRef(&KosDynFrame_STACK(KosContext_FRAME_TOP(ctx)));
  if (! right)
    return -1;
  PyObject *left = KosStackBlock_PopRef(&KosDynFrame_STACK(KosContext_FRAME_TOP(ctx)));
  if (! left) {
    Py_DECREF(right);
    return -1;
  }
  int cmp = (left == right);
  Py_DECREF(left);
  Py_DECREF(right);
  if (cmp == 1)
    KosDynFrame_PC(KosContext_FRAME_TOP(ctx)) = insn->arg;
  (void) out_ev;
  return 0;
}


/**
   CHECK_IS_INSTANCE: arg = target PC. Branch if isinstance check fails.
   Stack: value, class_or_tuple.
*/
KOS_OP_HANDLER(CHECK_IS_INSTANCE) {
  PyObject *class_or_tuple = KosStackBlock_PopRef(&KosDynFrame_STACK(KosContext_FRAME_TOP(ctx)));
  if (! class_or_tuple)
    return -1;
  PyObject *value = KosStackBlock_PopRef(&KosDynFrame_STACK(KosContext_FRAME_TOP(ctx)));
  if (! value) {
    Py_DECREF(class_or_tuple);
    return -1;
  }
  int res = PyObject_IsInstance(value, class_or_tuple);
  Py_DECREF(value);
  Py_DECREF(class_or_tuple);
  if (unlikely(res < 0))
    return -1;
  if (res == 0)
    KosDynFrame_PC(KosContext_FRAME_TOP(ctx)) = insn->arg;
  (void) out_ev;
  return 0;
}


/**
   CHECK_IS_SUBCLASS: arg = target PC. Branch if issubclass check fails.
   Stack: class, class_or_tuple.
*/
KOS_OP_HANDLER(CHECK_IS_SUBCLASS) {
  PyObject *class_or_tuple = KosStackBlock_PopRef(&KosDynFrame_STACK(KosContext_FRAME_TOP(ctx)));
  if (! class_or_tuple)
    return -1;
  PyObject *cls = KosStackBlock_PopRef(&KosDynFrame_STACK(KosContext_FRAME_TOP(ctx)));
  if (! cls) {
    Py_DECREF(class_or_tuple);
    return -1;
  }
  int res = PyObject_IsSubclass(cls, class_or_tuple);
  Py_DECREF(cls);
  Py_DECREF(class_or_tuple);
  if (unlikely(res < 0))
    return -1;
  if (res == 0)
    KosDynFrame_PC(KosContext_FRAME_TOP(ctx)) = insn->arg;
  (void) out_ev;
  return 0;
}


/**
   VAR_BIND: arg = constant index (name). Bind name in current namespace to
   the value at TOS (pop value, then bind).
*/
KOS_OP_HANDLER(VAR_BIND) {
  PyObject *name = get_name_from_insn(KosDynFrame_CODE(KosContext_FRAME_TOP(ctx)), insn);
  if (! name)
    return -1;
  PyObject *val = KosStackBlock_PopRef(&KosDynFrame_STACK(KosContext_FRAME_TOP(ctx)));
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
  PyObject *name = get_name_from_insn(KosDynFrame_CODE(KosContext_FRAME_TOP(ctx)), insn);
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
  PyObject *name = get_name_from_insn(KosDynFrame_CODE(KosContext_FRAME_TOP(ctx)), insn);
  if (! name)
    return -1;
  PyObject *ref = frame_ns_ref(ctx, name);
  if (! ref)
    return -1;
  Py_INCREF(ref);
  return push_result(ctx, ref);
}


/**
   VAR_STORE: arg = constant index (name). Pop TOS and store into name in
   the current frame namespace.
*/
KOS_OP_HANDLER(VAR_STORE) {
  PyObject *name = get_name_from_insn(KosDynFrame_CODE(KosContext_FRAME_TOP(ctx)), insn);
  if (! name)
    return -1;
  PyObject *val = KosStackBlock_PopRef(&KosDynFrame_STACK(KosContext_FRAME_TOP(ctx)));
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
  PyObject *ns = KosDynFrame_NS(KosContext_FRAME_TOP(ctx));
  if (! KosNamespace_SupportsFast(ns)) {
    PyErr_Format(PyExc_RuntimeError, "FAST invoked on %R", ns);
    return -1;
  }
  size_t idx = (size_t) insn->arg;
  PyObject *val = KosNamespace_FastDeref(ns, idx);
  if (! val)
    return -1;
  return push_result(ctx, val);
}


/**
   FAST_BIND: arg = slot index. Bind name to fast slot.
*/
KOS_OP_HANDLER(FAST_BIND) {
  PyObject *ns = KosDynFrame_NS(KosContext_FRAME_TOP(ctx));
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
  PyObject *val = KosStackBlock_PopRef(&KosDynFrame_STACK(KosContext_FRAME_TOP(ctx)));
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
   FAST_CLEAR: arg = slot index. Clear fast slot.
*/
KOS_OP_HANDLER(FAST_CLEAR) {
  PyObject *ns = KosDynFrame_NS(KosContext_FRAME_TOP(ctx));
  if (! KosNamespace_SupportsFast(ns)) {
    PyErr_Format(PyExc_RuntimeError, "FAST_CLEAR invoked on %R", ns);
    return -1;
  }
  if (KosNamespace_FastClear(ns, (size_t) insn->arg) < 0)
    return -1;
  (void) out_ev;
  return 0;
}


/**
   FAST_REF: arg = slot index. Push reference to fast slot.
*/
KOS_OP_HANDLER(FAST_REF) {
  PyObject *ns = KosDynFrame_NS(KosContext_FRAME_TOP(ctx));
  if (! KosNamespace_SupportsFast(ns)) {
    PyErr_Format(PyExc_RuntimeError, "FAST_REF invoked on %R", ns);
    return -1;
  }
  size_t idx = (size_t) insn->arg;
  PyObject *ref = KosNamespace_FastRef(ns, idx);
  if (! ref)
    return -1;
  Py_INCREF(ref);
  return push_result(ctx, ref);
}


/**
   FAST_STORE: arg = slot index. Store TOS into fast slot.
*/
KOS_OP_HANDLER(FAST_STORE) {
  PyObject *ns = KosDynFrame_NS(KosContext_FRAME_TOP(ctx));
  if (! KosNamespace_SupportsFast(ns)) {
    PyErr_Format(PyExc_RuntimeError, "FAST_STORE invoked on %R", ns);
    return -1;
  }
  size_t idx = (size_t) insn->arg;
  PyObject *val = KosStackBlock_PopRef(&KosDynFrame_STACK(KosContext_FRAME_TOP(ctx)));
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
   ATTR: arg = constant index (name). Push getattr(TOS, name).
*/
KOS_OP_HANDLER(ATTR) {
  PyObject *name = get_name_from_insn(KosDynFrame_CODE(KosContext_FRAME_TOP(ctx)), insn);
  if (! name)
    return -1;
  PyObject *obj = KosStackBlock_PopRef(&KosDynFrame_STACK(KosContext_FRAME_TOP(ctx)));
  if (! obj)
    return -1;
  PyObject *val = PyObject_GetAttr(obj, name);
  Py_DECREF(obj);
  if (! val)
    return -1;
  return push_result(ctx, val);
}


/**
   ATTR_CLEAR: arg = constant index (name). delattr(TOS, name).
*/
KOS_OP_HANDLER(ATTR_CLEAR) {
  PyObject *name = get_name_from_insn(KosDynFrame_CODE(KosContext_FRAME_TOP(ctx)), insn);
  if (! name)
    return -1;
  PyObject *obj = KosStackBlock_PopRef(&KosDynFrame_STACK(KosContext_FRAME_TOP(ctx)));
  if (! obj)
    return -1;
  int res = PyObject_DelAttr(obj, name);
  Py_DECREF(obj);
  if (res < 0)
    return -1;
  (void) out_ev;
  return 0;
}


/**
   ATTR_REF: arg = constant index (name). Push attribute reference.
*/
KOS_OP_HANDLER(ATTR_REF) {
  PyObject *name = get_name_from_insn(KosDynFrame_CODE(KosContext_FRAME_TOP(ctx)), insn);
  if (! name)
    return -1;
  PyObject *obj = KosStackBlock_PopRef(&KosDynFrame_STACK(KosContext_FRAME_TOP(ctx)));
  if (! obj)
    return -1;
  PyObject *ref = KosAttributeRef_New(name, obj, name);
  Py_DECREF(obj);
  if (! ref)
    return -1;
  return push_result(ctx, ref);
}


/**
   ATTR_STORE: arg = constant index (name). setattr(TOS1, name, TOS); pop val then obj.
*/
KOS_OP_HANDLER(ATTR_STORE) {
  PyObject *name = get_name_from_insn(KosDynFrame_CODE(KosContext_FRAME_TOP(ctx)), insn);
  if (! name)
    return -1;
  PyObject *val = KosStackBlock_PopRef(&KosDynFrame_STACK(KosContext_FRAME_TOP(ctx)));
  if (! val)
    return -1;
  PyObject *obj = KosStackBlock_PopRef(&KosDynFrame_STACK(KosContext_FRAME_TOP(ctx)));
  if (! obj) {
    Py_DECREF(val);
    return -1;
  }
  int res = PyObject_SetAttr(obj, name, val);
  Py_DECREF(obj);
  Py_DECREF(val);
  if (res < 0)
    return -1;
  (void) out_ev;
  return 0;
}


/**
   ITEM: arg unused. Push TOS1[TOS] (subscript). Stack: obj, key (TOS).
*/
KOS_OP_HANDLER(ITEM) {
  PyObject *key = KosStackBlock_PopRef(&KosDynFrame_STACK(KosContext_FRAME_TOP(ctx)));
  if (! key)
    return -1;
  PyObject *obj = KosStackBlock_PopRef(&KosDynFrame_STACK(KosContext_FRAME_TOP(ctx)));
  if (! obj) {
    Py_DECREF(key);
    return -1;
  }
  PyObject *item = PyObject_GetItem(obj, key);
  Py_DECREF(obj);
  Py_DECREF(key);
  if (! item)
    return -1;
  return push_result(ctx, item);
}


/**
   ITEM_CLEAR: arg unused. del TOS1[TOS]. Stack: obj, key (TOS).
*/
KOS_OP_HANDLER(ITEM_CLEAR) {
  PyObject *key = KosStackBlock_PopRef(&KosDynFrame_STACK(KosContext_FRAME_TOP(ctx)));
  if (! key)
    return -1;
  PyObject *obj = KosStackBlock_PopRef(&KosDynFrame_STACK(KosContext_FRAME_TOP(ctx)));
  if (! obj) {
    Py_DECREF(key);
    return -1;
  }
  int res = PyObject_DelItem(obj, key);
  Py_DECREF(obj);
  Py_DECREF(key);
  if (res < 0)
    return -1;
  (void) out_ev;
  return 0;
}


/**
   ITEM_REF: arg unused. Push item reference for subscription. Stack: obj, key (TOS).
*/
KOS_OP_HANDLER(ITEM_REF) {
  PyObject *key = KosStackBlock_PopRef(&KosDynFrame_STACK(KosContext_FRAME_TOP(ctx)));
  if (! key)
    return -1;
  PyObject *obj = KosStackBlock_PopRef(&KosDynFrame_STACK(KosContext_FRAME_TOP(ctx)));
  if (! obj) {
    Py_DECREF(key);
    return -1;
  }
  PyObject *ref = KosItemRef_New(key, obj, key);
  Py_DECREF(obj);
  Py_DECREF(key);
  if (! ref)
    return -1;
  return push_result(ctx, ref);
}


/**
   ITEM_STORE: arg unused. TOS2[TOS1] = TOS; pop val, key, obj.
*/
KOS_OP_HANDLER(ITEM_STORE) {
  PyObject *val = KosStackBlock_PopRef(&KosDynFrame_STACK(KosContext_FRAME_TOP(ctx)));
  if (! val)
    return -1;
  PyObject *key = KosStackBlock_PopRef(&KosDynFrame_STACK(KosContext_FRAME_TOP(ctx)));
  if (! key) {
    Py_DECREF(val);
    return -1;
  }
  PyObject *obj = KosStackBlock_PopRef(&KosDynFrame_STACK(KosContext_FRAME_TOP(ctx)));
  if (! obj) {
    Py_DECREF(key);
    Py_DECREF(val);
    return -1;
  }
  int res = PyObject_SetItem(obj, key, val);
  Py_DECREF(obj);
  Py_DECREF(key);
  Py_DECREF(val);
  if (res < 0)
    return -1;
  (void) out_ev;
  return 0;
}


/**
   CONST_ITEM: arg = constant index (key). Push TOS[key].
*/
KOS_OP_HANDLER(CONST_ITEM) {
  PyObject *key = KosCode_GetConstant((PyObject *) KosDynFrame_CODE(KosContext_FRAME_TOP(ctx)),
                                      insn->arg);
  if (! key)
    return -1;
  PyObject *obj = KosStackBlock_PopRef(&KosDynFrame_STACK(KosContext_FRAME_TOP(ctx)));
  if (! obj)
    return -1;
  PyObject *item = PyObject_GetItem(obj, key);
  Py_DECREF(obj);
  if (! item)
    return -1;
  return push_result(ctx, item);
}


/**
   CONST_ITEM_CLEAR: arg = constant index (key). del TOS[key].
*/
KOS_OP_HANDLER(CONST_ITEM_CLEAR) {
  PyObject *key = KosCode_GetConstant((PyObject *) KosDynFrame_CODE(KosContext_FRAME_TOP(ctx)),
                                      insn->arg);
  if (! key)
    return -1;
  PyObject *obj = KosStackBlock_PopRef(&KosDynFrame_STACK(KosContext_FRAME_TOP(ctx)));
  if (! obj)
    return -1;
  int res = PyObject_DelItem(obj, key);
  Py_DECREF(obj);
  if (res < 0)
    return -1;
  (void) out_ev;
  return 0;
}


/**
   CONST_ITEM_REF: arg = constant index (key). Push constant-key item reference.
*/
KOS_OP_HANDLER(CONST_ITEM_REF) {
  PyObject *key = KosCode_GetConstant((PyObject *) KosDynFrame_CODE(KosContext_FRAME_TOP(ctx)),
                                      insn->arg);
  if (! key)
    return -1;
  PyObject *obj = KosStackBlock_PopRef(&KosDynFrame_STACK(KosContext_FRAME_TOP(ctx)));
  if (! obj)
    return -1;
  PyObject *ref = KosItemRef_New(key, obj, key);
  Py_DECREF(obj);
  if (! ref)
    return -1;
  return push_result(ctx, ref);
}


/**
   CONST_ITEM_STORE: arg = constant index (key). TOS1[key] = TOS; pop val then obj.
*/
KOS_OP_HANDLER(CONST_ITEM_STORE) {
  PyObject *key = KosCode_GetConstant((PyObject *) KosDynFrame_CODE(KosContext_FRAME_TOP(ctx)),
                                      insn->arg);
  if (! key)
    return -1;
  PyObject *val = KosStackBlock_PopRef(&KosDynFrame_STACK(KosContext_FRAME_TOP(ctx)));
  if (! val)
    return -1;
  PyObject *obj = KosStackBlock_PopRef(&KosDynFrame_STACK(KosContext_FRAME_TOP(ctx)));
  if (! obj) {
    Py_DECREF(val);
    return -1;
  }
  int res = PyObject_SetItem(obj, key, val);
  Py_DECREF(obj);
  Py_DECREF(val);
  if (res < 0)
    return -1;
  (void) out_ev;
  return 0;
}


/**
   BUILD_ARGS: arg unused. Build positional args tuple from stack; not yet implemented.
*/
KOS_OP_HANDLER(BUILD_ARGS) {
  return op_unimplemented_stub(ctx, out_ev, insn);
}


/**
   BUILD_FUNCTION: arg = N (number of keyword default values).
   Stack: ... fspec [, val_1 ... val_N, names_tuple ]. Pop fspec, then if
   N > 0 pop N values and names tuple into a defaults dict. Build function
   via KosFunction_New(fspec, defaults, frame_ns) and push result.
*/
KOS_OP_HANDLER(BUILD_FUNCTION) {
  unsigned opa = insn->arg;
  KosStackBlock **stack = &KosDynFrame_STACK(KosContext_FRAME_TOP(ctx));

  PyObject *fspec = KosStackBlock_PopRef(stack);
  if (! fspec)
    return -1;

  PyObject *defaults = NULL;
  if (opa > 0) {
    defaults = KosStackBlock_PopTailDictNames(stack, (size_t) opa);
    if (! defaults) {
      Py_DECREF(fspec);
      return -1;
    }
  }

  PyObject *parent_ns = KosDynFrame_NS(KosContext_FRAME_TOP(ctx));
  PyObject *fn = KosFunction_New(fspec, defaults, parent_ns);
  Py_DECREF(fspec);
  Py_XDECREF(defaults);
  if (! fn)
    return -1;

  if (KosStackBlock_PushRef(stack, fn) < 0) {
    Py_DECREF(fn);
    return -1;
  }
  Py_DECREF(fn);

  (void) out_ev;
  return 0;
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
   PUSH_FOREACH: arg = break target PC. Stack: [loop_result, iterable].
   Pops iterable and loop_result, creates iterator, stores both in region,
   pushes FOREACH region and stack block. Continue target = next insn.
*/
KOS_OP_HANDLER(PUSH_FOREACH) {
  uint32_t break_pc = insn->arg;
  uint32_t continue_pc = KosDynFrame_PC(KosContext_FRAME_TOP(ctx));  /* already advanced by drive loop */

  /* Pop iterable (TOS) and loop_result from stack */
  PyObject *iterable = KosStackBlock_PopRef(&KosDynFrame_STACK(KosContext_FRAME_TOP(ctx)));
  if (! iterable) {
    PyErr_SetString(PyExc_RuntimeError, "PUSH_FOREACH: missing iterable");
    return -1;
  }
  PyObject *loop_result = KosStackBlock_PopRef(&KosDynFrame_STACK(KosContext_FRAME_TOP(ctx)));
  if (! loop_result) {
    Py_DECREF(iterable);
    PyErr_SetString(PyExc_RuntimeError, "PUSH_FOREACH: missing loop_result");
    return -1;
  }

  /* Create iterator from iterable (sync or async) */
  PyObject *iter = NULL;
  if (KosPyAsyncGen_Check(iterable)) {
    iter = iterable;
    Py_INCREF(iter);
    Py_DECREF(iterable);
  } else if (Py_TYPE(iterable)->tp_as_async &&
             Py_TYPE(iterable)->tp_as_async->am_aiter) {
    iter = Py_TYPE(iterable)->tp_as_async->am_aiter(iterable);
    Py_DECREF(iterable);
  } else {
    iter = PyObject_GetIter(iterable);
    Py_DECREF(iterable);
  }
  if (! iter) {
    Py_DECREF(loop_result);
    return -1;
  }

  /* Push region */
  KosDynRegion *region = kos_dyn_push_region(ctx, KOS_REGION_FOREACH,
                                             break_pc, continue_pc);
  if (! region) {
    Py_DECREF(iter);
    Py_DECREF(loop_result);
    PyErr_NoMemory();
    return -1;
  }

  /* Store result and iterator in region (takes ownership) */
  KosDynRegion_RESULT(region) = loop_result;
  KosDynRegion_ITER(region) = iter;
  KosDynRegion_EXHAUSTED(region) = 0;

  /* Push stack block for iteration temporaries */
  if (KosStackBlock_PushBlock(&KosDynFrame_STACK(KosContext_FRAME_TOP(ctx))) < 0) {
    kos_dyn_pop_region(ctx, region);
    return -1;
  }

  (void) out_ev;
  return 0;
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
   PUSH_LOOP: arg = break target PC. Push LOOP region and stack block;
   continue target = next instruction. Initializes region result to None.
*/
KOS_OP_HANDLER(PUSH_LOOP) {
  uint32_t break_pc = insn->arg;
  uint32_t continue_pc = KosDynFrame_PC(KosContext_FRAME_TOP(ctx));  /* already advanced by drive loop */
  KosDynRegion *region = kos_dyn_push_region(ctx, KOS_REGION_LOOP,
                                             break_pc, continue_pc);
  if (! region) {
    PyErr_NoMemory();
    return -1;
  }

  /* Initialize loop result to None */
  Py_INCREF(Py_None);
  KosDynRegion_RESULT(region) = Py_None;

  if (KosStackBlock_PushBlock(&KosDynFrame_STACK(KosContext_FRAME_TOP(ctx))) < 0) {
    kos_dyn_pop_region(ctx, region);
    return -1;
  }
  (void) out_ev;
  return 0;
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
  PyObject *parent = KosDynFrame_NS(KosContext_FRAME_TOP(ctx));
  PyObject *new_ns = NULL;
  KosDynNs *ns_node = NULL;

  if (opa < 0) {
    PyErr_Format(PyExc_RuntimeError,
                 "PUSH_NS called with negative count: %i", opa);
    return -1;
  }

  if (opa == 0) {
    nspec = KosStackBlock_PopRef(&KosDynFrame_STACK(KosContext_FRAME_TOP(ctx)));
    if (! nspec) {
      PyErr_SetString(PyExc_RuntimeError, "PUSH_NS stack underrun");
      return -1;
    }
  } else {
    names = KosStackBlock_PopRef(&KosDynFrame_STACK(KosContext_FRAME_TOP(ctx)));
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
    args = KosStackBlock_TailPtr(KosDynFrame_STACK(KosContext_FRAME_TOP(ctx)), (size_t) opa + 1);
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
    KosStackBlock_DropNoErr(&KosDynFrame_STACK(KosContext_FRAME_TOP(ctx)), (size_t) opa + 1);
    Py_DECREF(names);
  }

  Py_INCREF(parent);
  ns_node = kos_dyn_push_ns(ctx, parent);
  if (! ns_node) {
    Py_DECREF(parent);
    Py_DECREF(new_ns);
    return -1;
  }

  Py_DECREF(KosDynFrame_NS(KosContext_FRAME_TOP(ctx)));
  KosDynFrame_NS(KosContext_FRAME_TOP(ctx)) = new_ns;

  (void) out_ev;
  return 0;
}


/**
   CLEAR_LOOP: arg unused. Disable the top LOOP/FOREACH region (used when
   exiting normally before POP_LOOP).
*/
KOS_OP_HANDLER(CLEAR_LOOP) {
  if (! KosContext_DYN_TOP(ctx) || ! KosDynNode_IS_REGION(KosContext_DYN_TOP(ctx))) {
    PyErr_SetString(PyExc_RuntimeError, "CLEAR_LOOP: top is not a region");
    return -1;
  }
  KosDynRegion *region = (KosDynRegion *) KosContext_DYN_TOP(ctx);
  if (KosDynRegion_STYLE(region) != KOS_REGION_LOOP && KosDynRegion_STYLE(region) != KOS_REGION_FOREACH) {
    PyErr_SetString(PyExc_RuntimeError,
                    "CLEAR_LOOP: top is not a LOOP or FOREACH region");
    return -1;
  }
  KosDynRegion_ENABLED(region) = 0;
  (void) out_ev;
  (void) insn;
  return 0;
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
   POP_LOOP: arg unused. Pop LOOP/FOREACH region, drop stack block,
   and push KosDynRegion_RESULT(region) to the operand stack as the loop expression result.
*/
KOS_OP_HANDLER(POP_LOOP) {
  if (! KosContext_DYN_TOP(ctx) || ! KosDynNode_IS_REGION(KosContext_DYN_TOP(ctx))) {
    PyErr_SetString(PyExc_RuntimeError, "POP_LOOP: top is not a region");
    return -1;
  }
  KosDynRegion *region = (KosDynRegion *) KosContext_DYN_TOP(ctx);
  if (KosDynRegion_STYLE(region) != KOS_REGION_LOOP && KosDynRegion_STYLE(region) != KOS_REGION_FOREACH) {
    PyErr_SetString(PyExc_RuntimeError,
                    "POP_LOOP: top is not a LOOP or FOREACH region");
    return -1;
  }

  /* Get result before popping region (region owns the reference) */
  PyObject *result = KosDynRegion_RESULT(region);
  Py_INCREF(result);

  /* Drop the stack block (both LOOP and FOREACH now push blocks) */
  if (KosStackBlock_DropBlock(&KosDynFrame_STACK(KosContext_FRAME_TOP(ctx))) < 0) {
    Py_DECREF(result);
    return -1;
  }

  /* Pop the region (this DECREFs KosDynRegion_RESULT(region)) */
  kos_dyn_pop_region(ctx, region);

  /* Push result to stack as loop expression value */
  if (KosStackBlock_PushRef(&KosDynFrame_STACK(KosContext_FRAME_TOP(ctx)), result) < 0) {
    Py_DECREF(result);
    return -1;
  }
  Py_DECREF(result);

  (void) out_ev;
  (void) insn;
  return 0;
}


/**
   POP_NS: arg unused. Verify dyn_top is KOS_DYN_NS, restore frame ns from
   saved_ns, pop the NS node.
*/
KOS_OP_HANDLER(POP_NS) {
  if (! KosContext_DYN_TOP(ctx) || ! KosDynNode_IS_NS(KosContext_DYN_TOP(ctx))) {
    PyErr_SetString(PyExc_RuntimeError, "POP_NS: top is not a namespace scope");
    return -1;
  }

  KosDynNs *ns_node = (KosDynNs *) KosContext_DYN_TOP(ctx);
  PyObject *prior = KosDynNs_SAVED_NS(ns_node);
  Py_INCREF(prior);
  Py_DECREF(KosDynFrame_NS(KosContext_FRAME_TOP(ctx)));
  KosDynFrame_NS(KosContext_FRAME_TOP(ctx)) = prior;
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
    KosContext_FLAGS(ctx) &= ~KOS_CTX_RUNNING;
    KosContext_FLAGS(ctx) |= KOS_CTX_DEAD;
    return 0;
  }

  uint32_t handler_pc = KosDynRegion_BREAK_PC(region);
  {
    KosTryHandler th;
    if (kos_try_handler_for((PyObject *) KosDynFrame_CODE(KosContext_FRAME_TOP(ctx)),
                            KosDynFrame_PC(KosContext_FRAME_TOP(ctx)), &th) == 0) {
      if (KosDynRegion_STYLE(region) == KOS_REGION_FINALLY && th.finally_pc != KOS_EXTENT_NONE) {
        handler_pc = th.finally_pc;
      }
      else if (th.except_pc != KOS_EXTENT_NONE) {
        handler_pc = th.except_pc;
      }
    }
  }

  KosDynFrame_PC(KosContext_FRAME_TOP(ctx)) = handler_pc;
  KosDynRegion_ENABLED(region) = 0;

  /* Set aside exception in excinfo and clear so handler runs without PyErr set */
  {
    PyObject *typ = NULL, *val = NULL, *tb = NULL;
    PyErr_Fetch(&typ, &val, &tb);
    PyErr_SetExcInfo(typ, val, tb);
    PyErr_Clear();
  }

  if (KosDynRegion_STYLE(region) == KOS_REGION_FINALLY)
    KosDynRegion_HANDLING_RAISE(region) = 1;

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
  KosContext_FLAGS(ctx) |= KOS_CTX_RUNNING;

  /* Push token onto active stack and increment live_depth */
  push_active_token(KosContext_TOKEN(ctx));
  KosEvaluatorToken_IncrementLiveDepth(KosContext_TOKEN(ctx));

  /* Handle pending exception injection */
  if (KosContext_INJECT_MODE(ctx) == KOS_INJECT_THROW && KosContext_INJECT_VALUE(ctx)) {
    PyErr_SetObject((PyObject *) Py_TYPE(KosContext_INJECT_VALUE(ctx)), KosContext_INJECT_VALUE(ctx));
    Py_CLEAR(KosContext_INJECT_VALUE(ctx));
    KosContext_INJECT_MODE(ctx) = KOS_INJECT_NONE;

    out_ev->kind = KOS_EV_RAISE;
    out_ev->payload = NULL;
    PyErr_Fetch(&out_ev->payload, NULL, NULL);

    KosContext_FLAGS(ctx) &= ~KOS_CTX_RUNNING;
    KosContext_FLAGS(ctx) |= KOS_CTX_DEAD;
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
    KosContext_FLAGS(ctx) &= ~KOS_CTX_RUNNING;
    KosContext_FLAGS(ctx) |= KOS_CTX_DEAD;
    eval_ret = 0;
    goto koskari_eval_cleanup;
  }

  if (! KosContext_FRAME_TOP(ctx)) {
    out_ev->kind = KOS_EV_RETURN;
    out_ev->payload = Py_None;
    Py_INCREF(Py_None);
    KosContext_FLAGS(ctx) &= ~KOS_CTX_RUNNING;
    KosContext_FLAGS(ctx) |= KOS_CTX_DEAD;
    eval_ret = 0;
    goto koskari_eval_cleanup;
  }

  /* Re-entry: inject value from completed Python await or send() when resuming from yield.
     Push onto frame stack so the next opcode sees it. Do not clear RESUME_KIND here so
     YIELD_FROM can detect inject and pop TOS into its dyn node; YIELD_FROM clears KIND. */
  if (KosContext_PY_RESUME_INJECT(ctx) && KosContext_PY_RESUME_KIND(ctx) != KOS_RESUME_NONE) {
    if (KosStackBlock_PushRef(&KosDynFrame_STACK(KosContext_FRAME_TOP(ctx)), KosContext_PY_RESUME_INJECT(ctx)) < 0) {
      Py_DECREF(KosContext_PY_RESUME_INJECT(ctx));
      KosContext_PY_RESUME_INJECT(ctx) = NULL;
      KosContext_PY_RESUME_KIND(ctx) = KOS_RESUME_NONE;
      eval_ret = -1;
      goto koskari_eval_cleanup;
    }
    Py_CLEAR(KosContext_PY_RESUME_INJECT(ctx));
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
    if (KosContext_PLAN(ctx) && run_transfer_plan_to_completion(ctx, out_ev) < 0) {
      eval_ret = 0;
      goto koskari_eval_cleanup;
    }

    if (PyErr_Occurred()) {
      if (KoskariContinuationApply
          && PyErr_ExceptionMatches(KoskariContinuationApply)) {
        int r = handle_continuation_apply(ctx);
        if (r == 1)
          continue;
        eval_ret = -1;
        goto koskari_eval_cleanup;
      }
      clear_pending_agen_on_error(ctx);
      if (try_local_exception_handler(ctx, out_ev))
        continue;
      eval_ret = 0;
      goto koskari_eval_cleanup;
    }

    KosCode *code = KosDynFrame_CODE(KosContext_FRAME_TOP(ctx));
    uint32_t pc = KosDynFrame_PC(KosContext_FRAME_TOP(ctx));

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
    trace_stack("  before", KosDynFrame_STACK(KosContext_FRAME_TOP(ctx)));
    KosDynFrame_PC(KosContext_FRAME_TOP(ctx)) = pc + 1;

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
          clear_pending_agen_on_error(ctx);
          if (try_local_exception_handler(ctx, out_ev))
            continue;
          eval_ret = 0;
          goto koskari_eval_cleanup;
        }
        eval_ret = (op_res < 0) ? -1 : 0;
        goto koskari_eval_cleanup;
      }
    }
    /* Clear resume kind if still set (YIELD_FROM clears when it consumes; others leave it set) */
    if (KosContext_PY_RESUME_KIND(ctx) != KOS_RESUME_NONE)
      KosContext_PY_RESUME_KIND(ctx) = KOS_RESUME_NONE;
    trace_stack("  after ", KosDynFrame_STACK(KosContext_FRAME_TOP(ctx)));
  }

koskari_eval_cleanup:
  KosEvaluatorToken_DecrementLiveDepth(KosContext_TOKEN(ctx));
  pop_active_token(KosContext_TOKEN(ctx));
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
