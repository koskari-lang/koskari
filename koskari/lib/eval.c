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

/* Dispatch: computed goto when available, else switch/case; no function table
   so the compiler can inline handler bodies. Both paths use tail-dispatch to
   next_insn. Build may override with -DKOS_USE_COMPUTED_GOTO=0 or =1. */
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
/* Every handler has (ctx, out_ev, insn).  Cast unused parameters to (void)
   to silence warnings and make intent explicit, e.g. (void) out_ev; (void) insn; */

#define CURRENT_FRAME(ctx)    KosContext_FRAME_TOP(ctx)
#define CURRENT_STACK(ctx)    KosDynFrame_STACK(CURRENT_FRAME(ctx))
#define CURRENT_CODE(ctx)     KosDynFrame_CODE(CURRENT_FRAME(ctx))
#define CURRENT_NS(ctx)       KosDynFrame_NS(CURRENT_FRAME(ctx))
#define CURRENT_PC(ctx)       KosDynFrame_PC(CURRENT_FRAME(ctx))


/**
   Clear the context's transfer plan and free it.  Plan pointer is nulled
   before freeing so any re-entrant eval (e.g. from Py_XDECREF(continuation_ref)
   triggering Python dealloc/__del__) does not see the plan while it is freed.
*/
static void clear_and_free_transfer_plan(KosContext *ctx) {
  KosTransferPlan *p = KosContext_PLAN(ctx);
  KosContext_PLAN(ctx) = NULL;
  KosContext_FLAGS(ctx) &= ~KOS_CTX_IN_TRANSFER;
  kos_free_transfer_plan(p);
}


