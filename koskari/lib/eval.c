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
      wrapper (KosPyCoroutine or KosPyAsyncGen) during its lifetime.

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
#include "eval.h"
#include "bytecode.h"
#include <stdio.h>


/* Set to 1 to enable debug prints to stderr (opcode trace, CALL repr, etc.). */
#define KOS_ENABLE_DEBUG 0

#if KOS_ENABLE_DEBUG
# define KOS_DEBUGF(...) \
  do { fprintf(stderr, __VA_ARGS__); fflush(stderr); } while (0)
static void _kos_debug_repr(const char *prefix, PyObject *obj) {
  if (! obj) {
    fprintf(stderr, "%sNULL\n", prefix);
    return;
  }
  PyObject *repr = PyObject_Repr(obj);
  if (repr) {
    const char *s = PyUnicode_AsUTF8(repr);
    fprintf(stderr, "%s%s\n", prefix, s ? s : "<repr failed>");
    Py_DECREF(repr);
  } else {
    fprintf(stderr, "%s<repr error>\n", prefix);
    PyErr_Clear();
  }
  fflush(stderr);
}
# define KOS_DEBUGF_REPR(prefix, obj) _kos_debug_repr(prefix, obj)
#else
# define KOS_DEBUGF(...) do { (void)0; } while (0)
# define KOS_DEBUGF_REPR(prefix, obj) ((void)(prefix), (void)(obj))
#endif

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