/**
   Run the current transfer plan to completion.  Returns 0 on success (plan
   cleared or was NULL).  Returns -1 if a step failed (out_ev set to RAISE,
   caller should return 0 from koskari_eval).
*/
static int run_transfer_plan_to_completion(KosContext *ctx, KosEvent *out_ev) {
  while (KosContext_PLAN(ctx)) {
    SAYF("[eval] transfer step %zu/%zu\n",
         (size_t) KosTransferPlan_ACTION_INDEX(KosContext_PLAN(ctx)),
         (size_t) KosTransferPlan_ACTION_COUNT(KosContext_PLAN(ctx)));
    int plan_res = kos_run_transfer_step(ctx);
    if (plan_res < 0) {
      SAY("[eval] transfer step failed -> KOS_EV_RAISE\n");
      KosEvent_KIND(out_ev) = KOS_EV_RAISE;
      KosEvent_PAYLOAD(out_ev) = NULL;
      PyErr_Fetch(&KosEvent_PAYLOAD(out_ev), NULL, NULL);
      KosContext_FLAGS(ctx) &= ~KOS_CTX_RUNNING;
      KosContext_FLAGS(ctx) |= KOS_CTX_DEAD;
      return -1;
    }
    if (plan_res == KOS_XFER_SUSPENDED_AWAIT) {
      SAY("[eval] transfer step suspended on await\n");
      return 0;  /* do not clear plan; driver will resume and feed inject */
    }
    if (plan_res == 0)
      continue;
    SAYF("[eval] transfer complete inv_seg_top=%p\n",
         (void *) KosContext_DYN_TOP(ctx));
    KosContext_INVOCATION_SEGMENT_TOP(ctx) = KosContext_DYN_TOP(ctx);
    clear_and_free_transfer_plan(ctx);
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
  if (! CURRENT_FRAME(ctx) || KosDynNode_PARENT(CURRENT_FRAME(ctx)) == NULL)
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
   Resolve KosOp_ARG(insn) as a prompt_id constant from code.  Returns -1 on error,
   0 on success with *out_prompt_id set.
*/
static int get_prompt_id_from_insn(KosCode *code, KosOp *insn,
                                   uint32_t *out_prompt_id) {
  PyObject *cid = KosCode_GetConstant((PyObject *) code, KosOp_ARG(insn));
  if (! cid)
    return -1;
  long pid = PyLong_AsLong(cid);
  if (pid < 0 && PyErr_Occurred())
    return -1;
  *out_prompt_id = (uint32_t) pid;
  return 0;
}


#if KOS_STACK_TRACE
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
#endif


/* Module-level string constants for await/send/throw/close.  Initialized
   in koskari_eval_init, cleared in koskari_eval_clear. */
static PyObject *str_await = NULL;
static PyObject *str_send = NULL;
static PyObject *str_throw = NULL;
static PyObject *str_close = NULL;
static PyObject *str_asend = NULL;
static PyObject *str_athrow = NULL;
static PyObject *str_aclose = NULL;
static PyObject *str_enter = NULL;
static PyObject *str_exit = NULL;
static PyObject *str_aenter = NULL;
static PyObject *str_aexit = NULL;


/**
   Resolve enter/exit on mgr. Prefer __aenter__/__aexit__ if present (async
   protocol), else __enter__/__exit__ (sync). Returns new refs in *out_enter
   and *out_exit.

   :return: 0 on success, -1 on error
*/
static int resolve_context_enter_exit(
    PyObject *mgr,
    PyObject **out_enter,
    PyObject **out_exit) {

  PyObject *enter = PyObject_GetAttr(mgr, str_aenter);
  PyObject *exit = NULL;
  if (enter) {
    exit = PyObject_GetAttr(mgr, str_aexit);
  }
  if (! enter || ! exit) {
    Py_XDECREF(enter);
    PyErr_Clear();
    enter = PyObject_GetAttr(mgr, str_enter);
    if (! enter)
      return -1;
    exit = PyObject_GetAttr(mgr, str_exit);
    if (! exit) {
      Py_DECREF(enter);
      return -1;
    }
  }
  *out_enter = enter;
  *out_exit = exit;
  return 0;
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

  {
    PyObject *domain = NULL;
    KosContext *active = kos_ctx_current();

    if (active && KosContext_DOMAIN(active))
      domain = KosContext_DOMAIN(active);
    else
      domain = kos_domain_current();
    if (domain) {
      ctx->domain = domain;
      Py_INCREF(ctx->domain);
    } else {
      ctx->domain = (PyObject *) KosDomain_New();
      if (! ctx->domain) {
        Py_DECREF(KosContext_TOKEN(ctx));
        KosContext_TOKEN(ctx) = NULL;
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
    }
  }

  KosContext_DYN_TOP(ctx) = (KosDynNode *) top;
  KosContext_FRAME_TOP(ctx) = top;
  KosContext_GLOBALS_FRAME(ctx) = top;
  KosContext_GLOBALS_CACHE_VERSION(ctx) = 0;

  /* Initialize hook runner */
  KosHookRunner_CODE(KosContext_HOOK_RUNNER(ctx)) = NULL;
  KosHookRunner_PC(KosContext_HOOK_RUNNER(ctx)) = 0;
  KosHookRunner_SCRATCH(KosContext_HOOK_RUNNER(ctx)) = NULL;
  KosHookRunner_ACTIVE(KosContext_HOOK_RUNNER(ctx)) = 0;

  KosContext_UNWIND_TARGET_FRAME(ctx) = NULL;
  KosContext_PENDING_RETURN_VALUE(ctx) = NULL;
  KosContext_PENDING_RETURN_TO_PARENT(ctx) = 0;

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
    KosContext_PLAN_STACK(ctx) = KosTransferPlan_SUSPENDED(p);
    kos_free_transfer_plan(p);
  }

  /* Free hook runner scratch stack */
  if (KosHookRunner_SCRATCH(KosContext_HOOK_RUNNER(ctx))) {
    KosStackBlock *b = KosHookRunner_SCRATCH(KosContext_HOOK_RUNNER(ctx));
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
  Py_XDECREF(KosContext_PENDING_RETURN_VALUE(ctx));
  KosContext_PENDING_RETURN_VALUE(ctx) = NULL;
  Py_XDECREF(KosContext_PENDING_AGEN_DEFAULT(ctx));
  Py_XDECREF(KosContext_DOMAIN(ctx));
  KosContext_DOMAIN(ctx) = NULL;

  /* Clear pending WITH state */
  Py_XDECREF(KosContext_PENDING_WITH_ENTER_MGR(ctx));
  KosContext_PENDING_WITH_ENTER_MGR(ctx) = NULL;
  Py_XDECREF(KosContext_PENDING_WITH_ENTER_ENTER(ctx));
  KosContext_PENDING_WITH_ENTER_ENTER(ctx) = NULL;
  Py_XDECREF(KosContext_PENDING_WITH_ENTER_EXIT(ctx));
  KosContext_PENDING_WITH_ENTER_EXIT(ctx) = NULL;
  Py_XDECREF(KosContext_PENDING_WITH_ENTER_NAME(ctx));
  KosContext_PENDING_WITH_ENTER_NAME(ctx) = NULL;
  kos_clear_pending_with_exit(ctx);
  Py_XDECREF(KosContext_PLAN_RESUME_INJECT(ctx));
  KosContext_PLAN_RESUME_INJECT(ctx) = NULL;
  KosContext_PLAN_SUSPENDED_AWAIT(ctx) = 0;

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
  Py_VISIT(KosContext_DOMAIN(ctx));
  Py_VISIT(KosContext_PY_AWAIT_ITER(ctx));
  Py_VISIT(KosContext_PY_LAST_YIELDED(ctx));
  Py_VISIT(KosContext_PY_RESUME_INJECT(ctx));
  Py_VISIT(KosContext_INJECT_VALUE(ctx));
  Py_VISIT(KosContext_PENDING_AGEN_DEFAULT(ctx));

  /* Traverse pending WITH state */
  if (KosContext_PENDING_WITH_ENTER_MGR(ctx)) {
    Py_VISIT(KosContext_PENDING_WITH_ENTER_MGR(ctx));
    Py_VISIT(KosContext_PENDING_WITH_ENTER_ENTER(ctx));
    Py_VISIT(KosContext_PENDING_WITH_ENTER_EXIT(ctx));
    Py_VISIT(KosContext_PENDING_WITH_ENTER_NAME(ctx));
  }
  if (KosContext_PENDING_WITH_EXIT_NODE(ctx)) {
    Py_VISIT(KosContext_PENDING_WITH_EXIT_TYP(ctx));
    Py_VISIT(KosContext_PENDING_WITH_EXIT_VAL(ctx));
    Py_VISIT(KosContext_PENDING_WITH_EXIT_TB(ctx));
  }
  Py_VISIT(KosContext_PLAN_RESUME_INJECT(ctx));

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

  if (! ctx || ! CURRENT_FRAME(ctx)) {
    PyErr_SetString(PyExc_RuntimeError, "no frame for tail_koscall");
    return -1;
  }

  KosDynFrame *old = CURRENT_FRAME(ctx);
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
  if (KosStackBlock_PushRef(&CURRENT_STACK(ctx), val) < 0) {
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
    if (KosStackBlock_PushRef(&CURRENT_STACK(ctx), val) < 0) {
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
    if (KosStackBlock_PushRef(&CURRENT_STACK(ctx), result) < 0) {
      Py_DECREF(result);
      return -1;
    }
    Py_DECREF(result);
    return 0;
  }

  if (suspended && yielded) {
    KosContext_FLAGS(ctx) &= ~KOS_CTX_RUNNING;
    KosEvent_KIND(out_ev) = KOS_EV_AWAIT;
    KosEvent_PAYLOAD(out_ev) = yielded;
    SAYF("[eval] push_or_await set KOS_EV_AWAIT (suspended=%d yielded=%d)\n",
         suspended, yielded ? 1 : 0);
    return 1;
  }

  return -1;
}


/* Forward declaration for WITH helpers (frame_ns_bind is defined later). */
static int frame_ns_bind(KosContext *ctx, PyObject *name, PyObject *val);


/**
   Use enter result: if awaitable stash and push_or_await; else bind, push block, push node.
   Consumes result and takes ownership of mgr, enter, exit, name.
*/
int kos_push_with_after_enter(
    KosContext *ctx,
    KosEvent *out_ev,
    PyObject *result,
    PyObject *mgr,
    PyObject *enter,
    PyObject *exit,
    PyObject *name,
    uint32_t resume_pc) {

  PyTypeObject *type = result ? Py_TYPE(result) : NULL;
  int is_awaitable = (type && type->tp_as_async && type->tp_as_async->am_await);

  if (is_awaitable) {
    KosContext_PENDING_WITH_ENTER_MGR(ctx) = mgr;
    KosContext_PENDING_WITH_ENTER_ENTER(ctx) = enter;
    KosContext_PENDING_WITH_ENTER_EXIT(ctx) = exit;
    KosContext_PENDING_WITH_ENTER_NAME(ctx) = name;
    KosContext_PENDING_WITH_ENTER_RESUME_PC(ctx) = resume_pc;
    int r = push_or_await(ctx, out_ev, result);
    if (r == 1)
      return 1;
    if (r < 0) {
      kos_clear_pending_with_enter(ctx);
      return -1;
    }
    /* r == 0: awaitable completed synchronously; result was pushed on stack.
       Pop it and use stashed refs for the sync path below; clear stash pointers
       without DECREF so we pass ownership to the sync path. */
    result = KosStackBlock_PopRef(&CURRENT_STACK(ctx));
    if (! result) {
      kos_clear_pending_with_enter(ctx);
      return -1;
    }
    mgr = KosContext_PENDING_WITH_ENTER_MGR(ctx);
    enter = KosContext_PENDING_WITH_ENTER_ENTER(ctx);
    exit = KosContext_PENDING_WITH_ENTER_EXIT(ctx);
    name = KosContext_PENDING_WITH_ENTER_NAME(ctx);
    KosContext_PENDING_WITH_ENTER_MGR(ctx) = NULL;
    KosContext_PENDING_WITH_ENTER_ENTER(ctx) = NULL;
    KosContext_PENDING_WITH_ENTER_EXIT(ctx) = NULL;
    KosContext_PENDING_WITH_ENTER_NAME(ctx) = NULL;
  }

  if (name && name != Py_None) {
    if (frame_ns_bind(ctx, name, result) < 0) {
      Py_DECREF(result);
      Py_DECREF(exit);
      Py_DECREF(enter);
      Py_DECREF(mgr);
      Py_XDECREF(name);
      return -1;
    }
  }
  Py_XDECREF(result);

  if (KosStackBlock_PushBlock(&CURRENT_STACK(ctx)) < 0) {
    Py_DECREF(exit);
    Py_DECREF(enter);
    Py_DECREF(mgr);
    Py_XDECREF(name);
    return -1;
  }

  if (! kos_dyn_push_with(ctx, mgr, enter, exit, name, resume_pc)) {
    KosStackBlock_DropBlock(&CURRENT_STACK(ctx));
    Py_DECREF(exit);
    Py_DECREF(enter);
    Py_DECREF(mgr);
    Py_XDECREF(name);
    return -1;
  }
  return 0;
}


/**
   Complete pending WITH enter after await: TOS is the enter result.
*/
int kos_complete_pending_with_enter(KosContext *ctx, KosEvent *out_ev) {

  if (! KosContext_PENDING_WITH_ENTER_MGR(ctx))
    return 0;

  PyObject *result = KosStackBlock_PopRef(&CURRENT_STACK(ctx));
  if (! result) {
    kos_clear_pending_with_enter(ctx);
    return -1;
  }

  PyObject *mgr = KosContext_PENDING_WITH_ENTER_MGR(ctx);
  PyObject *enter = KosContext_PENDING_WITH_ENTER_ENTER(ctx);
  PyObject *exit = KosContext_PENDING_WITH_ENTER_EXIT(ctx);
  PyObject *name = KosContext_PENDING_WITH_ENTER_NAME(ctx);
  uint32_t resume_pc = KosContext_PENDING_WITH_ENTER_RESUME_PC(ctx);

  KosContext_PENDING_WITH_ENTER_MGR(ctx) = NULL;
  KosContext_PENDING_WITH_ENTER_ENTER(ctx) = NULL;
  KosContext_PENDING_WITH_ENTER_EXIT(ctx) = NULL;
  KosContext_PENDING_WITH_ENTER_NAME(ctx) = NULL;

  if (name && name != Py_None) {
    if (frame_ns_bind(ctx, name, result) < 0) {
      Py_DECREF(result);
      Py_DECREF(exit);
      Py_DECREF(enter);
      Py_DECREF(mgr);
      Py_XDECREF(name);
      return -1;
    }
  }
  Py_XDECREF(result);

  if (KosStackBlock_PushBlock(&CURRENT_STACK(ctx)) < 0) {
    Py_DECREF(exit);
    Py_DECREF(enter);
    Py_DECREF(mgr);
    Py_XDECREF(name);
    return -1;
  }

  if (! kos_dyn_push_with(ctx, mgr, enter, exit, name, resume_pc)) {
    KosStackBlock_DropBlock(&CURRENT_STACK(ctx));
    Py_DECREF(exit);
    Py_DECREF(enter);
    Py_DECREF(mgr);
    Py_XDECREF(name);
    return -1;
  }
  (void) out_ev;
  return 0;
}


/**
   Start awaiting WITH exit result. Stash already set by kos_dyn_with_unwind.
*/
int kos_with_exit_start_await(KosContext *ctx, KosEvent *out_ev,
                              PyObject *awaitable) {

  return push_or_await(ctx, out_ev, awaitable);
}


/**
   Complete pending WITH exit after await: TOS is the exit result.
   Apply swallow logic, drop block, pop node, clear stash.
*/
int kos_complete_pending_with_exit(KosContext *ctx) {

  if (! KosContext_PENDING_WITH_EXIT_NODE(ctx))
    return 0;

  PyObject *result = KosStackBlock_PopRef(&CURRENT_STACK(ctx));
  if (! result) {
    kos_clear_pending_with_exit(ctx);
    return -1;
  }

  KosDynWith *with_node = KosContext_PENDING_WITH_EXIT_NODE(ctx);
  PyObject *typ = KosContext_PENDING_WITH_EXIT_TYP(ctx);
  PyObject *val = KosContext_PENDING_WITH_EXIT_VAL(ctx);
  PyObject *tb = KosContext_PENDING_WITH_EXIT_TB(ctx);
  (void) KosContext_PENDING_WITH_EXIT_NORMAL(ctx);

  KosContext_PENDING_WITH_EXIT_NODE(ctx) = NULL;
  KosContext_PENDING_WITH_EXIT_TYP(ctx) = NULL;
  KosContext_PENDING_WITH_EXIT_VAL(ctx) = NULL;
  KosContext_PENDING_WITH_EXIT_TB(ctx) = NULL;

  int had_exception = (typ != Py_None);

  if (had_exception && PyObject_IsTrue(result)) {
    PyErr_Clear();
    Py_DECREF(result);
    Py_XDECREF(typ);
    Py_XDECREF(val);
    Py_XDECREF(tb);
    if (KosStackBlock_DropBlock(&CURRENT_STACK(ctx)) < 0)
      return -1;
    if (KosContext_FRAME_TOP(ctx))
      KosDynFrame_PC(KosContext_FRAME_TOP(ctx)) = KosDynWith_RESUME_PC(with_node);
    kos_dyn_pop_with(ctx, with_node);
    if (KosStackBlock_PushRef(&CURRENT_STACK(ctx), Py_None) < 0)
      return -1;
    return 0;
  }

  if (had_exception) {
    Py_DECREF(result);
    if (KosStackBlock_DropBlock(&CURRENT_STACK(ctx)) < 0) {
      PyErr_Restore(typ, val, tb);
      return -1;
    }
    kos_dyn_pop_with(ctx, with_node);
    PyErr_Restore(typ, val, tb);
    return -1;
  }

  Py_DECREF(result);
  Py_XDECREF(typ);
  Py_XDECREF(val);
  Py_XDECREF(tb);
  if (KosStackBlock_DropBlock(&CURRENT_STACK(ctx)) < 0)
    return -1;
  uint32_t next_pc = KosDynWith_RESUME_PC(with_node);
  kos_dyn_pop_with(ctx, with_node);
  if (KosContext_FRAME_TOP(ctx))
    KosDynFrame_PC(KosContext_FRAME_TOP(ctx)) = next_pc;
  if (KosStackBlock_PushRef(&CURRENT_STACK(ctx), Py_None) < 0)
    return -1;
  return 0;
}


/**
   Clear pending WITH exit stash. No-op if not set.
*/
void kos_clear_pending_with_exit(KosContext *ctx) {

  Py_XDECREF(KosContext_PENDING_WITH_EXIT_TYP(ctx));
  KosContext_PENDING_WITH_EXIT_TYP(ctx) = NULL;
  Py_XDECREF(KosContext_PENDING_WITH_EXIT_VAL(ctx));
  KosContext_PENDING_WITH_EXIT_VAL(ctx) = NULL;
  Py_XDECREF(KosContext_PENDING_WITH_EXIT_TB(ctx));
  KosContext_PENDING_WITH_EXIT_TB(ctx) = NULL;
  KosContext_PENDING_WITH_EXIT_NODE(ctx) = NULL;
}


/**
   Clear pending WITH enter stash (release refs). No-op if not set.
*/
void kos_clear_pending_with_enter(KosContext *ctx) {

  Py_XDECREF(KosContext_PENDING_WITH_ENTER_MGR(ctx));
  KosContext_PENDING_WITH_ENTER_MGR(ctx) = NULL;
  Py_XDECREF(KosContext_PENDING_WITH_ENTER_ENTER(ctx));
  KosContext_PENDING_WITH_ENTER_ENTER(ctx) = NULL;
  Py_XDECREF(KosContext_PENDING_WITH_ENTER_EXIT(ctx));
  KosContext_PENDING_WITH_ENTER_EXIT(ctx) = NULL;
  Py_XDECREF(KosContext_PENDING_WITH_ENTER_NAME(ctx));
  KosContext_PENDING_WITH_ENTER_NAME(ctx) = NULL;
}


/* === Frame namespace helpers (for VAR_* opcodes) === */


/**
   Get the name constant for a VAR_* opcode. Borrowed reference.
*/
static PyObject *get_name_from_insn(KosCode *code, KosOp *insn) {
  return KosCode_GetConstant((PyObject *) code, KosOp_ARG(insn));
}


/**
   Deref name in the current frame namespace. New reference or NULL.
*/
static PyObject *frame_ns_deref(KosContext *ctx, PyObject *name) {
  PyObject *ns = CURRENT_NS(ctx);
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
  PyObject *ns = CURRENT_NS(ctx);
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
  PyObject *ns = CURRENT_NS(ctx);
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
  PyObject *ns = CURRENT_NS(ctx);
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
  PyObject *ns = CURRENT_NS(ctx);
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
               (unsigned) KosOp_OP(insn));
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

  if (KosContinuation_PROMPT_ID(k) != KOS_PROMPT_ID_UNDELIMITED
      && ! kos_find_prompt(ctx, KosContinuation_PROMPT_ID(k))) {
    Py_DECREF(resume_val);
    PyErr_SetString(KoskariContinuationError,
                    "continuation: prompt not found in dynamic chain");
    return -1;
  }

  if (KosContinuation_TOKEN_REF(k) == (PyObject *) KosContext_TOKEN(ctx)
      || (KosContinuation_PROMPT_ID(k) == KOS_PROMPT_ID_UNDELIMITED && ! KosContinuation_TOKEN_REF(k))) {
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
      PyObject_SetAttrString(exc, "target", KosContinuation_TOKEN_REF(k));
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

  PyObject *code = KosExpression_CODE(callable);
  PyObject *ns = KosExpression_NS(callable);
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
  PyObject *fn_code = KosFunction_CODE(callable);
  if (kos_dyn_push_frame(ctx, (KosCode *) fn_code, frame_ns) == NULL) {
    Py_DECREF(frame_ns);
    return -1;
  }
  (void) out_ev;
  return 0;
}


/**
   call_tail_flattened_expr: callable is borrowed (KosExpression, n is 0).
   Replace current frame with expression's code/ns via kos_tail_koscall.
*/
static int call_tail_flattened_expr(
    KosContext *ctx,
    PyObject *callable,
    KosEvent *out_ev) {

  PyObject *code = KosExpression_CODE(callable);
  PyObject *ns = KosExpression_NS(callable);
  Py_INCREF(ns);
  int r = kos_tail_koscall(ctx, (KosCode *) code, ns);
  if (r < 0)
    Py_DECREF(ns);
  (void) out_ev;
  return r;
}


/**
   call_tail_flattened_function: callable and args are borrowed. Prepare
   call namespace from formals, replace current frame via kos_tail_koscall.
*/
static int call_tail_flattened_function(
    KosContext *ctx,
    PyObject *callable,
    PyObject *args,
    KosEvent *out_ev) {

  PyObject *frame_ns = KosFunction_PrepareCallNamespace(
      callable, args, NULL);
  if (! frame_ns)
    return -1;
  int r = kos_tail_koscall(ctx, (KosCode *) KosFunction_CODE(callable),
                          frame_ns);
  if (r < 0) {
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


/* === CALL === */


/**
   CALL: arg = number of positional arguments N. Stack has [callable, arg_0,
   ..., arg_{N-1}] (callable oldest). Call callable with those args, drop
   them, then push result or await it.
*/


/**
   Reducing operator: pop opa values (or use TailPtr for opa > 0), call fn,
   drop opa, push result. Faithful to ceval _obj_op.
 */
static int obj_op_reducing(KosContext *ctx, KosEvent *out_ev, KosOp *insn,
                           KosOperatorReducing *fn) {
  int opa = (int) KosOp_ARG(insn);
  PyObject *res;

  if (opa < 0) {
    PyErr_Format(PyExc_RuntimeError,
                 "Negative count argument to reducing operator: %i", opa);
    return -1;
  }

  if (opa == 0) {
    res = fn(NULL, 0);
  } else {
    PyObject *const *args = KosStackBlock_TailPtr(CURRENT_STACK(ctx),
                                                  (size_t) opa);
    if (! args)
      return -1;
    res = fn(args, (size_t) opa);
    KosStackBlock_DropNoErr(&CURRENT_STACK(ctx), (size_t) opa);
  }

  if (! res)
    return -1;
  return push_result(ctx, res);
}


static int obj_op_binary(
    KosContext *ctx,
    KosEvent *out_ev,
    KosOp *insn,
    PyObject *(*fn)(PyObject *left, PyObject *right)) {

  PyObject *right = KosStackBlock_PopRef(&CURRENT_STACK(ctx));
  if (! right)
    return -1;
  PyObject *left = KosStackBlock_PopRef(&CURRENT_STACK(ctx));
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


static int op_OBJECT_EQ_impl(
    KosContext *ctx,
    KosEvent *out_ev,
    KosOp *insn,
    int op) {

  PyObject *right = KosStackBlock_PopRef(&CURRENT_STACK(ctx));
  if (! right)
    return -1;
  PyObject *left = KosStackBlock_PopRef(&CURRENT_STACK(ctx));
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


/* Pop value then container; PySequence_Contains(container, value). Branch to
   KosOp_ARG(insn) when cmp == branch_when (CHECK_IN: 0, CHECK_NOT_IN: 1). */
static int kos_check_contains(
    KosContext *ctx,
    KosEvent *out_ev,
    KosOp *insn,
    int branch_when) {

  PyObject *value = KosStackBlock_PopRef(&CURRENT_STACK(ctx));
  if (! value)
    return -1;
  PyObject *container = KosStackBlock_PopRef(&CURRENT_STACK(ctx));
  if (! container) {
    Py_DECREF(value);
    return -1;
  }
  int cmp = PySequence_Contains(container, value);
  Py_DECREF(value);
  Py_DECREF(container);
  if (unlikely(cmp < 0))
    return -1;
  if (cmp == branch_when)
    CURRENT_PC(ctx) = KosOp_ARG(insn);
  (void) out_ev;
  return 0;
}


/* Pop right then left; branch to KosOp_ARG(insn) when (left == right) == branch_when_equal
   (CHECK_IS: 0 so branch when not equal; CHECK_IS_NOT: 1 so branch when equal). */
static int kos_check_identity(
    KosContext *ctx,
    KosEvent *out_ev,
    KosOp *insn,
    int branch_when_equal) {

  PyObject *right = KosStackBlock_PopRef(&CURRENT_STACK(ctx));
  if (! right)
    return -1;
  PyObject *left = KosStackBlock_PopRef(&CURRENT_STACK(ctx));
  if (! left) {
    Py_DECREF(right);
    return -1;
  }
  int cmp = (left == right);
  Py_DECREF(left);
  Py_DECREF(right);
  if (cmp == branch_when_equal)
    CURRENT_PC(ctx) = KosOp_ARG(insn);
  (void) out_ev;
  return 0;
}


/* Pop right then left; call check(left, right). Branch to KosOp_ARG(insn) when res == 0. */
static int kos_check_two_arg(
    KosContext *ctx,
    KosEvent *out_ev,
    KosOp *insn,
    int (*check)(PyObject *, PyObject *)) {

  PyObject *right = KosStackBlock_PopRef(&CURRENT_STACK(ctx));
  if (! right)
    return -1;
  PyObject *left = KosStackBlock_PopRef(&CURRENT_STACK(ctx));
  if (! left) {
    Py_DECREF(right);
    return -1;
  }
  int res = check(left, right);
  Py_DECREF(left);
  Py_DECREF(right);
  if (unlikely(res < 0))
    return -1;
  if (res == 0)
    CURRENT_PC(ctx) = KosOp_ARG(insn);
  (void) out_ev;
  return 0;
}


/* Helper for CHECK_EQ/GE/GT/LE/LT/NE: pop right then left, RichCompareBool;
   jump to KosOp_ARG(insn) if comparison fails (res == 0). */
static int kos_check_cmp(
    KosContext *ctx,
    KosEvent *out_ev,
    KosOp *insn,
    int cmp_op) {

  PyObject *right = KosStackBlock_PopRef(&CURRENT_STACK(ctx));
  if (! right)
    return -1;
  PyObject *left = KosStackBlock_PopRef(&CURRENT_STACK(ctx));
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
    CURRENT_PC(ctx) = KosOp_ARG(insn);
  (void) out_ev;
  return 0;
}


/* === THEEND, BREAK, CONTINUE, YIELD, YIELD_FROM === */

/* THEEND: no runtime implementation; hardcoded error (not a "stub" to be implemented). */

/**
   THEEND: arg unused. End-of-code sentinel; has no runtime implementation.
   If execution reaches THEEND, raise a hardcoded unimplemented error.
*/


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
    KosEvent_KIND(out_ev) = KOS_EV_AWAIT;
    KosEvent_PAYLOAD(out_ev) = yielded;
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


/* === NEXT, ITER_NEXT === */


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
    PyObject *res = PyObject_CallMethodObjArgs(delegate, str_close, NULL);
    Py_XDECREF(res);
    PyErr_Clear();
    break;
  }
  case KOS_YF_PY_ASYNC_ITER:
  case KOS_YF_PY_ASYNC_GEN:
    Py_XDECREF(inflight);
    KosDynYieldFrom_INFLIGHT(node) = NULL;
    {
      PyObject *res = PyObject_CallMethodObjArgs(delegate, str_aclose, NULL);
      Py_XDECREF(res);
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


/*  Includes the implementation of all opcodes from the opcode/ directory.
*/
#include "handlers.incl"


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
    if (CURRENT_FRAME(ctx)) {
      while (KosContext_DYN_TOP(ctx) != NULL
             && KosContext_DYN_TOP(ctx) != (KosDynNode *) CURRENT_FRAME(ctx)) {
        KosDynNode *top = KosContext_DYN_TOP(ctx);
        int top_was_with = (top && KosDynNode_IS_WITH(top));
        /* YIELD_FROM close can call PyErr_Clear(); preserve exception across pop */
        PyObject *saved_typ = NULL, *saved_val = NULL, *saved_tb = NULL;
        if (top && KosDynNode_IS_YIELD_FROM(top) && PyErr_Occurred())
          PyErr_Fetch(&saved_typ, &saved_val, &saved_tb);
        int pop_res = kos_dyn_pop_top(ctx, out_ev);
        if (saved_typ != NULL)
          PyErr_Restore(saved_typ, saved_val, saved_tb);
        if (pop_res == KOS_DYN_UNWIND_ASYNC_STARTED
            || pop_res == KOS_DYN_WITH_EXIT_ASYNC_STARTED) {
          KosContext_UNWIND_TARGET_FRAME(ctx) = CURRENT_FRAME(ctx);
          return 1;
        }
        if (pop_res != 0) {
          PyObject *typ = NULL, *val = NULL, *tb = NULL;
          PyErr_Fetch(&typ, &val, &tb);
          PyErr_Clear();
          KosEvent_KIND(out_ev) = KOS_EV_RAISE;
          KosEvent_PAYLOAD(out_ev) = val;  /* exception instance (caller DECREFs) */
          Py_XDECREF(typ);
          Py_XDECREF(tb);
          KosContext_FLAGS(ctx) &= ~KOS_CTX_RUNNING;
          KosContext_FLAGS(ctx) |= KOS_CTX_DEAD;
          return 0;
        }
        /* Only continue when a WITH's __exit__ swallowed; other nodes (e.g.
           YIELD_FROM) may clear PyErr in their teardown (e.g. delegate close). */
        if (top_was_with && ! PyErr_Occurred()) {
          return 1;
        }
      }
    }
    {
      PyObject *typ = NULL, *val = NULL, *tb = NULL;
      PyErr_Fetch(&typ, &val, &tb);
      PyErr_Clear();
      KosEvent_KIND(out_ev) = KOS_EV_RAISE;
      KosEvent_PAYLOAD(out_ev) = val;  /* exception instance (caller DECREFs) */
      Py_XDECREF(typ);
      Py_XDECREF(tb);
      KosContext_FLAGS(ctx) &= ~KOS_CTX_RUNNING;
      KosContext_FLAGS(ctx) |= KOS_CTX_DEAD;
      return 0;
    }
  }

  uint32_t handler_pc = KosDynRegion_BREAK_PC(region);
  {
    KosTryHandler th;
    if (kos_try_handler_for((PyObject *) CURRENT_CODE(ctx),
                            CURRENT_PC(ctx), &th) == 0) {
      if (KosDynRegion_STYLE(region) == KOS_REGION_FINALLY && th.finally_pc != KOS_EXTENT_NONE) {
        handler_pc = th.finally_pc;
      }
      else if (th.except_pc != KOS_EXTENT_NONE) {
        handler_pc = th.except_pc;
      }
    }
  }

  CURRENT_PC(ctx) = handler_pc;
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

  /* Push domain scope (token, context, live_depth) onto per-thread TLS */
  kos_domain_push_scope(ctx);

  /* Handle pending exception injection */
  if (KosContext_INJECT_MODE(ctx) == KOS_INJECT_THROW && KosContext_INJECT_VALUE(ctx)) {
    PyErr_SetObject((PyObject *) Py_TYPE(KosContext_INJECT_VALUE(ctx)), KosContext_INJECT_VALUE(ctx));
    Py_CLEAR(KosContext_INJECT_VALUE(ctx));
    KosContext_INJECT_MODE(ctx) = KOS_INJECT_NONE;

    KosEvent_KIND(out_ev) = KOS_EV_RAISE;
    KosEvent_PAYLOAD(out_ev) = NULL;
    PyErr_Fetch(&KosEvent_PAYLOAD(out_ev), NULL, NULL);

    KosContext_FLAGS(ctx) &= ~KOS_CTX_RUNNING;
    KosContext_FLAGS(ctx) |= KOS_CTX_DEAD;
    eval_ret = 0;
    goto koskari_eval_cleanup;
  }

  if (run_transfer_plan_to_completion(ctx, out_ev) < 0) {
    eval_ret = 0;
    goto koskari_eval_cleanup;
  }

  if (! CURRENT_FRAME(ctx) && ! kos_hook_active(ctx)) {
    KosEvent_KIND(out_ev) = KOS_EV_RETURN;
    KosEvent_PAYLOAD(out_ev) = Py_None;
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
    if (KosStackBlock_PushRef(&CURRENT_STACK(ctx), KosContext_PY_RESUME_INJECT(ctx)) < 0) {
      Py_DECREF(KosContext_PY_RESUME_INJECT(ctx));
      KosContext_PY_RESUME_INJECT(ctx) = NULL;
      KosContext_PY_RESUME_KIND(ctx) = KOS_RESUME_NONE;
      eval_ret = -1;
      goto koskari_eval_cleanup;
    }
    Py_CLEAR(KosContext_PY_RESUME_INJECT(ctx));
  }

  /* If we resumed from WITH exit await, complete the pop before dispatch. */
  if (KosContext_PENDING_WITH_EXIT_NODE(ctx)) {
    if (kos_complete_pending_with_exit(ctx) < 0) {
      eval_ret = 0;
      goto koskari_eval_cleanup;
    }
  }

  /* If we resumed from transfer plan await, pop inject into plan slot for the step. */
  if (KosContext_PLAN_SUSPENDED_AWAIT(ctx)) {
    PyObject *inj = KosStackBlock_PopRef(&CURRENT_STACK(ctx));
    KosContext_PLAN_RESUME_INJECT(ctx) = inj;
    KosContext_PLAN_SUSPENDED_AWAIT(ctx) = 0;
  }

#if KOS_USE_COMPUTED_GOTO
  static const void *const optable[] = {
#define OP(name, style) &&TARGET_##name,
#include "opcodes.incl"
#undef OP
  };
#endif

  /* Opcode dispatch loop; tail-dispatch: normal path jumps to next_insn. */
  for (;;) {
    next_insn:
    /* Clear resume kind from previous op (YIELD_FROM clears when it consumes; others leave it set). */
    if (KosContext_PY_RESUME_KIND(ctx) != KOS_RESUME_NONE)
      KosContext_PY_RESUME_KIND(ctx) = KOS_RESUME_NONE;
#if KOS_STACK_TRACE
    trace_stack("  after ", CURRENT_STACK(ctx));
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

    KosCode *code = kos_hook_active(ctx)
        ? KosHookRunner_CODE(KosContext_HOOK_RUNNER(ctx))
        : CURRENT_CODE(ctx);
    uint32_t pc = kos_hook_active(ctx)
        ? KosHookRunner_PC(KosContext_HOOK_RUNNER(ctx))
        : CURRENT_PC(ctx);

    if (unlikely(pc >= KosCode_CODE_LEN(code))) {
      SAYF("[eval] pc=%u OUT OF RANGE (code_len=%u)\n",
           (unsigned) pc, (unsigned) KosCode_CODE_LEN(code));
      PyErr_SetString(PyExc_RuntimeError, "pc out of range");
      eval_ret = -1;
      goto koskari_eval_cleanup;
    }

    KosOp *insn = &KosCode_CODE(code)[pc];
#if VERBOSE_DEBUG
    SAYF("[eval] pc=%u op=%u arg=%u\n",
         (unsigned) pc, (unsigned) KosOp_OP(insn), (unsigned) KosOp_ARG(insn));
#endif
#if KOS_STACK_TRACE
    trace_stack("  before", CURRENT_STACK(ctx));
#endif

    /* Advance PC before dispatch so normal path advances. If handler returns 1
       (suspend, e.g. WITH enter/exit await) we roll back so next eval is same insn. */
    if (kos_hook_active(ctx))
      KosHookRunner_PC(KosContext_HOOK_RUNNER(ctx)) = pc + 1;
    else
      CURRENT_PC(ctx) = pc + 1;

    if (unlikely((KosOpCode) KosOp_OP(insn) > KOS_OP_THEEND)) {
      PyErr_Format(PyExc_RuntimeError, "invalid opcode %u",
                   (unsigned) KosOp_OP(insn));
      eval_ret = -1;
      goto koskari_eval_cleanup;
    }
    {
      int op_res;
#if KOS_USE_COMPUTED_GOTO
      goto *optable[KosOp_OP(insn)];
#define OP(name, style) TARGET_##name: op_res = op_##name(ctx, out_ev, insn); if (op_res != 0) goto handle_op_result; goto next_insn;
#include "opcodes.incl"
#undef OP
      handle_op_result:
#else
      switch (KosOp_OP(insn)) {
#define OP(name, style) case KOS_OP_##name: op_res = op_##name(ctx, out_ev, insn); break;
#include "opcodes.incl"
#undef OP
      default:
        PyErr_Format(PyExc_RuntimeError, "invalid opcode %u",
                     (unsigned) KosOp_OP(insn));
        eval_ret = -1;
        goto koskari_eval_cleanup;
      }
#endif
      if (unlikely(op_res != 0)) {
        if (PyErr_Occurred()) {
          clear_pending_agen_on_error(ctx);
          if (try_local_exception_handler(ctx, out_ev))
            continue;
          eval_ret = 0;
          goto koskari_eval_cleanup;
        }
        if (op_res == 1) {
          /* WITH enter/exit await: do not advance PC so resume dispatches
             the same opcode. Other opcodes (e.g. YIELD) use 1 for suspend
             but resume at the next instruction. */
          KosOpCode op = (KosOpCode) KosOp_OP(insn);
          if (op == KOS_OP_PUSH_WITH || op == KOS_OP_POP_WITH) {
            if (kos_hook_active(ctx))
              KosHookRunner_PC(KosContext_HOOK_RUNNER(ctx)) = pc;
            else
              CURRENT_PC(ctx) = pc;
          }
        }
        eval_ret = (op_res < 0) ? -1 : 0;
        goto koskari_eval_cleanup;
      }
#if !KOS_USE_COMPUTED_GOTO
      goto next_insn;
#endif
    }
  }

koskari_eval_cleanup:
  kos_domain_pop_scope(ctx);
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

  /* Initialize string constants (cleared in koskari_eval_clear). */
  STR_CONST(str_await, "__await__");
  STR_CONST(str_send, "send");
  STR_CONST(str_throw, "throw");
  STR_CONST(str_close, "close");
  STR_CONST(str_asend, "asend");
  STR_CONST(str_athrow, "athrow");
  STR_CONST(str_aclose, "aclose");
  STR_CONST(str_enter, "__enter__");
  STR_CONST(str_exit, "__exit__");
  STR_CONST(str_aenter, "__aenter__");
  STR_CONST(str_aexit, "__aexit__");

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

  /* Clear string constants initialized in koskari_eval_init. */
  Py_CLEAR(str_await);
  Py_CLEAR(str_send);
  Py_CLEAR(str_throw);
  Py_CLEAR(str_close);
  Py_CLEAR(str_asend);
  Py_CLEAR(str_athrow);
  Py_CLEAR(str_aclose);
  Py_CLEAR(str_enter);
  Py_CLEAR(str_exit);
  Py_CLEAR(str_aenter);
  Py_CLEAR(str_aexit);

  /* Null out exception type pointers (module dict owns the refs) */
  KoskariContinuationDead = NULL;
  KoskariContinuationApply = NULL;
  KoskariContinuationError = NULL;

  koskari_dynchain_clear(mod);

  return 0;
}


// The end.