/**
   Run the current transfer plan to completion.  Returns 0 on success (plan
   cleared or was NULL).  Returns -1 if a step failed (out_ev set to RAISE,
   caller should return 0 from kos_drive).
*/
static int run_transfer_plan_to_completion(KosContext *ctx, KosEvent *out_ev) {
  while (ctx->plan) {
    KOS_DEBUGF("[eval] transfer step %zu/%zu\n",
               (size_t) ctx->plan->action_index,
               (size_t) ctx->plan->action_count);
    int plan_res = kos_run_transfer_step(ctx);
    if (plan_res < 0) {
      KOS_DEBUGF("[eval] transfer step failed -> KOS_EV_RAISE\n");
      out_ev->kind = KOS_EV_RAISE;
      out_ev->payload = NULL;
      PyErr_Fetch(&out_ev->payload, NULL, NULL);
      ctx->flags &= ~KOS_CTX_RUNNING;
      ctx->flags |= KOS_CTX_DEAD;
      return -1;
    }
    if (plan_res == 0)
      continue;
    KOS_DEBUGF("[eval] transfer complete inv_seg_top=%p\n",
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
  if (! KOS_ENABLE_DEBUG)
    return;
  fprintf(stderr, "[eval] %s stack=%p", prefix, (void *) stack);
  if (stack) {
    size_t n = KosStackBlock_LEN(stack);
    fprintf(stderr, " len=%zu", n);
    for (size_t i = (n >= 2 ? n - 2 : 0); i < n; i++) {
      PyObject *o = stack->objs[i];
      if (PyLong_Check(o))
        fprintf(stderr, " [%zu]=%ld", i, PyLong_AsLong(o));
    }
  }
  fprintf(stderr, "\n");
  fflush(stderr);
}


/* === Static string constants === */

static PyObject *str_await = NULL;
static PyObject *str_send = NULL;
static PyObject *str_throw = NULL;
static PyObject *str_close = NULL;


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


/* === Run Code as coroutine (Python entry point) === */


/**
   Create a coroutine that runs the given Code with the given namespace.

   The coroutine is not started; the caller drives it via send(None) or
   await. code must be a KosCode; ns is the initial frame namespace
   (e.g. a KosNamespace or dict), reference is borrowed.

   :return: new KosPyCoroutine, or NULL on error
*/
PyObject *kos_run_code_as_coroutine(PyObject *code, PyObject *ns) {

  if (! KosCode_Check(code)) {
    PyErr_Format(PyExc_TypeError, "expected Code, got %s",
                 Py_TYPE(code)->tp_name);
    return NULL;
  }

  if (! ns) {
    PyErr_SetString(PyExc_TypeError, "namespace is required");
    return NULL;
  }

  Py_INCREF(ns);
  KosDynFrame *frame = kos_dynframe_new(NULL, (KosCode *) code, ns, NULL);
  if (! frame)
    return NULL;

  KosContext *ctx = kos_context_new(frame);
  if (! ctx) {
    kos_dynframe_free(frame);
    return NULL;
  }

  return KosPyCoroutine_New(ctx);
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

  if (! str_await) {
    str_await = PyUnicode_InternFromString("__await__");
    if (! str_await)
      return NULL;
  }

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

  KOS_DEBUGF("[eval] eager_step_awaitable before send(None) (await_iter=%d)\n",
             await_iter ? 1 : 0);

  /* Try first send(None) */
  if (! str_send) {
    str_send = PyUnicode_InternFromString("send");
    if (! str_send) {
      Py_DECREF(await_iter);
      return NULL;
    }
  }

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
  KOS_DEBUGF("[eval] push_or_await entry is_awaitable=%d\n",
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
    KOS_DEBUGF("[eval] push_or_await set KOS_EV_AWAIT (suspended=%d yielded=%d)\n",
               suspended, yielded ? 1 : 0);
    return 1;
  }

  return -1;
}


/* === Opcode handlers (always inlined; return 0=continue 1=stop -1=error) === */


static int op_unimplemented_stub(KosContext *ctx, KosEvent *out_ev, KosOp *insn) {
  (void) ctx;
  (void) out_ev;
  PyErr_Format(PyExc_RuntimeError, "unimplemented opcode %u",
               (unsigned) insn->op);
  return -1;
}


KOS_OP_INLINE int op_NOOP(KosContext *ctx, KosEvent *out_ev, KosOp *insn) {
  (void) ctx;
  (void) out_ev;
  (void) insn;
  return 0;  /* continue */
}


KOS_OP_INLINE int op_LABEL(KosContext *ctx, KosEvent *out_ev, KosOp *insn) {
  (void) ctx;
  (void) out_ev;
  (void) insn;
  return 0;
}


KOS_OP_INLINE int op_POSITION(KosContext *ctx, KosEvent *out_ev, KosOp *insn) {
  (void) ctx;
  (void) out_ev;
  (void) insn;
  return 0;
}


KOS_OP_INLINE int op_CONST(KosContext *ctx, KosEvent *out_ev, KosOp *insn) {
  KosCode *code = ctx->frame_top->code;
  PyObject *val = KosCode_GetConstant((PyObject *) code, insn->arg);
  if (! val) {
    KOS_DEBUGF("[eval] CONST GetConstant failed arg=%u\n",
               (unsigned) insn->arg);
    return -1;
  }
  if (PyLong_Check(val))
    KOS_DEBUGF("[eval] CONST pushed %ld\n", PyLong_AsLong(val));
  if (KosStackBlock_PushRef(&ctx->frame_top->stack, val) < 0) {
    Py_DECREF(val);
    return -1;
  }
  Py_DECREF(val);
  (void) out_ev;
  return 0;
}


KOS_OP_INLINE int op_DUP(KosContext *ctx, KosEvent *out_ev, KosOp *insn) {
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


KOS_OP_INLINE int op_POP(KosContext *ctx, KosEvent *out_ev, KosOp *insn) {
  unsigned n = insn->arg;
  for (unsigned i = 0; i < n; i++) {
    PyObject *val = KosStackBlock_PopRef(&ctx->frame_top->stack);
    if (! val)
      return -1;
    Py_DECREF(val);
  }
  (void) out_ev;
  return 0;
}


KOS_OP_INLINE int op_RETURN(KosContext *ctx, KosEvent *out_ev, KosOp *insn) {
  PyObject *val = KosStackBlock_PopRef(&ctx->frame_top->stack);
  if (! val) {
    KOS_DEBUGF("[eval] RETURN pop underrun\n");
    return -1;
  }
  if (PyLong_Check(val))
    KOS_DEBUGF("[eval] RETURN value=%ld -> KOS_EV_RETURN\n",
               PyLong_AsLong(val));
  out_ev->kind = KOS_EV_RETURN;
  out_ev->payload = val;
  ctx->flags &= ~KOS_CTX_RUNNING;
  ctx->flags |= KOS_CTX_DEAD;
  (void) insn;
  return 1;
}


KOS_OP_INLINE int op_AWAIT(KosContext *ctx, KosEvent *out_ev, KosOp *insn) {
  (void) insn;
  PyObject *val = KosStackBlock_PopRef(&ctx->frame_top->stack);
  if (! val)
    return -1;
  return push_or_await(ctx, out_ev, val);
}


KOS_OP_INLINE int op_CALL(KosContext *ctx, KosEvent *out_ev, KosOp *insn) {
  unsigned n = insn->arg;
  PyObject *callable = KosStackBlock_PopRef(&ctx->frame_top->stack);
  if (! callable)
    return -1;
  PyObject *tuple = NULL;
  PyObject *result = NULL;
  PyObject *const *args = KosStackBlock_TailPtr(ctx->frame_top->stack, n);
  if (! args && n > 0) {
    Py_DECREF(callable);
    return -1;
  }
  tuple = PyTuple_New((Py_ssize_t) n);
  if (! tuple) {
    Py_DECREF(callable);
    return -1;
  }
  for (unsigned i = 0; i < n; i++) {
    Py_INCREF(args[i]);
    if (PyTuple_SetItem(tuple, (Py_ssize_t) i, args[i]) < 0) {
      Py_DECREF(tuple);
      Py_DECREF(callable);
      return -1;
    }
  }
  KosStackBlock_DropNoErr(&ctx->frame_top->stack, n);
  KOS_DEBUGF("[CALL] callable: ");
  KOS_DEBUGF_REPR("", callable);
  result = PyObject_Call(callable, tuple, NULL);
  KOS_DEBUGF("[CALL] result: ");
  KOS_DEBUGF_REPR("", result);
  if (result && Py_TYPE(result)->tp_as_async)
    KOS_DEBUGF("[CALL] result is awaitable, passing to push_or_await\n");
  KOS_DEBUGF("[eval] op_CALL after PyObject_Call (result=%d awaitable=%d)\n",
             result ? 1 : 0, result && Py_TYPE(result)->tp_as_async ? 1 : 0);
  Py_DECREF(tuple);
  Py_DECREF(callable);
  if (! result)
    return -1;
  return push_or_await(ctx, out_ev, result);
}


KOS_OP_INLINE int op_GLOBALS(KosContext *ctx, KosEvent *out_ev, KosOp *insn) {
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
  if (KosStackBlock_PushRef(&ctx->frame_top->stack, ns) < 0) {
    Py_DECREF(ns);
    return -1;
  }
  Py_DECREF(ns);
  (void) out_ev;
  (void) insn;
  return 0;
}


KOS_OP_INLINE int op_LOCALS(KosContext *ctx, KosEvent *out_ev, KosOp *insn) {
  PyObject *ns = ctx->frame_top->ns;
  Py_INCREF(ns);
  if (KosStackBlock_PushRef(&ctx->frame_top->stack, ns) < 0) {
    Py_DECREF(ns);
    return -1;
  }
  Py_DECREF(ns);
  (void) out_ev;
  (void) insn;
  return 0;
}


KOS_OP_INLINE int op_VAR(KosContext *ctx, KosEvent *out_ev, KosOp *insn) {
  KosCode *code = ctx->frame_top->code;
  PyObject *name = KosCode_GetConstant((PyObject *) code, insn->arg);
  if (! name)
    return -1;
  PyObject *ns = ctx->frame_top->ns;
  PyObject *val = NULL;
  if (KosNamespace_Check(ns)) {
    val = KosNamespace_Deref(ns, name);
  } else if (PyDict_Check(ns)) {
    val = PyDict_GetItemWithError(ns, name);
    if (val)
      Py_INCREF(val);
    else if (! PyErr_Occurred())
      PyErr_Format(PyExc_NameError, "name %R is not defined", name);
  } else {
    PyErr_Format(PyExc_TypeError, "frame namespace must be Namespace or dict,"
                 " got %R", ns);
    return -1;
  }
  if (! val)
    return -1;
  return push_or_await(ctx, out_ev, val);
}


KOS_OP_INLINE int op_EXCEPTION(KosContext *ctx, KosEvent *out_ev, KosOp *insn) {
  PyObject *typ = NULL, *val = NULL, *tb = NULL;
  PyErr_GetExcInfo(&typ, &val, &tb);

  if (! typ) {
    if (KosStackBlock_PushRef(&ctx->frame_top->stack, Py_None) < 0)
      return -1;
    (void) out_ev;
    (void) insn;
    return 0;
  }

  if (! val)
    PyErr_NormalizeException(&typ, &val, &tb);

  Py_DECREF(typ);
  Py_XDECREF(tb);

  if (KosStackBlock_PushRef(&ctx->frame_top->stack, val) < 0) {
    Py_DECREF(val);
    return -1;
  }
  Py_DECREF(val);
  (void) out_ev;
  (void) insn;
  return 0;
}


KOS_OP_INLINE int op_EXCEPTION_CLEAR(KosContext *ctx, KosEvent *out_ev,
                                      KosOp *insn) {
  PyErr_Clear();
  PyErr_SetExcInfo(NULL, NULL, NULL);
  (void) ctx;
  (void) out_ev;
  (void) insn;
  return 0;
}


KOS_OP_INLINE int op_EXCEPTION_MATCH(KosContext *ctx, KosEvent *out_ev,
                                      KosOp *insn) {
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
    if (KosStackBlock_PushRef(&ctx->frame_top->stack, ret) < 0) {
      Py_DECREF(ret);
      return -1;
    }
    Py_DECREF(ret);
    (void) out_ev;
    (void) insn;
    return 0;
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
  if (KosStackBlock_PushRef(&ctx->frame_top->stack, ret) < 0) {
    Py_DECREF(ret);
    return -1;
  }
  Py_DECREF(ret);
  (void) out_ev;
  (void) insn;
  return 0;
}


KOS_OP_INLINE int op_RAISE(KosContext *ctx, KosEvent *out_ev, KosOp *insn) {
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


KOS_OP_INLINE int op_CHECK_EXCEPTION(KosContext *ctx, KosEvent *out_ev,
                                     KosOp *insn) {
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


KOS_OP_INLINE int op_PUSH_PROMPT(KosContext *ctx, KosEvent *out_ev,
                                 KosOp *insn) {
  uint32_t prompt_id;
  if (get_prompt_id_from_insn(ctx->frame_top->code, insn, &prompt_id) < 0)
    return -1;
  if (! kos_dyn_push_prompt(ctx, prompt_id))
    return -1;
  (void) out_ev;
  return 0;
}


KOS_OP_INLINE int op_SET_PROMPT_HANDLER(KosContext *ctx, KosEvent *out_ev,
                                        KosOp *insn) {
  if (ctx->dyn_top && KosDynNode_IS_PROMPT(ctx->dyn_top)) {
    KosDynPrompt *p = (KosDynPrompt *) ctx->dyn_top;
    p->handler_code = ctx->frame_top->code;
    p->handler_pc = insn->arg;
  }
  (void) out_ev;
  return 0;
}


KOS_OP_INLINE int op_POP_PROMPT(KosContext *ctx, KosEvent *out_ev,
                                KosOp *insn) {
  KOS_DEBUGF("[eval] POP_PROMPT inv_seg=%p dyn_top=%p IS_PROMPT=%d\n",
             (void *) ctx->invocation_segment_top, (void *) ctx->dyn_top,
             (ctx->dyn_top && KosDynNode_IS_PROMPT(ctx->dyn_top)) ? 1 : 0);
  if (ctx->invocation_segment_top != NULL) {
    PyObject *val = KosStackBlock_PopRef(&ctx->frame_top->stack);
    if (! val) {
      KOS_DEBUGF("[eval] POP_PROMPT (invoke) pop underrun\n");
      return -1;
    }
    if (PyLong_Check(val))
      KOS_DEBUGF("[eval] POP_PROMPT return to invoker value=%ld\n",
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


KOS_OP_INLINE int op_PUSH_TRY(KosContext *ctx, KosEvent *out_ev,
                               KosOp *insn) {
  if (! kos_dyn_push_region(ctx, KOS_REGION_TRY, insn->arg, 0)) {
    PyErr_NoMemory();
    return -1;
  }
  (void) out_ev;
  return 0;
}


KOS_OP_INLINE int op_PUSH_FINALLY(KosContext *ctx, KosEvent *out_ev,
                                  KosOp *insn) {
  if (! kos_dyn_push_region(ctx, KOS_REGION_FINALLY, insn->arg, 0)) {
    PyErr_NoMemory();
    return -1;
  }
  (void) out_ev;
  return 0;
}


KOS_OP_INLINE int op_POP_TRY(KosContext *ctx, KosEvent *out_ev,
                             KosOp *insn) {
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


KOS_OP_INLINE int op_POP_FINALLY(KosContext *ctx, KosEvent *out_ev,
                                 KosOp *insn) {
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


KOS_OP_INLINE int op_CLEAR_TRY(KosContext *ctx, KosEvent *out_ev,
                               KosOp *insn) {
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


KOS_OP_INLINE int op_CLEAR_FINALLY(KosContext *ctx, KosEvent *out_ev,
                                   KosOp *insn) {
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


KOS_OP_INLINE int op_SHIFT(KosContext *ctx, KosEvent *out_ev, KosOp *insn) {
  uint32_t prompt_id;
  KosContinuation *k = NULL;
  if (get_prompt_id_from_insn(ctx->frame_top->code, insn, &prompt_id) < 0)
    return -1;
  KOS_DEBUGF("[eval] SHIFT prompt_id=%u\n", (unsigned) prompt_id);
  if (kos_shift_capture(ctx, prompt_id, &k) < 0) {
    KOS_DEBUGF("[eval] SHIFT capture failed\n");
    return -1;
  }
  KosDynPrompt *prompt = kos_find_prompt(ctx, prompt_id);
  if (! prompt) {
    kos_continuation_free(k);
    PyErr_SetString(PyExc_RuntimeError, "shift: prompt not found");
    return -1;
  }
  KOS_DEBUGF("[eval] SHIFT unwinding to prompt, then handler_pc=%u\n",
             (unsigned) prompt->handler_pc);
  while (ctx->dyn_top != (KosDynNode *) prompt) {
    if (KosDynNode_IS_FRAME(ctx->dyn_top)) {
      if (kos_dyn_pop_frame(ctx) < 0) {
        kos_continuation_free(k);
        return -1;
      }
    } else if (KosDynNode_IS_WIND(ctx->dyn_top)) {
      if (kos_dyn_pop_wind(ctx, (KosDynWind *) ctx->dyn_top) < 0) {
        kos_continuation_free(k);
        return -1;
      }
    } else if (KosDynNode_IS_REGION(ctx->dyn_top)) {
      kos_dyn_pop_region(ctx, (KosDynRegion *) ctx->dyn_top);
    } else {
      kos_continuation_free(k);
      PyErr_SetString(PyExc_RuntimeError,
                      "shift: cannot unwind past non-frame/wind/region");
      return -1;
    }
  }
  PyObject *wrapper = KosPyContinuation_New(k);
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
  if (KosStackBlock_PushRef(&ctx->frame_top->stack, wrapper) < 0) {
    Py_DECREF(wrapper);
    return -1;
  }
  Py_DECREF(wrapper);
  (void) out_ev;
  (void) handler_frame;
  return 0;
}


KOS_OP_INLINE int op_INVOKE_K(KosContext *ctx, KosEvent *out_ev, KosOp *insn) {
  PyObject *value = KosStackBlock_PopRef(&ctx->frame_top->stack);
  if (! value) {
    KOS_DEBUGF("[eval] INVOKE_K pop value failed\n");
    return -1;
  }
  PyObject *cont_obj = KosStackBlock_PopRef(&ctx->frame_top->stack);
  if (! cont_obj) {
    KOS_DEBUGF("[eval] INVOKE_K pop cont failed\n");
    Py_DECREF(value);
    return -1;
  }
  if (PyLong_Check(value))
    KOS_DEBUGF("[eval] INVOKE_K value=%ld k=%p\n",
               PyLong_AsLong(value), (void *) cont_obj);
  if (! PyObject_TypeCheck(cont_obj, &KosPyContinuationType)) {
    PyErr_SetString(PyExc_TypeError, "INVOKE_K: TOS is not a continuation");
    Py_DECREF(value);
    Py_DECREF(cont_obj);
    return -1;
  }
  KosContinuation *k = ((KosPyContinuation *) cont_obj)->captured;
  if (! k) {
    Py_DECREF(value);
    Py_DECREF(cont_obj);
    PyErr_SetString(PyExc_RuntimeError,
                    "INVOKE_K: continuation has no captured state");
    return -1;
  }
  KOS_DEBUGF("[eval] INVOKE_K calling kos_invoke_continuation captured_chain=%p\n",
             (void *) k->captured_chain);
  if (kos_invoke_continuation(ctx, k, value, 0, cont_obj) < 0) {
    KOS_DEBUGF("[eval] INVOKE_K kos_invoke_continuation returned -1\n");
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


KOS_OP_INLINE int op_OBJECT_ADD(KosContext *ctx, KosEvent *out_ev,
                                KosOp *insn) {
  return obj_op_reducing(ctx, out_ev, insn, KosOperator_Add);
}


#define OP_UNARY(name, func)                                            \
  KOS_OP_INLINE int op_##name(KosContext *ctx, KosEvent *out_ev,        \
                              KosOp *insn) {                            \
    PyObject *val = KosStackBlock_PopRef(&ctx->frame_top->stack);       \
    if (! val) return -1;                                              \
    PyObject *res = func(val);                                          \
    Py_DECREF(val);                                                    \
    if (! res) return -1;                                              \
    (void) insn;                                                       \
    return push_or_await(ctx, out_ev, res);                             \
  }

KOS_OP_INLINE int op_OBJECT_STR(KosContext *ctx, KosEvent *out_ev,
                                KosOp *insn) {
  return obj_op_reducing(ctx, out_ev, insn, KosOperator_BuildStr);
}

OP_UNARY(OBJECT_REPR, PyObject_Repr)
OP_UNARY(OBJECT_ITER, PyObject_GetIter)
OP_UNARY(OBJECT_BIT_INVERT, PyNumber_Invert)
OP_UNARY(OBJECT_DIR, PyObject_Dir)

#undef OP_UNARY


KOS_OP_INLINE int op_OBJECT_LEN(KosContext *ctx, KosEvent *out_ev,
                                KosOp *insn) {
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


KOS_OP_INLINE int op_OBJECT_NOT(KosContext *ctx, KosEvent *out_ev,
                                KosOp *insn) {
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


KOS_OP_INLINE int op_OBJECT_TRUTH(KosContext *ctx, KosEvent *out_ev,
                                 KosOp *insn) {
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


KOS_OP_INLINE int op_OBJECT_TYPE(KosContext *ctx, KosEvent *out_ev,
                                 KosOp *insn) {
  PyObject *val = KosStackBlock_PopRef(&ctx->frame_top->stack);
  if (! val)
    return -1;
  PyObject *res = (PyObject *) Py_TYPE(val);
  Py_INCREF(res);
  Py_DECREF(val);
  (void) insn;
  return push_or_await(ctx, out_ev, res);
}


KOS_OP_INLINE int op_OBJECT_VARS(KosContext *ctx, KosEvent *out_ev,
                                 KosOp *insn) {
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


KOS_OP_INLINE int op_OBJECT_AITER(KosContext *ctx, KosEvent *out_ev,
                                  KosOp *insn) {
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
  KOS_OP_INLINE int op_##name(KosContext *ctx, KosEvent *out_ev,        \
                              KosOp *insn) {                            \
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

KOS_OP_INLINE int op_OBJECT_SUBTRACT(KosContext *ctx, KosEvent *out_ev,
                                      KosOp *insn) {
  return obj_op_reducing(ctx, out_ev, insn, KosOperator_Subtract);
}
KOS_OP_INLINE int op_OBJECT_MULTIPLY(KosContext *ctx, KosEvent *out_ev,
                                      KosOp *insn) {
  return obj_op_reducing(ctx, out_ev, insn, KosOperator_Multiply);
}
KOS_OP_INLINE int op_OBJECT_FLOOR_DIVIDE(KosContext *ctx, KosEvent *out_ev,
                                          KosOp *insn) {
  return obj_op_reducing(ctx, out_ev, insn, KosOperator_FloorDivide);
}
KOS_OP_INLINE int op_OBJECT_TRUE_DIVIDE(KosContext *ctx, KosEvent *out_ev,
                                         KosOp *insn) {
  return obj_op_reducing(ctx, out_ev, insn, KosOperator_TrueDivide);
}
OP_BINARY_NUM(OBJECT_MODULO, PyNumber_Remainder)
OP_BINARY_NUM(OBJECT_DIVMOD, PyNumber_Divmod)
KOS_OP_INLINE int op_OBJECT_BIT_AND(KosContext *ctx, KosEvent *out_ev,
                                    KosOp *insn) {
  return obj_op_reducing(ctx, out_ev, insn, KosOperator_BitwiseAnd);
}
KOS_OP_INLINE int op_OBJECT_BIT_OR(KosContext *ctx, KosEvent *out_ev,
                                   KosOp *insn) {
  return obj_op_reducing(ctx, out_ev, insn, KosOperator_BitwiseOr);
}
KOS_OP_INLINE int op_OBJECT_BIT_XOR(KosContext *ctx, KosEvent *out_ev,
                                    KosOp *insn) {
  return obj_op_reducing(ctx, out_ev, insn, KosOperator_BitwiseXor);
}
KOS_OP_INLINE int op_OBJECT_MATRIX_MULTIPLY(KosContext *ctx, KosEvent *out_ev,
                                             KosOp *insn) {
  return obj_op_reducing(ctx, out_ev, insn, KosOperator_MatrixMultiply);
}

#undef OP_BINARY_NUM


KOS_OP_INLINE int op_OBJECT_POWER(KosContext *ctx, KosEvent *out_ev,
                                  KosOp *insn) {
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


KOS_OP_INLINE int op_OBJECT_BIT_LEFT(KosContext *ctx, KosEvent *out_ev,
                                     KosOp *insn) {
  PyObject *right = KosStackBlock_PopRef(&ctx->frame_top->stack);
  if (! right)
    return -1;
  PyObject *left = KosStackBlock_PopRef(&ctx->frame_top->stack);
  if (! left) {
    Py_DECREF(right);
    return -1;
  }
  PyObject *res = PyNumber_Lshift(left, right);
  Py_DECREF(left);
  Py_DECREF(right);
  if (! res)
    return -1;
  (void) insn;
  return push_or_await(ctx, out_ev, res);
}


KOS_OP_INLINE int op_OBJECT_BIT_RIGHT(KosContext *ctx, KosEvent *out_ev,
                                      KosOp *insn) {
  PyObject *right = KosStackBlock_PopRef(&ctx->frame_top->stack);
  if (! right)
    return -1;
  PyObject *left = KosStackBlock_PopRef(&ctx->frame_top->stack);
  if (! left) {
    Py_DECREF(right);
    return -1;
  }
  PyObject *res = PyNumber_Rshift(left, right);
  Py_DECREF(left);
  Py_DECREF(right);
  if (! res)
    return -1;
  (void) insn;
  return push_or_await(ctx, out_ev, res);
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

KOS_OP_INLINE int op_OBJECT_EQ(KosContext *ctx, KosEvent *out_ev,
                               KosOp *insn) {
  return op_OBJECT_EQ_impl(ctx, out_ev, insn, Py_EQ);
}
KOS_OP_INLINE int op_OBJECT_NE(KosContext *ctx, KosEvent *out_ev,
                               KosOp *insn) {
  return op_OBJECT_EQ_impl(ctx, out_ev, insn, Py_NE);
}
KOS_OP_INLINE int op_OBJECT_LT(KosContext *ctx, KosEvent *out_ev,
                               KosOp *insn) {
  return op_OBJECT_EQ_impl(ctx, out_ev, insn, Py_LT);
}
KOS_OP_INLINE int op_OBJECT_LE(KosContext *ctx, KosEvent *out_ev,
                               KosOp *insn) {
  return op_OBJECT_EQ_impl(ctx, out_ev, insn, Py_LE);
}
KOS_OP_INLINE int op_OBJECT_GT(KosContext *ctx, KosEvent *out_ev,
                               KosOp *insn) {
  return op_OBJECT_EQ_impl(ctx, out_ev, insn, Py_GT);
}
KOS_OP_INLINE int op_OBJECT_GE(KosContext *ctx, KosEvent *out_ev,
                               KosOp *insn) {
  return op_OBJECT_EQ_impl(ctx, out_ev, insn, Py_GE);
}


KOS_OP_INLINE int op_OBJECT_IN(KosContext *ctx, KosEvent *out_ev,
                               KosOp *insn) {
  PyObject *right = KosStackBlock_PopRef(&ctx->frame_top->stack);
  if (! right)
    return -1;
  PyObject *left = KosStackBlock_PopRef(&ctx->frame_top->stack);
  if (! left) {
    Py_DECREF(right);
    return -1;
  }
  int r = PySequence_Contains(right, left);
  Py_DECREF(left);
  Py_DECREF(right);
  if (r < 0)
    return -1;
  PyObject *res = (r ? Py_True : Py_False);
  Py_INCREF(res);
  (void) insn;
  return push_or_await(ctx, out_ev, res);
}


KOS_OP_INLINE int op_OBJECT_NOT_IN(KosContext *ctx, KosEvent *out_ev,
                                   KosOp *insn) {
  PyObject *right = KosStackBlock_PopRef(&ctx->frame_top->stack);
  if (! right)
    return -1;
  PyObject *left = KosStackBlock_PopRef(&ctx->frame_top->stack);
  if (! left) {
    Py_DECREF(right);
    return -1;
  }
  int r = PySequence_Contains(right, left);
  Py_DECREF(left);
  Py_DECREF(right);
  if (r < 0)
    return -1;
  PyObject *res = (r ? Py_False : Py_True);
  Py_INCREF(res);
  (void) insn;
  return push_or_await(ctx, out_ev, res);
}


KOS_OP_INLINE int op_OBJECT_IS(KosContext *ctx, KosEvent *out_ev,
                               KosOp *insn) {
  PyObject *right = KosStackBlock_PopRef(&ctx->frame_top->stack);
  if (! right)
    return -1;
  PyObject *left = KosStackBlock_PopRef(&ctx->frame_top->stack);
  if (! left) {
    Py_DECREF(right);
    return -1;
  }
  PyObject *res = (left == right ? Py_True : Py_False);
  Py_INCREF(res);
  Py_DECREF(left);
  Py_DECREF(right);
  (void) insn;
  return push_or_await(ctx, out_ev, res);
}


KOS_OP_INLINE int op_OBJECT_IS_NOT(KosContext *ctx, KosEvent *out_ev,
                                   KosOp *insn) {
  PyObject *right = KosStackBlock_PopRef(&ctx->frame_top->stack);
  if (! right)
    return -1;
  PyObject *left = KosStackBlock_PopRef(&ctx->frame_top->stack);
  if (! left) {
    Py_DECREF(right);
    return -1;
  }
  PyObject *res = (left != right ? Py_True : Py_False);
  Py_INCREF(res);
  Py_DECREF(left);
  Py_DECREF(right);
  (void) insn;
  return push_or_await(ctx, out_ev, res);
}


KOS_OP_INLINE int op_OBJECT_IS_INSTANCE(KosContext *ctx, KosEvent *out_ev,
                                        KosOp *insn) {
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


KOS_OP_INLINE int op_OBJECT_IS_SUBCLASS(KosContext *ctx, KosEvent *out_ev,
                                        KosOp *insn) {
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


/* Unimplemented opcodes: redirect to stub so op_handlers table compiles. */
#define op_THEEND              op_unimplemented_stub
/* op_AWAIT implemented above */
#define op_BREAK              op_unimplemented_stub
#define op_CONTINUE           op_unimplemented_stub
#define op_YIELD              op_unimplemented_stub
#define op_YIELD_FROM         op_unimplemented_stub
#define op_ITER_NEXT          op_unimplemented_stub
#define op_AWAIT_NEXT         op_unimplemented_stub
#define op_JUMP               op_unimplemented_stub
#define op_JUMP_FALSE         op_unimplemented_stub
#define op_JUMP_TRUE          op_unimplemented_stub
#define op_JUMP_AND           op_unimplemented_stub
#define op_JUMP_OR            op_unimplemented_stub
#define op_CHECK_EQ           op_unimplemented_stub
#define op_CHECK_GE           op_unimplemented_stub
#define op_CHECK_GT           op_unimplemented_stub
#define op_CHECK_LE           op_unimplemented_stub
#define op_CHECK_LT           op_unimplemented_stub
#define op_CHECK_NE           op_unimplemented_stub
#define op_CHECK_IN           op_unimplemented_stub
#define op_CHECK_NOT_IN       op_unimplemented_stub
#define op_CHECK_IS           op_unimplemented_stub
#define op_CHECK_IS_NOT       op_unimplemented_stub
#define op_CHECK_IS_INSTANCE  op_unimplemented_stub
#define op_CHECK_IS_SUBCLASS  op_unimplemented_stub
/* op_VAR implemented in eval.c (see op_VAR) */
#define op_VAR_BIND           op_unimplemented_stub
#define op_VAR_CLEAR          op_unimplemented_stub
#define op_VAR_REF            op_unimplemented_stub
#define op_VAR_STORE          op_unimplemented_stub
#define op_FAST               op_unimplemented_stub
#define op_FAST_BIND          op_unimplemented_stub
#define op_FAST_CLEAR         op_unimplemented_stub
#define op_FAST_REF           op_unimplemented_stub
#define op_FAST_STORE         op_unimplemented_stub
#define op_ATTR               op_unimplemented_stub
#define op_ATTR_CLEAR         op_unimplemented_stub
#define op_ATTR_REF           op_unimplemented_stub
#define op_ATTR_STORE         op_unimplemented_stub
#define op_ITEM               op_unimplemented_stub
#define op_ITEM_CLEAR         op_unimplemented_stub
#define op_ITEM_REF           op_unimplemented_stub
#define op_ITEM_STORE         op_unimplemented_stub
#define op_CONST_ITEM         op_unimplemented_stub
#define op_CONST_ITEM_CLEAR   op_unimplemented_stub
#define op_CONST_ITEM_REF     op_unimplemented_stub
#define op_CONST_ITEM_STORE   op_unimplemented_stub
#define op_BUILD_ARGS         op_unimplemented_stub
#define op_BUILD_FUNCTION     op_unimplemented_stub
#define op_BUILD_KWDS         op_unimplemented_stub
#define op_BUILD_KWDS_NAMES   op_unimplemented_stub
#define op_COMBINE_ARGS       op_unimplemented_stub
#define op_COMBINE_KWDS       op_unimplemented_stub
/* op_CALL implemented above */
#define op_CALL_NAMES         op_unimplemented_stub
#define op_CALL_STAR          op_unimplemented_stub
#define op_CALL_ATTR          op_unimplemented_stub
#define op_CALL_ATTR_NAMES    op_unimplemented_stub
#define op_CALL_ATTR_STAR     op_unimplemented_stub
#define op_RECUR              op_unimplemented_stub
#define op_RECUR_NAMES        op_unimplemented_stub
#define op_RECUR_STAR         op_unimplemented_stub
#define op_TAIL_CALL          op_unimplemented_stub
#define op_TAIL_CALL_NAMES    op_unimplemented_stub
#define op_TAIL_CALL_STAR     op_unimplemented_stub
#define op_TAIL_RECUR         op_unimplemented_stub
#define op_TAIL_RECUR_NAMES   op_unimplemented_stub
#define op_TAIL_RECUR_STAR    op_unimplemented_stub
#define op_PUSH_FOREACH       op_unimplemented_stub
#define op_PUSH_WITH          op_unimplemented_stub
#define op_PUSH_BEGIN         op_unimplemented_stub
#define op_PUSH_CLASS         op_unimplemented_stub
#define op_PUSH_LOOP          op_unimplemented_stub
#define op_PUSH_NS            op_unimplemented_stub
#define op_CLEAR_LOOP         op_unimplemented_stub
#define op_POP_WITH           op_unimplemented_stub
#define op_POP_BEGIN          op_unimplemented_stub
#define op_POP_LOOP           op_unimplemented_stub
#define op_POP_NS             op_unimplemented_stub
#define op_PUSH_WIND          op_unimplemented_stub
#define op_POP_WIND           op_unimplemented_stub
#define op_END_WIND           op_unimplemented_stub
#define op_PUSH_REGION        op_unimplemented_stub
#define op_POP_REGION         op_unimplemented_stub

/* Dispatch table generated from opcodes.incl; every op_##name must be
   defined (implemented or #define to op_unimplemented_stub). */
static int (*const op_handlers[])(KosContext *, KosEvent *, KosOp *) = {
#define OP(name, style) op_##name,
#include "opcodes.incl"
#undef OP
};


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

  ctx->frame_top->pc = region->a_label;
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


int kos_drive(KosContext *ctx, KosEvent *out_ev) {

  if (! ctx) {
    PyErr_SetString(PyExc_RuntimeError, "NULL context for kos_drive");
    return -1;
  }

  if (! out_ev) {
    PyErr_SetString(PyExc_RuntimeError, "NULL output event");
    return -1;
  }

  /* Mark context as running */
  ctx->flags |= KOS_CTX_RUNNING;

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

    return 0;
  }

  if (run_transfer_plan_to_completion(ctx, out_ev) < 0)
    return 0;

  if (kos_hook_active(ctx)) {
    /* Minimal: no hook execution; return */
    out_ev->kind = KOS_EV_RETURN;
    out_ev->payload = Py_None;
    Py_INCREF(Py_None);
    ctx->flags &= ~KOS_CTX_RUNNING;
    ctx->flags |= KOS_CTX_DEAD;
    return 0;
  }

  if (! ctx->frame_top) {
    out_ev->kind = KOS_EV_RETURN;
    out_ev->payload = Py_None;
    Py_INCREF(Py_None);
    ctx->flags &= ~KOS_CTX_RUNNING;
    ctx->flags |= KOS_CTX_DEAD;
    return 0;
  }

  /* Re-entry: inject value from completed Python await */
  if (ctx->py_await_inject) {
    if (KosStackBlock_PushRef(&ctx->frame_top->stack, ctx->py_await_inject) < 0) {
      Py_DECREF(ctx->py_await_inject);
      ctx->py_await_inject = NULL;
      return -1;
    }
    Py_CLEAR(ctx->py_await_inject);
  }

  /* Opcode dispatch loop */
  for (;;) {
    if (ctx->plan && run_transfer_plan_to_completion(ctx, out_ev) < 0)
      return 0;

    if (PyErr_Occurred()) {
      if (try_local_exception_handler(ctx, out_ev))
        continue;
      return 0;
    }

    KosCode *code = ctx->frame_top->code;
    uint32_t pc = ctx->frame_top->pc;

    if (unlikely(pc >= code->code_len)) {
      KOS_DEBUGF("[eval] pc=%u OUT OF RANGE (code_len=%u)\n",
                 (unsigned) pc, (unsigned) code->code_len);
      PyErr_SetString(PyExc_RuntimeError, "pc out of range");
      return -1;
    }

    KosOp *insn = &code->code[pc];
    KOS_DEBUGF("[eval] pc=%u op=%u arg=%u\n",
               (unsigned) pc, (unsigned) insn->op, (unsigned) insn->arg);
    trace_stack("  before", ctx->frame_top->stack);
    ctx->frame_top->pc = pc + 1;

    if (unlikely((KosOpCode) insn->op > KOS_OP_THEEND)) {
      PyErr_Format(PyExc_RuntimeError, "invalid opcode %u",
                   (unsigned) insn->op);
      return -1;
    }
    int op_res = op_handlers[insn->op](ctx, out_ev, insn);
    if (unlikely(op_res != 0)) {
      if (PyErr_Occurred()) {
        if (try_local_exception_handler(ctx, out_ev))
          continue;
        return 0;
      }
      return (op_res < 0) ? -1 : 0;
    }
    trace_stack("  after ", ctx->frame_top->stack);
  }
}


/* === KosPyContinuation type === */


static void continuation_dealloc(PyObject *self) {

  KosPyContinuation *cont = (KosPyContinuation *) self;

  kos_continuation_free(cont->captured);

  Py_TYPE(self)->tp_free(self);
}


static int continuation_traverse(
    PyObject *self,
    visitproc visit,
    void *arg) {

  KosPyContinuation *cont = (KosPyContinuation *) self;

  if (cont->captured) {
    return kos_continuation_traverse(cont->captured, visit, arg);
  }

  return 0;
}


static int continuation_clear(PyObject *self) {

  KosPyContinuation *cont = (KosPyContinuation *) self;

  kos_continuation_free(cont->captured);
  cont->captured = NULL;

  return 0;
}


static PyObject *continuation_call(
    PyObject *self,
    PyObject *args,
    PyObject *kwds) {

  KosPyContinuation *cont = (KosPyContinuation *) self;

  /* Parse arguments: single optional value */
  PyObject *resume_value = Py_None;
  if (! PyArg_ParseTuple(args, "|O:continuation", &resume_value))
    return NULL;

  if (kwds && PyDict_Size(kwds)) {
    PyErr_SetString(PyExc_TypeError,
                    "continuation does not accept keyword arguments");
    return NULL;
  }

  if (! cont->captured) {
    PyErr_SetString(PyExc_RuntimeError, "continuation has no captured state");
    return NULL;
  }

  /* Clone captured state for multi-shot semantics */
  KosContinuation *cloned = kos_continuation_clone(cont->captured);
  if (! cloned)
    return NULL;

  /* Create new context with the cloned chain */
  KosDynFrame *top_frame = cloned->resume_frame;
  cloned->captured_chain = NULL;  /* context takes ownership */

  KosContext *ctx = kos_context_new(top_frame);
  if (! ctx) {
    kos_continuation_free(cloned);
    return NULL;
  }

  /* Set up the context's dynamic chain */
  /* TODO: properly restore the full chain */

  kos_continuation_free(cloned);

  /* Create and return a coroutine wrapper */
  return KosPyCoroutine_New(ctx);
}


static PyObject *continuation_repr(PyObject *self) {

  return PyUnicode_FromFormat("<continuation at %p>", self);
}


PyTypeObject KosPyContinuationType = {
  PyVarObject_HEAD_INIT(NULL, 0)

  .tp_name = "koskari.lib.Continuation",
  .tp_basicsize = sizeof(KosPyContinuation),
  .tp_itemsize = 0,
  .tp_dealloc = continuation_dealloc,
  .tp_repr = continuation_repr,
  .tp_call = continuation_call,
  .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC,
  .tp_doc = "Captured continuation (multi-shot, delimited)",
  .tp_traverse = continuation_traverse,
  .tp_clear = continuation_clear,
};


PyObject *KosPyContinuation_New(KosContinuation *captured) {

  KosPyContinuation *cont = PyObject_GC_New(KosPyContinuation,
                                            &KosPyContinuationType);
  if (! cont)
    return NULL;

  cont->captured = captured;

  PyObject_GC_Track((PyObject *) cont);
  return (PyObject *) cont;
}


/* === KosPyCoroutine type === */


static void coroutine_dealloc(PyObject *self) {

  PyObject_GC_UnTrack(self);

  KosPyCoroutine *coro = (KosPyCoroutine *) self;

  if (coro->ctx)
    kos_context_free(coro->ctx);

  Py_XDECREF(coro->pending_await);

  Py_TYPE(self)->tp_free(self);
}


static int coroutine_traverse(
    PyObject *self,
    visitproc visit,
    void *arg) {

  KosPyCoroutine *coro = (KosPyCoroutine *) self;

  Py_VISIT(coro->pending_await);

  if (coro->ctx) {
    return kos_context_traverse(coro->ctx, visit, arg);
  }

  return 0;
}


static int coroutine_clear(PyObject *self) {

  KosPyCoroutine *coro = (KosPyCoroutine *) self;

  Py_CLEAR(coro->pending_await);

  if (coro->ctx) {
    kos_context_free(coro->ctx);
    coro->ctx = NULL;
  }

  return 0;
}


static PyObject *coroutine_send(PyObject *self, PyObject *value);
static PyObject *coroutine_throw(PyObject *self, PyObject *args);
static PyObject *coroutine_close(PyObject *self);


static PyObject *coroutine_await(PyObject *self) {

  Py_INCREF(self);
  return self;
}


static PyObject *coroutine_iternext(PyObject *self) {

  return coroutine_send(self, Py_None);
}


static PyObject *coroutine_send(PyObject *self, PyObject *value) {

  KosPyCoroutine *coro = (KosPyCoroutine *) self;

  /* Re-entrancy guard */
  if (coro->in_send) {
    PyErr_SetString(PyExc_RuntimeError,
                    "coroutine already being awaited");
    return NULL;
  }

  /* Check state */
  if (coro->state == KOS_CORO_DONE) {
    PyErr_SetNone(PyExc_StopIteration);
    return NULL;
  }

  /* If waiting on Python awaitable, forward to it */
  if (coro->state == KOS_CORO_WAITING_PY && coro->ctx->py_await_iter) {
    PyObject *result = PyObject_CallMethodObjArgs(coro->ctx->py_await_iter,
                                                  str_send, value, NULL);

    if (result) {
      /* Still yielding */
      return result;
    }

    if (PyErr_ExceptionMatches(PyExc_StopIteration)) {
      /* Python awaitable completed - inject result and drive again */
      PyObject *await_result = NULL;
      PyGen_FetchStopIterationValue(&await_result);
      if (! await_result)
        await_result = Py_None;

      Py_CLEAR(coro->ctx->py_await_iter);
      coro->state = KOS_CORO_RUNNING;
      Py_XDECREF(coro->ctx->py_await_inject);
      coro->ctx->py_await_inject = await_result;
      /* fall through to drive and handle ev */
    } else {
      /* Exception from Python awaitable */
      Py_CLEAR(coro->ctx->py_await_iter);
      coro->state = KOS_CORO_DONE;
      return NULL;
    }
  }

  coro->in_send = 1;
  coro->state = KOS_CORO_RUNNING;

  for (;;) {
    KosEvent ev = {0};
    int res = kos_drive(coro->ctx, &ev);

    coro->in_send = 0;

    if (res < 0) {
      coro->state = KOS_CORO_DONE;
      return NULL;
    }

    switch (ev.kind) {
      case KOS_EV_RETURN:
        coro->state = KOS_CORO_DONE;
        PyErr_SetObject(PyExc_StopIteration, ev.payload);
        Py_XDECREF(ev.payload);
        return NULL;

      case KOS_EV_RAISE:
        coro->state = KOS_CORO_DONE;
        if (ev.payload) {
          PyErr_SetObject((PyObject *) Py_TYPE(ev.payload), ev.payload);
          Py_DECREF(ev.payload);
        }
        return NULL;

      case KOS_EV_YIELD:
        /* For coroutines, yield is like await - return the value */
        return ev.payload;

      case KOS_EV_AWAIT: {
        KOS_DEBUGF("[eval] coroutine_send KOS_EV_AWAIT branch (payload=%d)\n",
                   ev.payload ? 1 : 0);
        if (! ev.payload) {
          PyErr_SetString(PyExc_RuntimeError,
                          "KOS_EV_AWAIT event has NULL payload");
          coro->state = KOS_CORO_DONE;
          return NULL;
        }
        /* Driver already stepped the awaitable in push_or_await and set
           ctx->py_await_iter. Do not call eager_step_awaitable again (would
           send(None) twice and corrupt state). Just suspend and return the
           yielded value to the event loop. */
        coro->state = KOS_CORO_WAITING_PY;
        return ev.payload;  /* transfer ownership to caller (asyncio) */
      }
    }

    PyErr_SetString(PyExc_RuntimeError, "unexpected event kind");
    return NULL;
  }
}


static PyObject *coroutine_throw(PyObject *self, PyObject *args) {

  KosPyCoroutine *coro = (KosPyCoroutine *) self;

  PyObject *typ = NULL, *val = NULL, *tb = NULL;
  if (! PyArg_ParseTuple(args, "O|OO:throw", &typ, &val, &tb))
    return NULL;

  if (coro->state == KOS_CORO_DONE) {
    PyErr_SetNone(PyExc_StopIteration);
    return NULL;
  }

  /* If waiting on Python awaitable, forward throw */
  if (coro->state == KOS_CORO_WAITING_PY && coro->ctx->py_await_iter) {
    if (! str_throw) {
      str_throw = PyUnicode_InternFromString("throw");
      if (! str_throw)
        return NULL;
    }

    PyObject *result = PyObject_CallMethodObjArgs(coro->ctx->py_await_iter,
                                                  str_throw, typ, val, tb,
                                                  NULL);

    if (result) {
      return result;
    }

    if (PyErr_ExceptionMatches(PyExc_StopIteration)) {
      PyObject *await_result = NULL;
      PyGen_FetchStopIterationValue(&await_result);
      if (! await_result)
        await_result = Py_None;
      Py_CLEAR(coro->ctx->py_await_iter);
      coro->state = KOS_CORO_RUNNING;
      Py_XDECREF(coro->ctx->py_await_inject);
      coro->ctx->py_await_inject = await_result;
      return coroutine_send(self, Py_None);
    } else {
      Py_CLEAR(coro->ctx->py_await_iter);
      coro->state = KOS_CORO_DONE;
      return NULL;
    }
  }

  /* Normalize exception */
  PyErr_NormalizeException(&typ, &val, &tb);

  coro->ctx->pending_throw = 1;
  Py_XINCREF(val);
  Py_XDECREF(coro->ctx->pending_exc);
  coro->ctx->pending_exc = val;

  return coroutine_send(self, Py_None);
}


static PyObject *coroutine_close(PyObject *self) {

  KosPyCoroutine *coro = (KosPyCoroutine *) self;

  if (coro->state == KOS_CORO_DONE) {
    Py_RETURN_NONE;
  }

  /* Close any pending Python awaitable */
  if (coro->ctx->py_await_iter) {
    if (! str_close) {
      str_close = PyUnicode_InternFromString("close");
      if (! str_close)
        return NULL;
    }

    PyObject *result = PyObject_CallMethodObjArgs(coro->ctx->py_await_iter,
                                                  str_close, NULL);
    Py_XDECREF(result);
    PyErr_Clear();
    Py_CLEAR(coro->ctx->py_await_iter);
  }

  /* Throw GeneratorExit */
  PyObject *args = Py_BuildValue("(O)", PyExc_GeneratorExit);
  if (! args)
    return NULL;

  PyObject *result = coroutine_throw(self, args);
  Py_DECREF(args);

  if (result) {
    Py_DECREF(result);
    PyErr_SetString(PyExc_RuntimeError,
                    "coroutine ignored GeneratorExit");
    return NULL;
  }

  if (PyErr_ExceptionMatches(PyExc_StopIteration) ||
      PyErr_ExceptionMatches(PyExc_GeneratorExit)) {
    PyErr_Clear();
    coro->state = KOS_CORO_DONE;
    Py_RETURN_NONE;
  }

  return NULL;
}


static PyMethodDef coroutine_methods[] = {
  { "send", (PyCFunction) coroutine_send, METH_O,
    "Send a value into the coroutine" },

  { "throw", (PyCFunction) coroutine_throw, METH_VARARGS,
    "Throw an exception into the coroutine" },

  { "close", (PyCFunction) coroutine_close, METH_NOARGS,
    "Close the coroutine" },

  { NULL, NULL, 0, NULL },
};


static PyAsyncMethods coroutine_async_methods = {
  .am_await = coroutine_await,
};


static PyObject *coroutine_repr(PyObject *self) {

  KosPyCoroutine *coro = (KosPyCoroutine *) self;
  const char *state_str;

  switch (coro->state) {
    case KOS_CORO_NEW:
      state_str = "new";
      break;
    case KOS_CORO_RUNNING:
      state_str = "running";
      break;
    case KOS_CORO_WAITING_PY:
      state_str = "waiting";
      break;
    case KOS_CORO_DONE:
      state_str = "done";
      break;
    default:
      state_str = "unknown";
  }

  return PyUnicode_FromFormat("<coroutine at %p [%s]>", self, state_str);
}


PyTypeObject KosPyCoroutineType = {
  PyVarObject_HEAD_INIT(NULL, 0)

  .tp_name = "koskari.lib.Coroutine",
  .tp_basicsize = sizeof(KosPyCoroutine),
  .tp_itemsize = 0,
  .tp_dealloc = coroutine_dealloc,
  .tp_repr = coroutine_repr,
  .tp_as_async = &coroutine_async_methods,
  .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC,
  .tp_doc = "Koskari coroutine wrapper",
  .tp_traverse = coroutine_traverse,
  .tp_clear = coroutine_clear,
  .tp_iter = PyObject_SelfIter,
  .tp_iternext = coroutine_iternext,
  .tp_methods = coroutine_methods,
};


PyObject *KosPyCoroutine_New(KosContext *ctx) {

  KosPyCoroutine *coro = PyObject_GC_New(KosPyCoroutine, &KosPyCoroutineType);
  if (! coro) {
    kos_context_free(ctx);
    return NULL;
  }

  coro->ctx = ctx;
  coro->state = KOS_CORO_NEW;
  coro->pending_await = NULL;
  coro->in_send = 0;

  PyObject_GC_Track((PyObject *) coro);
  return (PyObject *) coro;
}


/* === KosPyAsyncGen type === */


static void asyncgen_dealloc(PyObject *self) {

  PyObject_GC_UnTrack(self);

  KosPyAsyncGen *agen = (KosPyAsyncGen *) self;

  if (agen->ctx)
    kos_context_free(agen->ctx);

  Py_TYPE(self)->tp_free(self);
}


static int asyncgen_traverse(PyObject *self, visitproc visit, void *arg) {

  KosPyAsyncGen *agen = (KosPyAsyncGen *) self;

  if (agen->ctx) {
    return kos_context_traverse(agen->ctx, visit, arg);
  }

  return 0;
}


static int asyncgen_clear(PyObject *self) {

  KosPyAsyncGen *agen = (KosPyAsyncGen *) self;

  if (agen->ctx) {
    kos_context_free(agen->ctx);
    agen->ctx = NULL;
  }

  return 0;
}


static PyObject *asyncgen_aiter(PyObject *self) {

  Py_INCREF(self);
  return self;
}


static PyObject *asyncgen_anext(PyObject *self) {

  KosPyAsyncGen *agen = (KosPyAsyncGen *) self;

  if (agen->closed) {
    PyErr_SetNone(PyExc_StopAsyncIteration);
    return NULL;
  }

  return KosPyAsyncGenASend_New(agen, Py_None);
}


static PyObject *asyncgen_repr(PyObject *self) {

  return PyUnicode_FromFormat("<async_generator at %p>", self);
}


static PyAsyncMethods asyncgen_async_methods = {
  .am_aiter = asyncgen_aiter,
  .am_anext = asyncgen_anext,
};


PyTypeObject KosPyAsyncGenType = {
  PyVarObject_HEAD_INIT(NULL, 0)

  .tp_name = "koskari.lib.AsyncGenerator",
  .tp_basicsize = sizeof(KosPyAsyncGen),
  .tp_itemsize = 0,
  .tp_dealloc = asyncgen_dealloc,
  .tp_repr = asyncgen_repr,
  .tp_as_async = &asyncgen_async_methods,
  .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC,
  .tp_doc = "Koskari async generator wrapper",
  .tp_traverse = asyncgen_traverse,
  .tp_clear = asyncgen_clear,
};


PyObject *KosPyAsyncGen_New(KosContext *ctx) {

  KosPyAsyncGen *agen = PyObject_GC_New(KosPyAsyncGen, &KosPyAsyncGenType);
  if (! agen) {
    kos_context_free(ctx);
    return NULL;
  }

  agen->ctx = ctx;
  agen->in_progress = 0;
  agen->closed = 0;

  PyObject_GC_Track((PyObject *) agen);
  return (PyObject *) agen;
}


/* === KosPyAsyncGenASend type === */


static void asyncgenasend_dealloc(PyObject *self) {

  PyObject_GC_UnTrack(self);

  KosPyAsyncGenASend *asend = (KosPyAsyncGenASend *) self;

  Py_XDECREF(asend->agen);
  Py_XDECREF(asend->send_value);
  Py_XDECREF(asend->throw_exc);

  Py_TYPE(self)->tp_free(self);
}


static int asyncgenasend_traverse(
    PyObject *self,
    visitproc visit,
    void *arg) {

  KosPyAsyncGenASend *asend = (KosPyAsyncGenASend *) self;

  Py_VISIT(asend->agen);
  Py_VISIT(asend->send_value);
  Py_VISIT(asend->throw_exc);

  return 0;
}


static int asyncgenasend_clear(PyObject *self) {

  KosPyAsyncGenASend *asend = (KosPyAsyncGenASend *) self;

  Py_CLEAR(asend->agen);
  Py_CLEAR(asend->send_value);
  Py_CLEAR(asend->throw_exc);

  return 0;
}


static PyObject *asyncgenasend_await(PyObject *self) {

  Py_INCREF(self);
  return self;
}


static PyObject *asyncgenasend_iternext(PyObject *self) {

  KosPyAsyncGenASend *asend = (KosPyAsyncGenASend *) self;
  KosPyAsyncGen *agen = asend->agen;

  if (! agen || ! agen->ctx) {
    PyErr_SetNone(PyExc_StopAsyncIteration);
    return NULL;
  }

  if (agen->closed) {
    PyErr_SetNone(PyExc_StopAsyncIteration);
    return NULL;
  }

  /* Re-entrancy guard */
  if (agen->in_progress) {
    PyErr_SetString(PyExc_RuntimeError,
                    "async generator already being iterated");
    return NULL;
  }

  agen->in_progress = 1;

  /* Drive the context */
  KosEvent ev = {0};
  int res = kos_drive(agen->ctx, &ev);

  agen->in_progress = 0;

  if (res < 0) {
    agen->closed = 1;
    PyErr_SetNone(PyExc_StopAsyncIteration);
    return NULL;
  }

  switch (ev.kind) {
    case KOS_EV_RETURN:
      /* Generator completed */
      agen->closed = 1;
      Py_XDECREF(ev.payload);
      PyErr_SetNone(PyExc_StopAsyncIteration);
      return NULL;

    case KOS_EV_RAISE:
      agen->closed = 1;
      if (ev.payload) {
        PyErr_SetObject((PyObject *) Py_TYPE(ev.payload), ev.payload);
        Py_DECREF(ev.payload);
      } else {
        PyErr_SetNone(PyExc_StopAsyncIteration);
      }
      return NULL;

    case KOS_EV_YIELD:
      /* Yielded a value - wrap in StopIteration to signal completion
         of this __anext__ awaitable */
      PyErr_SetObject(PyExc_StopIteration, ev.payload);
      Py_XDECREF(ev.payload);
      return NULL;

    case KOS_EV_AWAIT:
      /* Need to suspend on Python awaitable */
      /* Return the awaitable for the caller to await */
      return ev.payload;
  }

  PyErr_SetString(PyExc_RuntimeError, "unexpected event kind");
  return NULL;
}


static PyObject *asyncgenasend_send(PyObject *self, PyObject *value) {

  /* For now, just delegate to iternext */
  return asyncgenasend_iternext(self);
}


static PyObject *asyncgenasend_throw(PyObject *self, PyObject *args) {

  KosPyAsyncGenASend *asend = (KosPyAsyncGenASend *) self;

  /* Mark as throw and delegate */
  asend->is_throw = 1;

  PyObject *typ = NULL;
  if (! PyArg_ParseTuple(args, "O:throw", &typ))
    return NULL;

  Py_XINCREF(typ);
  Py_XDECREF(asend->throw_exc);
  asend->throw_exc = typ;

  return asyncgenasend_iternext(self);
}


static PyObject *asyncgenasend_close(PyObject *self) {

  KosPyAsyncGenASend *asend = (KosPyAsyncGenASend *) self;

  if (asend->agen) {
    asend->agen->closed = 1;
  }

  Py_RETURN_NONE;
}


static PyMethodDef asyncgenasend_methods[] = {
  { "send", (PyCFunction) asyncgenasend_send, METH_O,
    "Send a value" },

  { "throw", (PyCFunction) asyncgenasend_throw, METH_VARARGS,
    "Throw an exception" },

  { "close", (PyCFunction) asyncgenasend_close, METH_NOARGS,
    "Close" },

  { NULL, NULL, 0, NULL },
};


static PyAsyncMethods asyncgenasend_async_methods = {
  .am_await = asyncgenasend_await,
};


static PyObject *asyncgenasend_repr(PyObject *self) {

  return PyUnicode_FromFormat("<async_generator_asend at %p>", self);
}


PyTypeObject KosPyAsyncGenASendType = {
  PyVarObject_HEAD_INIT(NULL, 0)

  .tp_name = "koskari.lib.async_generator_asend",
  .tp_basicsize = sizeof(KosPyAsyncGenASend),
  .tp_itemsize = 0,
  .tp_dealloc = asyncgenasend_dealloc,
  .tp_repr = asyncgenasend_repr,
  .tp_as_async = &asyncgenasend_async_methods,
  .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC,
  .tp_doc = "Koskari async generator asend awaitable",
  .tp_traverse = asyncgenasend_traverse,
  .tp_clear = asyncgenasend_clear,
  .tp_iter = PyObject_SelfIter,
  .tp_iternext = asyncgenasend_iternext,
  .tp_methods = asyncgenasend_methods,
};


PyObject *KosPyAsyncGenASend_New(
    KosPyAsyncGen *agen,
    PyObject *send_value) {

  KosPyAsyncGenASend *asend = PyObject_GC_New(KosPyAsyncGenASend,
                                              &KosPyAsyncGenASendType);
  if (! asend)
    return NULL;

  Py_INCREF(agen);
  asend->agen = agen;

  Py_XINCREF(send_value);
  asend->send_value = send_value;

  asend->is_throw = 0;
  asend->throw_exc = NULL;

  PyObject_GC_Track((PyObject *) asend);
  return (PyObject *) asend;
}


static PyMethodDef eval_module_methods[] = {
  { NULL, NULL, 0, NULL },
};


/* === Unit lifecycle === */


int koskari_eval_init(PyObject *mod) {

  PyObject *dict = PyModule_GetDict(mod);
  if (! dict)
    return -1;

  if (PyModule_AddFunctions(mod, eval_module_methods) < 0)
    return -1;

  /* Initialize string constants */
  STR_CONST(str_await, "__await__");
  STR_CONST(str_send, "send");
  STR_CONST(str_throw, "throw");
  STR_CONST(str_close, "close");

  /* Initialize dynchain module */
  if (koskari_dynchain_init(mod))
    return -1;

  /* Register types */
  if (PyType_Ready(&KosPyContinuationType))
    return -1;

  if (PyType_Ready(&KosPyCoroutineType))
    return -1;

  if (PyType_Ready(&KosPyAsyncGenType))
    return -1;

  if (PyType_Ready(&KosPyAsyncGenASendType))
    return -1;

  if (PyDict_SetItemString(dict, "Continuation",
                           (PyObject *) &KosPyContinuationType))
    return -1;

  if (PyDict_SetItemString(dict, "Coroutine",
                           (PyObject *) &KosPyCoroutineType))
    return -1;

  if (PyDict_SetItemString(dict, "AsyncGenerator",
                           (PyObject *) &KosPyAsyncGenType))
    return -1;

  return 0;
}


int koskari_eval_clear(PyObject *mod) {

  Py_CLEAR(str_await);
  Py_CLEAR(str_send);
  Py_CLEAR(str_throw);
  Py_CLEAR(str_close);

  koskari_dynchain_clear(mod);

  return 0;
}


// The end.
