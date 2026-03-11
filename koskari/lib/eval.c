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

   6. Cross-context continuation invocation (segmented transfer): when a
      KoskariContinuationApply exception targets a different eval context
      (foreign token), process_raise intercepts it via
      handle_continuation_apply.  The exception is cleared, a
      KosDynCCRaise sentinel is inserted at the root of the dynamic chain,
      and a NULL-target transfer plan is built.  The plan unwinds all
      WITH/finally extents in the current context (calling their exit
      handlers with no active exception), and then the sentinel's unwind
      callback re-raises the original exception.  The eval loop exits with
      KOS_EV_RAISE, propagating the exception upward through the Python
      async call chain to the owning eval context.

   :author: Christopher O'Brien <obriencj@preoccupied.net>
   :license: GNU General Public License v3
   :ai-assistant: claude-4.6-sonnet-high via Cursor
*/


#include "lib.h"
#include "hints.h"
#include "debug.h"
#include "tracing.h"

#include <stdint.h>
#include <frameobject.h>

/* Every handler has (ctx, out_ev, insn).  Cast unused parameters to (void)
   to silence warnings and make intent explicit, e.g. (void) out_ev; (void) insn; */

#define CURRENT_FRAME(ctx)       KosContext_FRAME_TOP(ctx)
#define CURRENT_STACK(ctx)       KosDynFrame_STACK(CURRENT_FRAME(ctx))
#define CURRENT_CODE(ctx)        KosDynFrame_CODE(CURRENT_FRAME(ctx))
#define CURRENT_NS(ctx)          KosDynFrame_NS(CURRENT_FRAME(ctx))
#define CURRENT_PC(ctx)          KosDynFrame_PC(CURRENT_FRAME(ctx))
#define CURRENT_ACTING_PC(ctx)   KosDynFrame_ACTING_PC(CURRENT_FRAME(ctx))


/**
   Clear the context's transfer plan and free it.  Plan pointer is nulled
   before freeing so any re-entrant eval (e.g. from Py_XDECREF(continuation_ref)
   triggering Python dealloc/__del__) does not see the plan while it is freed.
   For PUSH_SHIFT unwind plans, transfers continuation_ref to the prompt's
   pending_shift_wrapper so the opcode can push the handler on re-entry.
*/
static void clear_and_free_transfer_plan(KosContext *ctx) {
  KosTransferPlan *p = KosContext_PLAN(ctx);
  KosContext_PLAN(ctx) = NULL;
  KosContext_FLAGS(ctx) &= ~KOS_CTX_IN_TRANSFER;
  if (p && KosTransferPlan_CONTINUATION_REF(p)
      && KosTransferPlan_ORIGINAL_TARGET(p)
      && KosDynNode_IS_PROMPT(KosTransferPlan_ORIGINAL_TARGET(p))) {
    KosDynPrompt *prompt = (KosDynPrompt *) KosTransferPlan_ORIGINAL_TARGET(p);
    KosDynPrompt_PENDING_SHIFT_WRAPPER(prompt) = KosTransferPlan_CONTINUATION_REF(p);
    KosTransferPlan_CONTINUATION_REF(p) = NULL;
  }
  kos_free_transfer_plan(p);
}


static void inject_eval_frame_traceback(KosContext *ctx,
    PyObject **exc_type, PyObject **exc_val, PyObject **exc_tb);


/**
   Run the current transfer plan to completion.  Returns 0 on success (plan
   cleared or was NULL).  Returns -1 if a step failed (out_ev set to RAISE,
   caller should return 0 from koskari_eval).
*/
static int run_transfer_plan_to_completion(
    KosContext *ctx,
    KosEvent *out_ev) {

  while (KosContext_PLAN(ctx)) {
    SAYF("[eval] transfer step %zu/%zu\n",
         (size_t) KosTransferPlan_ACTION_INDEX(KosContext_PLAN(ctx)),
         (size_t) KosTransferPlan_ACTION_COUNT(KosContext_PLAN(ctx)));
    int plan_res = kos_run_transfer_step(ctx, out_ev);

    if (plan_res < 0) {
      SAY("[eval] transfer step failed -> KOS_EV_RAISE\n");
      KosEvent_KIND(out_ev) = KOS_EV_RAISE;
      KosEvent_PAYLOAD(out_ev) = NULL;
      KosEvent_EXC_TYPE(out_ev) = NULL;
      {
        PyObject *exc_typ = NULL, *exc_val = NULL, *exc_tb = NULL;
        PyErr_Fetch(&exc_typ, &exc_val, &exc_tb);
        inject_eval_frame_traceback(ctx, &exc_typ, &exc_val, &exc_tb);
        Py_XDECREF(exc_tb);
        KosEvent_EXC_TYPE(out_ev) = exc_typ;  /* caller owns */
        KosEvent_PAYLOAD(out_ev) = exc_val;   /* caller owns */
      }
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
  }

  return 0;
}


/**
   Handle delimited k(value) return: build plan, install it, run to completion.
   Caller has popped val; we consume it. Return 0 on success, -1 on error.
*/
static int kos_return_delimited(KosContext *ctx, PyObject *val, KosEvent *out_ev) {
  SAYF("[RETURN] taking return-target path: return_pc=%u",
       (unsigned) KosContext_RETURN_TARGET_PC(ctx));
  KosTransferPlan *plan = NULL;
  if (kos_build_return_plan(ctx, KosContext_RETURN_TARGET_CHAIN(ctx),
                            val, 0, &plan) < 0) {
    Py_DECREF(val);
    return -1;
  }
  Py_DECREF(val);
  KosTransferPlan_RESUME_PC(plan) = KosContext_RETURN_TARGET_PC(ctx);
  KosTransferPlan_RESUME_FRAME(plan) = NULL;
  KosTransferPlan_RESUME_STACK(plan) = NULL;
  if (KosContext_PLAN(ctx))
    kos_free_transfer_plan(KosContext_PLAN(ctx));
  KosContext_PLAN(ctx) = plan;
  KosContext_FLAGS(ctx) |= KOS_CTX_IN_TRANSFER;
  KosTransferPlan_CONTINUATION_REF(plan) = NULL;
  kos_dynchain_free(KosContext_RETURN_TARGET_CHAIN(ctx), NULL);
  KosContext_RETURN_TARGET_CHAIN(ctx) = NULL;
  KosContext_RETURN_TARGET_PC(ctx) = (uint32_t) -1;
  if (run_transfer_plan_to_completion(ctx, out_ev) < 0)
    return -1;
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
   Revive the active transfer plan with send_val or throw_exc.
   Consumes send_val/throw_exc for the plan. May set exception in context.
   :return: KOS_MODE_TRANSFER to continue draining plan, KOS_MODE_EXCEPTION if throw_exc set, KOS_MODE_NORMAL if plan done
*/
static KosEvalMode revive_plan(
    KosContext *ctx,
    PyObject *send_val,
    PyObject *throw_typ,
    PyObject *throw_exc) {

  KosTransferPlan *plan = KosContext_PLAN(ctx);

  if (throw_exc != NULL) {
    PyErr_SetObject(throw_typ, throw_exc);
    return KOS_MODE_EXCEPTION;
  }

  if (send_val != NULL) {
    Py_XINCREF(send_val);
    if (plan) {
      Py_XDECREF(KosTransferPlan_STEP_RESUME_INJECT(plan));
      KosTransferPlan_STEP_RESUME_INJECT(plan) = send_val;
    }
  }

  return KOS_MODE_TRANSFER;
}


/**
   Set up VM for "resume with no value" after AWAIT suspend.
   No inject; just continue. Call when send_val and throw_exc are both NULL and suspension_event was AWAIT.
*/
static KosEvalMode raise_await(KosContext *ctx) {
  (void) ctx;
  return KOS_MODE_NORMAL;
}


/**
   Set up VM for "resume with no value" after YIELD suspend.
*/
static KosEvalMode raise_yield(KosContext *ctx) {
  (void) ctx;
  return KOS_MODE_NORMAL;
}


/**
   Set up VM for "resume with no value" after YIELD_FROM suspend.
*/
static KosEvalMode raise_yield_from(KosContext *ctx) {
  (void) ctx;
  return KOS_MODE_NORMAL;
}


/**
   Walk chain from node toward root; return first WITH in PENDING_EXIT_AWAIT, or NULL.
*/
static KosDynWith *find_pending_exit_with(KosDynNode *node) {
  for (; node; node = KosDynNode_PARENT(node)) {
    if (KosDynNode_IS_WITH(node)
        && KosDynWith_PHASE((KosDynWith *) node) == KOS_WITH_PHASE_PENDING_EXIT_AWAIT)
      return (KosDynWith *) node;
  }
  return NULL;
}


/**
   Revive from AWAIT: push send_val or set throw_exc. Complete pending WITH exit if applicable.
*/
static KosEvalMode revive_await(KosContext *ctx, PyObject *send_val, PyObject *throw_typ, PyObject *throw_exc) {
  if (throw_exc != NULL) {
    PyErr_SetObject(throw_typ, throw_exc);
    return KOS_MODE_EXCEPTION;
  }
  if (KosContext_PENDING_DEFER(ctx) && send_val != NULL) {
    KosDefer_FeedResult((KosDefer *) KosContext_PENDING_DEFER(ctx), send_val);
    Py_CLEAR(KosContext_PENDING_DEFER(ctx));
  }
  if (send_val != NULL && CURRENT_FRAME(ctx)) {
    if (KosStackBlock_PushRef(&CURRENT_STACK(ctx), send_val) < 0)
      return KOS_MODE_EXCEPTION;
  }
  {
    KosDynWith *with_node = find_pending_exit_with(KosContext_DYN_TOP(ctx));
    if (with_node && kos_dyn_with_on_control_returned(ctx, with_node, 1) < 0)
      return KOS_MODE_EXCEPTION;
  }
  return KOS_MODE_NORMAL;
}


/**
   Revive from YIELD: push send_val or set throw_exc.
*/
static KosEvalMode revive_yield(KosContext *ctx, PyObject *send_val, PyObject *throw_typ, PyObject *throw_exc) {
  if (throw_exc != NULL) {
    PyErr_SetObject(throw_typ, throw_exc);
    return KOS_MODE_EXCEPTION;
  }
  if (send_val != NULL && CURRENT_FRAME(ctx)) {
    if (KosStackBlock_PushRef(&CURRENT_STACK(ctx), send_val) < 0)
      return KOS_MODE_EXCEPTION;
  }
  return KOS_MODE_NORMAL;
}


/**
   Revive from YIELD_FROM: push send_val or set throw_exc.
*/
static KosEvalMode revive_yield_from(KosContext *ctx, PyObject *send_val, PyObject *throw_typ, PyObject *throw_exc) {
  if (throw_exc != NULL) {
    PyErr_SetObject(throw_typ, throw_exc);
    return KOS_MODE_EXCEPTION;
  }
  if (send_val != NULL && CURRENT_FRAME(ctx)) {
    if (KosStackBlock_PushRef(&CURRENT_STACK(ctx), send_val) < 0)
      return KOS_MODE_EXCEPTION;
  }
  return KOS_MODE_NORMAL;
}


static int try_local_exception_handler(KosContext *ctx, KosEvent *out_ev);


static void decorate_exception_with_dynchain(KosContext *ctx, PyObject *exc_val) {

  if (! ctx || ! exc_val || ! PyExceptionInstance_Check(exc_val)) {
    return;
  }

  PyObject *frames = PyList_New(0);
  if (! frames) {
    PyErr_Clear();
    return;
  }

  for (KosDynNode *node = KosContext_DYN_TOP(ctx); node; node = KosDynNode_PARENT(node)) {
    if (! KosDynNode_IS_FRAME(node)) {
      continue;
    }
    KosDynFrame *frame = (KosDynFrame *) node;
    PyObject *meta = PyDict_New();
    if (! meta) {
      PyErr_Clear();
      continue;
    }

    KosCode *code = KosDynFrame_CODE(frame);
    uint16_t line = 0, col = 0;
    uint32_t pc = KosDynFrame_ACTING_PC(frame);
    if (code)
      kos_loc_lookup((PyObject *) code, pc, &line, &col);

    PyObject *name = code ? KosCode_NAME(code) : Py_None;
    PyObject *filename = code ? KosCode_FILENAME(code) : Py_None;

    PyDict_SetItemString(meta, "name", name ? name : Py_None);
    PyDict_SetItemString(meta, "filename", filename ? filename : Py_None);
    PyObject *line_o = PyLong_FromUnsignedLong((unsigned long) line);
    PyObject *col_o = PyLong_FromUnsignedLong((unsigned long) col);
    PyObject *pc_o = PyLong_FromUnsignedLong((unsigned long) pc);
    if (line_o && col_o && pc_o) {
      PyDict_SetItemString(meta, "line", line_o);
      PyDict_SetItemString(meta, "col", col_o);
      PyDict_SetItemString(meta, "pc", pc_o);
    }
    Py_XDECREF(line_o);
    Py_XDECREF(col_o);
    Py_XDECREF(pc_o);
    PyList_Append(frames, meta);
    Py_DECREF(meta);
  }

  if (PyObject_SetAttrString(exc_val, "__koskari_dynchain__", frames) < 0) {
    PyErr_Clear();
  }

  Py_DECREF(frames);
}


static void inject_eval_frame_traceback(
    KosContext *ctx,
    PyObject **exc_type,
    PyObject **exc_val,
    PyObject **exc_tb) {

  if (! ctx || ! exc_type || ! exc_val || ! *exc_val) {
    return;
  }

  if (KoskariContinuationApply && *exc_type
      && PyObject_IsSubclass(*exc_type, KoskariContinuationApply)) {
    return;
  }

#if PY_MINOR_VERSION < 12
  PyErr_NormalizeException(exc_type, exc_val, exc_tb);
#endif

  decorate_exception_with_dynchain(ctx, *exc_val);

  KosDynFrame *frame_top = CURRENT_FRAME(ctx);
  if (! frame_top || ! KosDynFrame_CODE(frame_top)) {
    return;
  }

  PyCodeObject *faux = KosCode_Faux((PyObject *) KosDynFrame_CODE(frame_top));
  if (! faux) {
    PyErr_Clear();
    return;
  }

  PyObject *globals = PyDict_New();
  if (! globals) {
    PyErr_Clear();
    return;
  }
  PyObject *modname = PyUnicode_FromString("koskari.native");
  if (modname) {
    PyDict_SetItemString(globals, "__name__", modname);
    Py_DECREF(modname);
  }
  PyObject *trace_data = PyObject_GetAttrString(*exc_val, "__koskari_dynchain__");
  if (trace_data) {
    PyDict_SetItemString(globals, "__koskari_trace__", trace_data);
    Py_DECREF(trace_data);
  } else {
    PyErr_Clear();
  }

  PyFrameObject *pyframe = PyFrame_New(PyThreadState_Get(), faux, globals, NULL);
  Py_DECREF(globals);
  if (! pyframe) {
    SAY("[eval] inject_eval_frame_traceback: PyFrame_New failed\n");
    PyErr_Clear();
    return;
  }

  PyErr_Restore(*exc_type, *exc_val, *exc_tb);
  *exc_type = NULL;
  *exc_val = NULL;
  *exc_tb = NULL;

  if (PyTraceBack_Here(pyframe) < 0) {
    SAY("[eval] inject_eval_frame_traceback: PyTraceBack_Here failed\n");
    PyErr_Clear();
  }
  Py_DECREF(pyframe);

  SAY("[eval] inject_eval_frame_traceback: PyTraceBack_Here succeeded\n");
  PyErr_Fetch(exc_type, exc_val, exc_tb);

  /* PyTraceBack_Here updates the thread-state traceback only; the exception
     value's __traceback__ is not updated. Attach the extended chain to the
     value so it survives PyErr_SetObject in coro.c (which uses value as-is). */
  if (*exc_val && *exc_tb && PyExceptionInstance_Check(*exc_val)) {
    if (PyObject_SetAttrString(*exc_val, "__traceback__", *exc_tb) < 0)
      PyErr_Clear();
  }
}

/**
   Intercept a KoskariContinuationApply exception and install the appropriate
   transfer plan.  PyErr must be set to KoskariContinuationApply on entry.

   Own-token path: the continuation targets this eval context.  Extract the
   continuation and resume value, clear PyErr, and call kos_invoke_continuation
   to install a direct-apply transfer plan.

   Foreign-token path: the continuation targets a different eval context.
   Fetch and clear PyErr, insert a KosDynCCRaise sentinel at the root of the
   dynamic chain, and build a NULL-target transfer plan that unwinds all local
   WITH/finally extents and then re-raises the original exception via the
   sentinel's unwind callback.  The eval loop then exits with KOS_EV_RAISE so
   the exception propagates upward to the owning context.

   :return: KOS_MODE_TRANSFER on success, KOS_MODE_RAISE on error
*/
static KosEvalMode handle_continuation_apply(
    KosContext *ctx,
    KosEvent *out_ev) {

  (void) out_ev;

  PyObject *typ = NULL, *val = NULL, *tb = NULL;
  PyErr_Fetch(&typ, &val, &tb);
  Py_XDECREF(tb);

  if (! val) {
    Py_XDECREF(typ);
    PyErr_SetString(PyExc_RuntimeError,
                    "KoskariContinuationApply: missing exception value");
    return KOS_MODE_RAISE;
  }

  PyObject *exc_target = PyObject_GetAttrString(val, "target");
  if (! exc_target) {
    Py_DECREF(typ);
    Py_DECREF(val);
    return KOS_MODE_RAISE;
  }

  int is_own = (exc_target == (PyObject *) KosContext_TOKEN(ctx));
  Py_DECREF(exc_target);

  if (is_own) {
    PyObject *cont_obj = PyObject_GetAttrString(val, "continuation");
    PyObject *args_obj = PyObject_GetAttrString(val, "args");

    if (cont_obj && PyObject_TypeCheck(cont_obj, KOS_CALLABLE_PYTYPE(KosPyContinuationType))) {
      KosContinuation *k = ((KosPyContinuation *) cont_obj)->captured;
      PyObject *resume_val = Py_None;

      if (args_obj && PyTuple_Check(args_obj) && PyTuple_GET_SIZE(args_obj) > 0) {
        resume_val = PyTuple_GET_ITEM(args_obj, 0);
        Py_INCREF(resume_val);
      } else {
        Py_INCREF(resume_val);
      }

      Py_XDECREF(args_obj);
      PyErr_Clear();

      if (k && kos_invoke_continuation(ctx, k, resume_val, 0, cont_obj) == 0) {
        Py_DECREF(resume_val);
        Py_DECREF(val);
        Py_DECREF(cont_obj);
        return KOS_MODE_TRANSFER;
      }
      Py_DECREF(resume_val);
    } else {
      Py_XDECREF(args_obj);
    }

    Py_XDECREF(cont_obj);
    Py_DECREF(val);

    if (! PyErr_Occurred())
      PyErr_SetString(PyExc_RuntimeError,
                      "KoskariContinuationApply: invalid continuation");
    return KOS_MODE_RAISE;
  }

  /* Foreign-token path: unwind this context to null and re-raise. */
  PyErr_Clear();
  KosTransferPlan *plan = NULL;
  if (kos_build_cc_unwind_plan(ctx, typ, val, &plan) < 0) {
    Py_DECREF(typ);
    Py_DECREF(val);
    return KOS_MODE_RAISE;
  }
  Py_DECREF(typ);
  Py_DECREF(val);

  if (KosContext_PLAN(ctx))
    kos_free_transfer_plan(KosContext_PLAN(ctx));
  KosContext_PLAN(ctx) = plan;
  KosContext_FLAGS(ctx) |= KOS_CTX_IN_TRANSFER;
  return KOS_MODE_TRANSFER;
}


/**
   Process the current exception (in PyErr or context). Handles
   KoskariContinuationApply (invoke continuation or unwind-and-reraise), local
   handlers (try/except/finally, WITH exit, unwind), or returns RAISE.
   Call only when mode is EXCEPTION.

   :return: KOS_MODE_NORMAL or KOS_MODE_TRANSFER to continue,
            KOS_MODE_RAISE to propagate (out_ev set to KOS_EV_RAISE)
*/
static KosEvalMode process_raise(KosContext *ctx, KosEvent *out_ev) {
  if (! PyErr_Occurred()) {
    KosEvent_KIND(out_ev) = KOS_EV_RAISE;
    KosEvent_PAYLOAD(out_ev) = NULL;
    KosEvent_EXC_TYPE(out_ev) = NULL;
    return KOS_MODE_RAISE;
  }
  if (KoskariContinuationApply && PyErr_ExceptionMatches(KoskariContinuationApply))
    return handle_continuation_apply(ctx, out_ev);
  clear_pending_agen_on_error(ctx);
  if (try_local_exception_handler(ctx, out_ev))
    return KOS_MODE_NORMAL;
  /* try_local_exception_handler already set out_ev and ctx flags when it returned 0 */
  return KOS_MODE_RAISE;
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

/* Sentinel fromlist for IMPORT / IMPORT_REL opcodes.  Passing ("*",) as
   fromlist to PyImport_ImportModuleLevelObject causes it to return the
   deepest (leaf) submodule rather than the topmost package, matching
   importlib.import_module semantics.  Initialized in koskari_eval_init. */
PyObject *_import_fromlist = NULL;

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
static PyObject *str_class = NULL;


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
    KOS_TRACE_FL_ALLOC("context", 1);
    memset(ctx, 0, sizeof(KosContext));
  } else {
    KOS_TRACE_FL_ALLOC("context", 0);
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
      KOS_TRACE_FL_RETURN("context", 0);
      memset(ctx, 0, sizeof(KosContext));
      KosContext_FREELIST_NEXT(ctx) = context_freelist;
      context_freelist = ctx;
      context_freecount++;
    } else {
      KOS_TRACE_FL_RETURN("context", 1);
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
          KOS_TRACE_FL_RETURN("context", 0);
          memset(ctx, 0, sizeof(KosContext));
          KosContext_FREELIST_NEXT(ctx) = context_freelist;
          context_freelist = ctx;
          context_freecount++;
        } else {
          KOS_TRACE_FL_RETURN("context", 1);
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

  KosContext_UNWIND_TARGET_FRAME(ctx) = NULL;
  KosContext_PENDING_RETURN_VALUE(ctx) = NULL;
  KosContext_PENDING_RETURN_TO_PARENT(ctx) = 0;

  /* Initialize transfer plan state */
  KosContext_PLAN(ctx) = NULL;
  KosContext_PLAN_STACK(ctx) = NULL;
  KosContext_CHAIN_VERSION(ctx) = 0;
  KosContext_INVOCATION_SEGMENT_TOP(ctx) = NULL;
  KosContext_RETURN_TARGET_CHAIN(ctx) = NULL;
  KosContext_RETURN_TARGET_PC(ctx) = (uint32_t) -1;
  KosContext_DELIMITED_RETURN_FRAME(ctx) = NULL;
  KosContext_DELIMITED_RETURN_PC(ctx) = (uint32_t) -1;
  KosContext_DELIMITED_RETURN_NODE(ctx) = NULL;

  /* Initialize Python await bridging */
  KosContext_PY_AWAIT_ITER(ctx) = NULL;
  KosContext_PY_LAST_YIELDED(ctx) = NULL;
  KosContext_PY_RESUME_INJECT(ctx) = NULL;
  KosContext_PY_RESUME_KIND(ctx) = KOS_RESUME_NONE;
  KosContext_PENDING_DEFER(ctx) = NULL;

  /* Initialize exception injection */
  KosContext_INJECT_MODE(ctx) = KOS_INJECT_NONE;
  KosContext_INJECT_TYPE(ctx) = NULL;
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

  /* Free return target chain (delimited k return path) */
  if (KosContext_RETURN_TARGET_CHAIN(ctx)) {
    kos_dynchain_free(KosContext_RETURN_TARGET_CHAIN(ctx), NULL);
    KosContext_RETURN_TARGET_CHAIN(ctx) = NULL;
  }

  /* Free any active transfer plan */
  kos_free_transfer_plan(KosContext_PLAN(ctx));
  while (KosContext_PLAN_STACK(ctx)) {
    KosTransferPlan *p = KosContext_PLAN_STACK(ctx);
    KosContext_PLAN_STACK(ctx) = KosTransferPlan_SUSPENDED(p);
    kos_free_transfer_plan(p);
  }

  Py_XDECREF(KosContext_PY_AWAIT_ITER(ctx));
  Py_XDECREF(KosContext_PY_LAST_YIELDED(ctx));
  Py_XDECREF(KosContext_PY_RESUME_INJECT(ctx));
  Py_XDECREF(KosContext_PENDING_DEFER(ctx));
  KosContext_PENDING_DEFER(ctx) = NULL;
  Py_XDECREF(KosContext_INJECT_TYPE(ctx));
  KosContext_INJECT_TYPE(ctx) = NULL;
  Py_XDECREF(KosContext_INJECT_VALUE(ctx));
  Py_XDECREF(KosContext_PENDING_RETURN_VALUE(ctx));
  KosContext_PENDING_RETURN_VALUE(ctx) = NULL;
  Py_XDECREF(KosContext_PENDING_AGEN_DEFAULT(ctx));
  Py_XDECREF(KosContext_DOMAIN(ctx));
  KosContext_DOMAIN(ctx) = NULL;

  /* Clear pending WITH state (exc_* on any WITH in PENDING_EXIT_AWAIT) */
  kos_clear_pending_with_exit(ctx);

  if (context_freecount < CONTEXT_MAX_FREE) {
    KOS_TRACE_FL_RETURN("context", 0);
    memset(ctx, 0, sizeof(KosContext));
    KosContext_FREELIST_NEXT(ctx) = context_freelist;
    context_freelist = ctx;
    context_freecount++;
  } else {
    KOS_TRACE_FL_RETURN("context", 1);
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
  Py_VISIT(KosContext_PENDING_DEFER(ctx));
  Py_VISIT(KosContext_INJECT_TYPE(ctx));
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


static int kos_tail_koscall(
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
    if (PyGen_FetchStopIterationValue(&value) < 0) {
      /* FetchStopIterationValue failed and set a new exception; propagate. */
      Py_DECREF(await_iter);
      return NULL;
    }
    Py_DECREF(await_iter);

    if (! value) {
      value = Py_None;
      Py_INCREF(value);
    }

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
KOS_OP_INLINE int push_result(KosContext *ctx, PyObject *val) {
  if (likely(val))
    return KosStackBlock_PUSH(&CURRENT_STACK(ctx), val);
  return -1;
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

  /* Defer is not auto-awaited; push as plain value (explicit AWAIT drives it). */
  if (KosDefer_Check(val)) {
    return KosStackBlock_PUSH(&CURRENT_STACK(ctx), val);
  }

  PyTypeObject *type = Py_TYPE(val);
  SAYF("[eval] push_or_await entry is_awaitable=%d\n",
       (type->tp_as_async && type->tp_as_async->am_await) ? 1 : 0);
  if (! type->tp_as_async || ! type->tp_as_async->am_await) {
    return KosStackBlock_PUSH(&CURRENT_STACK(ctx), val);
  }

  int suspended = 0;
  PyObject *yielded = NULL;
  PyObject *result = eager_step_awaitable(val, ctx, &suspended, &yielded);
  Py_DECREF(val);

  if (result) {
    return KosStackBlock_PUSH(&CURRENT_STACK(ctx), result);
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


/* Public wrapper for external callers; delegates to static push_or_await. */
int kos_push_or_await(KosContext *ctx, KosEvent *out_ev, PyObject *val) {
  return push_or_await(ctx, out_ev, val);
}


/* Forward declaration for WITH helpers (frame_ns_store is defined later). */
static int frame_ns_store(KosContext *ctx, PyObject *name, PyObject *val);


/**
   Finalize provisional WITH node after enter completed synchronously.
   Block and node already exist. Store result to name, set entered=1 and phase=IDLE.
   Consumes result.
*/
static int finalize_provisional_with_enter(KosContext *ctx, PyObject *result,
                                           KosDynWith *with_node) {

  PyObject *name = KosDynWith_NAME(with_node);
  if (name && name != Py_None) {
    if (frame_ns_store(ctx, name, result) < 0) {
      Py_DECREF(result);
      return -1;
    }
  }
  Py_DECREF(result);

  KosDynWith_ENTERED(with_node) = 1;
  KosDynWith_PHASE(with_node) = KOS_WITH_PHASE_IDLE;
  return 0;
}


/**
   Complete provisional WITH enter when enter frame returns (sync or after await).
   Pops result from stack, stores to name, sets entered=1 and phase=IDLE.
*/
int kos_dyn_with_on_enter_control_returned(KosContext *ctx, KosDynWith *with_node) {

  if (! with_node || KosDynWith_PHASE(with_node) != KOS_WITH_PHASE_PENDING_ENTER_AWAIT)
    return 0;

  if (! KosContext_FRAME_TOP(ctx))
    return -1;

  PyObject *result = KosStackBlock_PopRef(&CURRENT_STACK(ctx));
  if (! result)
    return -1;

  PyObject *name = KosDynWith_NAME(with_node);
  if (name && name != Py_None) {
    if (frame_ns_store(ctx, name, result) < 0) {
      Py_DECREF(result);
      return -1;
    }
  }
  Py_DECREF(result);

  KosDynWith_ENTERED(with_node) = 1;
  KosDynWith_PHASE(with_node) = KOS_WITH_PHASE_IDLE;
  return 0;
}


/**
   Start awaiting WITH exit result.
*/
int kos_with_exit_start_await(KosContext *ctx, KosEvent *out_ev,
                              PyObject *awaitable) {

  return push_or_await(ctx, out_ev, awaitable);
}


/**
   Complete WITH exit after control returned (await or RETURN). Pops result
   from stack, applies swallow, drops block, optionally pops node. Reads
   exc_* from the node. Called from revive (value on TOS) or
   kos_dynnode_on_control_returned (value on frame stack).

   :param do_pop_node: If true, unlink and free the WITH node. If false,
                       only do semantic work (caller will unlink/free).
*/
int kos_dyn_with_on_control_returned(KosContext *ctx, KosDynWith *with_node,
                                     int do_pop_node) {

  if (! with_node || KosDynWith_PHASE(with_node) != KOS_WITH_PHASE_PENDING_EXIT_AWAIT)
    return 0;

  if (! KosContext_FRAME_TOP(ctx))
    return -1;

  PyObject *result = KosStackBlock_PopRef(&CURRENT_STACK(ctx));
  if (! result)
    return -1;

  PyObject *typ = KosDynWith_EXC_TYP(with_node);
  PyObject *val = KosDynWith_EXC_VAL(with_node);
  PyObject *tb = KosDynWith_EXC_TB(with_node);
  KosDynWith_EXC_TYP(with_node) = NULL;
  KosDynWith_EXC_VAL(with_node) = NULL;
  KosDynWith_EXC_TB(with_node) = NULL;
  KosDynWith_PHASE(with_node) = KOS_WITH_PHASE_IDLE;

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
    if (do_pop_node) {
      kos_dyn_pop_with(ctx, with_node);
    }
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
    if (do_pop_node) {
      kos_dyn_pop_with(ctx, with_node);
    }
    PyErr_Restore(typ, val, tb);
    return -1;
  }

  Py_DECREF(result);
  Py_XDECREF(typ);
  Py_XDECREF(val);
  Py_XDECREF(tb);
  PyObject *body_val = KosStackBlock_PopRef(&CURRENT_STACK(ctx));
  if (! body_val)
    return -1;
  if (KosStackBlock_DropBlock(&CURRENT_STACK(ctx)) < 0) {
    Py_DECREF(body_val);
    return -1;
  }
  if (KosContext_FRAME_TOP(ctx))
    KosDynFrame_PC(KosContext_FRAME_TOP(ctx)) = KosDynWith_RESUME_PC(with_node);
  if (do_pop_node) {
    kos_dyn_pop_with(ctx, with_node);
  }
  if (KosStackBlock_PushRef(&CURRENT_STACK(ctx), body_val) < 0) {
    Py_DECREF(body_val);
    return -1;
  }
  Py_DECREF(body_val);
  return 0;
}


int kos_complete_pending_with_exit(KosContext *ctx) {
  KosDynWith *node = find_pending_exit_with(KosContext_DYN_TOP(ctx));
  return node ? kos_dyn_with_on_control_returned(ctx, node, 1) : 0;
}


int kos_complete_pending_with_exit_semantic(KosContext *ctx) {
  KosDynWith *node = find_pending_exit_with(KosContext_DYN_TOP(ctx));
  return node ? kos_dyn_with_on_control_returned(ctx, node, 0) : 0;
}


/**
   Clear exc_* on any WITH node in PENDING_EXIT_AWAIT on the chain.
*/
void kos_clear_pending_with_exit(KosContext *ctx) {
  KosDynNode *n = KosContext_DYN_TOP(ctx);
  for (; n; n = KosDynNode_PARENT(n)) {
    if (KosDynNode_IS_WITH(n)
        && KosDynWith_PHASE((KosDynWith *) n) == KOS_WITH_PHASE_PENDING_EXIT_AWAIT) {
      Py_CLEAR(KosDynWith_EXC_TYP((KosDynWith *) n));
      Py_CLEAR(KosDynWith_EXC_VAL((KosDynWith *) n));
      Py_CLEAR(KosDynWith_EXC_TB((KosDynWith *) n));
      KosDynWith_PHASE((KosDynWith *) n) = KOS_WITH_PHASE_IDLE;
    }
  }
}


int kos_dyn_dyn_with_wind(KosContext *ctx, KosDynWith *node, KosEvent *out_ev) {
  PyObject *enter_fn = KosDynWith_ENTER(node);
  int r = kos_callable_vcall(ctx, out_ev, enter_fn, NULL, 0, NULL);
  if (r == 1)
    return KOS_DYN_UNWIND_ASYNC_STARTED;
  if (r < 0)
    return -1;

  PyObject *result = KosStackBlock_PopRef(&CURRENT_STACK(ctx));
  if (! result)
    return -1;

  PyObject *name = KosDynWith_NAME(node);
  if (name && name != Py_None) {
    if (frame_ns_store(ctx, name, result) < 0) {
      Py_DECREF(result);
      return -1;
    }
  }
  Py_DECREF(result);

  KosDynWith_ENTERED(node) = 1;
  return 0;
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

  PyErr_Format(PyExc_TypeError,
               "frame namespace must be Namespace, got %R", ns);
  return -1;
}


/**
   Bind name to reference in the current frame namespace.
*/
static int frame_ns_bind(KosContext *ctx, PyObject *name, PyObject *ref) {
  PyObject *ns = CURRENT_NS(ctx);

  if (KosNamespace_Check(ns))
    return KosNamespace_Bind(ns, name, ref);

  PyErr_Format(PyExc_TypeError,
               "frame namespace must be Namespace, got %R", ns);
  return -1;
}


/**
   Clear name from the current frame namespace.
*/
static int frame_ns_clear(KosContext *ctx, PyObject *name) {
  PyObject *ns = CURRENT_NS(ctx);
  if (KosNamespace_Check(ns))
    return KosNamespace_Clear(ns, name);
  PyErr_Format(PyExc_TypeError,
               "frame namespace must be Namespace, got %R", ns);
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
   CALL, CALL_*, TAIL_* - calls
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

  /* Undelimited: check liveness before apply or raise. */
  if (KosContinuation_PROMPT_ID(k) == KOS_PROMPT_ID_UNDELIMITED) {
    PyObject *token_ref = KosContinuation_TOKEN_REF(k);
    int target_dead = 0;
    if (! token_ref) {
      target_dead = 1;
    } else if (PyObject_TypeCheck(token_ref, &KosEvaluatorTokenType)) {
      KosEvaluatorToken *token = (KosEvaluatorToken *) token_ref;
      if (token->live_depth == 0)
        target_dead = 1;
    }
    if (target_dead) {
      if (KoskariContinuationDead)
        PyErr_SetString(KoskariContinuationDead,
                        "continuation target context is no longer alive");
      else
        PyErr_SetString(PyExc_RuntimeError,
                        "continuation target context is no longer alive");
      Py_DECREF(resume_val);
      return -1;
    }
  }

  /* Delimited: prompt need not be in chain; push model grafts onto current
   * chain and returns to call site on POP_PROMPT. */

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
   call_native_flattened_function: callable, args, and kwds are borrowed.
   Pass args and kwds to PrepareCallNamespace, push frame.
*/
static int call_native_flattened_function(
    KosContext *ctx,
    PyObject *callable,
    PyObject *args,
    PyObject *kwds,
    KosEvent *out_ev) {

  PyObject *frame_ns = KosFunction_PrepareCallNamespace(
      callable, args, kwds);
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
   call_tail_flattened_function: callable, args, and kwds are borrowed.
   Prepare call namespace from formals, replace frame via kos_tail_koscall.
*/
static int call_tail_flattened_function(
    KosContext *ctx,
    PyObject *callable,
    PyObject *args,
    PyObject *kwds,
    KosEvent *out_ev) {

  PyObject *frame_ns = KosFunction_PrepareCallNamespace(
      callable, args, kwds);
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
   Default path for callables without install: Python call + push_or_await.
   Used when the callable is not KosCallable or has no install slot.
   callable, args_tuple, kwds_dict are borrowed.
*/
static int call_default_python_call(
    KosContext *ctx,
    KosEvent *out_ev,
    PyObject *callable,
    PyObject *args_tuple,
    PyObject *kwds_dict) {

  PyObject *result = (! args_tuple || PyTuple_GET_SIZE(args_tuple) == 0)
      ? PyObject_CallNoArgs(callable)
      : PyObject_Call(callable, args_tuple, kwds_dict ? kwds_dict : NULL);
  if (! result)
    return -1;
  return push_or_await(ctx, out_ev, result);
}


static int kos_apply_build_args_tuple(PyObject *arglist, PyObject **out_args) {
  if (! arglist || arglist == Py_None) {
    *out_args = PyTuple_New(0);
  } else if (KosPair_Check(arglist)) {
    PyObject *iter = KosPair_Unpack(arglist);
    if (! iter)
      return -1;
    *out_args = PySequence_Tuple(iter);
    Py_DECREF(iter);
  } else if (PyTuple_Check(arglist)) {
    *out_args = arglist;
    Py_INCREF(*out_args);
  } else if (PyList_Check(arglist)) {
    *out_args = PyList_AsTuple(arglist);
  } else {
    *out_args = PySequence_Tuple(arglist);
  }

  if (! *out_args)
    return -1;
  return 0;
}


int kos_callable_call(KosContext *ctx, KosEvent *out_ev,
                      PyObject *callable, PyObject *args_tuple, PyObject *kwds_dict) {
  if (KosCallable_Check(callable)) {
    KosInstallFunc fn = KosCallableType_INSTALL(Py_TYPE(callable));
    if (fn)
      return fn(ctx, out_ev, callable, args_tuple, kwds_dict);
  }
  return call_default_python_call(ctx, out_ev, callable, args_tuple, kwds_dict);
}


int kos_callable_tail_call(KosContext *ctx, KosEvent *out_ev,
                           PyObject *callable, PyObject *args_tuple, PyObject *kwds_dict) {
  if (KosCallable_Check(callable)) {
    KosInstallFunc fn = KosCallableType_TAIL_INSTALL(Py_TYPE(callable));
    if (fn)
      return fn(ctx, out_ev, callable, args_tuple, kwds_dict);
    fn = KosCallableType_INSTALL(Py_TYPE(callable));
    if (fn) {
      KosDynFrame *frame = kos_dyn_push_frame_empty(ctx);
      if (! frame)
        return -1;
      return fn(ctx, out_ev, callable, args_tuple, kwds_dict);
    }
  }
  return call_default_python_call(ctx, out_ev, callable, args_tuple, kwds_dict);
}


/**
   Vectorcall fallback for plain Python callables. Passes the C array directly
   to PyObject_Vectorcall, avoiding any tuple construction.
   callable, args, and kwnames are all borrowed.
*/
static int call_default_python_vcall(
    KosContext *ctx,
    KosEvent *out_ev,
    PyObject *callable,
    PyObject *const *args,
    Py_ssize_t nargs,
    PyObject *kwnames) {

  // TODO: remove the non-KosCallable support from kos_callable and family

  PyObject *result = PyObject_Vectorcall(callable, args, (size_t) nargs, kwnames);
  if (! result)
    return -1;
  return push_or_await(ctx, out_ev, result);
}


/**
   Build a tuple from a C args array. Used as a fallback in kos_callable_vcall
   when a KosCallable type has only the legacy install slot, not the vector one.
*/
static PyObject *vcall_build_args_tuple(PyObject *const *args, Py_ssize_t nargs) {
  PyObject *tup = PyTuple_New(nargs);
  if (! tup)
    return NULL;
  for (Py_ssize_t i = 0; i < nargs; i++) {
    Py_INCREF(args[i]);
    PyTuple_SET_ITEM(tup, i, args[i]);
  }
  return tup;
}


/**
   Build a kwds dict from a C args slice and a kwnames tuple. Used as a
   fallback when a KosCallable type has only the legacy install slot.
*/
static PyObject *vcall_build_kwds_dict(
    PyObject *const *kw_vals,
    PyObject *kwnames) {

  Py_ssize_t nkw = PyTuple_GET_SIZE(kwnames);
  PyObject *kwds = PyDict_New();
  if (! kwds)
    return NULL;
  for (Py_ssize_t i = 0; i < nkw; i++) {
    if (PyDict_SetItem(kwds, PyTuple_GET_ITEM(kwnames, i), kw_vals[i]) < 0) {
      Py_DECREF(kwds);
      return NULL;
    }
  }
  return kwds;
}


int kos_callable_vcall(
    KosContext *ctx,
    KosEvent *out_ev,
    PyObject *callable,
    PyObject *const *args,
    Py_ssize_t nargs,
    PyObject *kwnames) {

  if (KosCallable_Check(callable)) {
    KosVectorInstallFunc vfn = KosCallableType_VINSTALL(Py_TYPE(callable));
    if (vfn)
      return vfn(ctx, out_ev, callable, args, nargs, kwnames);

    /* fallback: build tuple (and dict) for the legacy install slot */
    KosInstallFunc fn = KosCallableType_INSTALL(Py_TYPE(callable));
    if (fn) {
      PyObject *tup = (nargs > 0) ? vcall_build_args_tuple(args, nargs) : NULL;
      if (nargs > 0 && ! tup)
        return -1;
      PyObject *kwds = NULL;
      if (kwnames) {
        kwds = vcall_build_kwds_dict(args + nargs, kwnames);
        if (! kwds) {
          Py_XDECREF(tup);
          return -1;
        }
      }
      int r = fn(ctx, out_ev, callable, tup, kwds);
      Py_XDECREF(tup);
      Py_XDECREF(kwds);
      return r;
    }
  }

  // TODO: remove the non-KosCallable support from kos_callable and family
  return call_default_python_vcall(
    ctx,
    out_ev,
    callable,
    args,
    nargs,
    kwnames);
}


/**
   kos_callable_tail_vcall: dispatch to tail install for KosCallable,
   else call_default_python_vcall.

   STACK OWNERSHIP: When a tail install handler is invoked (tp_kos_tail_vinstall
   or fallback tail paths), it takes ownership of the caller's stack. The handler
   either frees the current frame (kos_tail_koscall) or resets it in place
   (kos_dynframe_reset_for_tail). Callers must NOT call KosStackBlock_DropNoErr
   after kos_callable_tail_vcall returns in the KosCallable path. Only the plain
   Python path (call_default_python_vcall) leaves the caller's stack valid; in
   that path the caller must DropNoErr before push_or_await.
*/
int kos_callable_tail_vcall(
    KosContext *ctx,
    KosEvent *out_ev,
    PyObject *callable,
    PyObject *const *args,
    Py_ssize_t nargs,
    PyObject *kwnames) {

  if (KosCallable_Check(callable)) {
    KosVectorInstallFunc vfn = KosCallableType_TAIL_VINSTALL(Py_TYPE(callable));
    if (vfn)
      return vfn(ctx, out_ev, callable, args, nargs, kwnames);

    /* fallback: try the non-tail vector slot, pushing an empty frame */
    vfn = KosCallableType_VINSTALL(Py_TYPE(callable));
    if (vfn) {
      KosDynFrame *frame = kos_dyn_push_frame_empty(ctx);
      if (! frame)
        return -1;
      return vfn(ctx, out_ev, callable, args, nargs, kwnames);
    }

    /* fallback: build tuple (and dict) for legacy install slots */
    KosInstallFunc fn = KosCallableType_TAIL_INSTALL(Py_TYPE(callable));
    if (fn) {
      PyObject *tup = (nargs > 0) ? vcall_build_args_tuple(args, nargs) : NULL;
      if (nargs > 0 && ! tup)
        return -1;
      PyObject *kwds = NULL;
      if (kwnames) {
        kwds = vcall_build_kwds_dict(args + nargs, kwnames);
        if (! kwds) {
          Py_XDECREF(tup);
          return -1;
        }
      }
      int r = fn(ctx, out_ev, callable, tup, kwds);
      Py_XDECREF(tup);
      Py_XDECREF(kwds);
      return r;
    }
    fn = KosCallableType_INSTALL(Py_TYPE(callable));
    if (fn) {
      KosDynFrame *frame = kos_dyn_push_frame_empty(ctx);
      if (! frame)
        return -1;
      PyObject *tup = (nargs > 0) ? vcall_build_args_tuple(args, nargs) : NULL;
      if (nargs > 0 && ! tup)
        return -1;
      PyObject *kwds = NULL;
      if (kwnames) {
        kwds = vcall_build_kwds_dict(args + nargs, kwnames);
        if (! kwds) {
          Py_XDECREF(tup);
          return -1;
        }
      }
      int r = fn(ctx, out_ev, callable, tup, kwds);
      Py_XDECREF(tup);
      Py_XDECREF(kwds);
      return r;
    }
  }

  // TODO: remove the non-KosCallable support from kos_callable and family
  return call_default_python_vcall(
    ctx,
    out_ev,
    callable,
    args,
    nargs,
    kwnames);
}


/* === Install slot implementations === */


int kos_install_function(
    KosContext *ctx,
    KosEvent *out_ev,
    PyObject *callable,
    PyObject *args_tuple,
    PyObject *kwds_dict) {

  return call_native_flattened_function(
      ctx, callable, args_tuple, kwds_dict, out_ev);
}


int kos_tail_install_function(
    KosContext *ctx,
    KosEvent *out_ev,
    PyObject *callable,
    PyObject *args_tuple,
    PyObject *kwds_dict) {

  return call_tail_flattened_function(
      ctx, callable, args_tuple, kwds_dict, out_ev);
}


int kos_vinstall_function(
    KosContext *ctx,
    KosEvent *out_ev,
    PyObject *callable,
    PyObject *const *args,
    Py_ssize_t nargs,
    PyObject *kwnames) {

  PyObject *frame_ns = KosFunction_PrepareCallNamespaceFromVector(
      callable, args, nargs, kwnames);
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


int kos_tail_vinstall_function(
    KosContext *ctx,
    KosEvent *out_ev,
    PyObject *callable,
    PyObject *const *args,
    Py_ssize_t nargs,
    PyObject *kwnames) {

  PyObject *frame_ns = KosFunction_PrepareCallNamespaceFromVector(
      callable, args, nargs, kwnames);
  if (! frame_ns)
    return -1;
  int r = kos_tail_koscall(ctx, (KosCode *) KosFunction_CODE(callable),
                           frame_ns);
  if (r < 0)
    Py_DECREF(frame_ns);
  (void) out_ev;
  return r;
}


int kos_install_expression(
    KosContext *ctx,
    KosEvent *out_ev,
    PyObject *callable,
    PyObject *args_tuple,
    PyObject *kwds_dict) {

  if (args_tuple && PyTuple_GET_SIZE(args_tuple) != 0) {
    PyErr_SetString(PyExc_TypeError, "Expression takes no arguments");
    return -1;
  }
  if (kwds_dict && PyDict_Size(kwds_dict) != 0) {
    PyErr_SetString(PyExc_TypeError, "Expression takes no named arguments");
    return -1;
  }
  return call_native_flattened_expr(ctx, callable, out_ev);
}


int kos_tail_install_expression(
    KosContext *ctx,
    KosEvent *out_ev,
    PyObject *callable,
    PyObject *args_tuple,
    PyObject *kwds_dict) {

  if (args_tuple && PyTuple_GET_SIZE(args_tuple) != 0) {
    PyErr_SetString(PyExc_TypeError, "Expression takes no arguments");
    return -1;
  }
  if (kwds_dict && PyDict_Size(kwds_dict) != 0) {
    PyErr_SetString(PyExc_TypeError, "Expression takes no named arguments");
    return -1;
  }
  return call_tail_flattened_expr(ctx, callable, out_ev);
}


int kos_install_continuation(
    KosContext *ctx,
    KosEvent *out_ev,
    PyObject *callable,
    PyObject *args_tuple,
    PyObject *kwds_dict) {

  (void) kwds_dict;
  return call_call_continuation(ctx, callable, args_tuple, out_ev, NULL);
}


/**
   Native handler for apply (non-flattening path). Stack has [fn, seq_args, kwds]
   (kwds may be Py_None). Pops them, PyObject_Call, then pops the apply frame,
   pushes result onto parent stack, and push_or_await. The apply frame has
   code=NULL so we must detach it before continuing; otherwise the eval loop
   would segfault in prepare_dispatch.
*/
static int apply_native_handler(
    KosContext *ctx,
    KosEvent *out_ev,
    int is_resume,
    PyObject *delivered_value) {

  (void) delivered_value;
  if (unlikely(is_resume != 0)) {
    PyErr_SetString(PyExc_RuntimeError, "apply native handler resume not implemented");
    return -1;
  }

  PyObject *kwds = KosStackBlock_PopRef(&CURRENT_STACK(ctx));
  PyObject *seq_args = KosStackBlock_PopRef(&CURRENT_STACK(ctx));
  PyObject *fn = KosStackBlock_PopRef(&CURRENT_STACK(ctx));
  if (! fn || ! seq_args) {
    Py_XDECREF(kwds);
    Py_XDECREF(seq_args);
    Py_XDECREF(fn);
    return -1;
  }
  if (kwds == Py_None)
    kwds = NULL;

  KosDynFrame_NATIVE_HANDLER(CURRENT_FRAME(ctx)) = NULL;
  PyObject *result = PyObject_Call(fn, seq_args, kwds);
  Py_DECREF(fn);
  Py_DECREF(seq_args);
  Py_XDECREF(kwds);
  if (! result)
    return -1;

  /* Pop apply frame (code=NULL) and deliver result to parent. */
  KosDynFrame *detached = NULL;
  if (kos_dyn_unwind_to_frame(ctx, CURRENT_FRAME(ctx), out_ev) < 0) {
    Py_DECREF(result);
    return -1;
  }
  if (kos_dyn_detach_top_frame(ctx, &detached) < 0) {
    Py_DECREF(result);
    return -1;
  }
  if (detached)
    kos_dynframe_free(detached);

  return push_or_await(ctx, out_ev, result);
}


static int kos_install_apply_impl(
    KosContext *ctx,
    KosEvent *out_ev,
    PyObject *callable,
    PyObject *args_tuple,
    int is_tail) {

  (void) callable;
  Py_ssize_t n = args_tuple ? PyTuple_GET_SIZE(args_tuple) : 0;
  if (n < 1 || n > 3) {
    PyErr_SetString(PyExc_TypeError, "apply expects 1, 2, or 3 arguments");
    return -1;
  }

  PyObject *fn = PyTuple_GET_ITEM(args_tuple, 0);
  PyObject *arglist = (n >= 2) ? PyTuple_GET_ITEM(args_tuple, 1) : NULL;
  PyObject *kwds = (n >= 3) ? PyTuple_GET_ITEM(args_tuple, 2) : NULL;
  PyObject *seq_args = NULL;

  if (kos_apply_build_args_tuple(arglist, &seq_args) < 0)
    return -1;

  if (kwds == Py_None)
    kwds = NULL;

  if (kwds && ! PyDict_Check(kwds)) {
    PyErr_SetString(PyExc_TypeError, "apply kwargs must be a dict or None");
    Py_DECREF(seq_args);
    return -1;
  }

  /* Flattening: delegate to inner callable's install */
  if (KosCallable_Check(fn)) {
    KosInstallFunc inner = is_tail
        ? KosCallableType_TAIL_INSTALL(Py_TYPE(fn))
        : KosCallableType_INSTALL(Py_TYPE(fn));
    if (inner) {
      int r = inner(ctx, out_ev, fn, seq_args, kwds);
      Py_DECREF(seq_args);
      return r;
    }
  }

  /* Non-flattening: push native frame with fn, seq_args, kwds on stack */
  if (is_tail) {
    kos_dynframe_reset_for_tail(CURRENT_FRAME(ctx));
  } else {
    KosDynFrame *frame = kos_dyn_push_frame_empty(ctx);
    if (! frame) {
      Py_DECREF(seq_args);
      return -1;
    }
  }

  KosDynFrame *frame = CURRENT_FRAME(ctx);
  typedef int (*NativeHandlerFn)(void *, void *, int, PyObject *);
  KosDynFrame_NATIVE_HANDLER(frame) = (NativeHandlerFn) apply_native_handler;

  if (KosStackBlock_PushRef(&CURRENT_STACK(ctx), fn) < 0
      || KosStackBlock_PushRef(&CURRENT_STACK(ctx), seq_args) < 0) {
    Py_DECREF(seq_args);
    return -1;
  }
  PyObject *kwds_push = kwds ? kwds : Py_None;
  Py_INCREF(kwds_push);
  if (KosStackBlock_PushRef(&CURRENT_STACK(ctx), kwds_push) < 0) {
    Py_DECREF(kwds_push);
    return -1;
  }
  return 0;
}


int kos_install_apply(KosContext *ctx, KosEvent *out_ev,
                      PyObject *callable, PyObject *args_tuple, PyObject *kwds_dict) {
  (void) kwds_dict;
  return kos_install_apply_impl(ctx, out_ev, callable, args_tuple, 0);
}


int kos_tail_install_apply(KosContext *ctx, KosEvent *out_ev,
                           PyObject *callable, PyObject *args_tuple, PyObject *kwds_dict) {
  (void) kwds_dict;
  return kos_install_apply_impl(ctx, out_ev, callable, args_tuple, 1);
}


static int defer_drive(KosContext *ctx, KosEvent *out_ev, PyObject *defer_obj);


static int kos_await_dispatch(KosContext *ctx, KosEvent *out_ev, PyObject *value) {
  if (KosDefer_Check(value))
    return defer_drive(ctx, out_ev, value);
  return push_or_await(ctx, out_ev, value);
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
static int obj_op_reducing(
    KosContext *ctx,
    KosEvent *out_ev,
    KosOp *insn,
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
    KosDynNode *region) {

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


/**
   Drive a defer to completion or suspension. Used only by AWAIT opcode.
   Consumes defer (caller's ref). Returns 0 continue, 1 stop (AWAIT), -1 error.
   When DELEGATING(inner), calls push_or_await(ctx, out_ev, inner); on suspend
   stores defer in ctx->pending_defer for FeedResult on resume.
*/
static int defer_drive(KosContext *ctx, KosEvent *out_ev, PyObject *defer_obj) {
  KosDefer *defer = (KosDefer *) defer_obj;
  for (;;) {
    int suspended = 0;
    PyObject *yielded = NULL;
    PyObject *result = KosDefer_Start(defer, &suspended, &yielded);

    if (result) {
      if (KosStackBlock_PushRef(&CURRENT_STACK(ctx), result) < 0) {
        Py_DECREF(result);
        Py_DECREF(defer_obj);
        return -1;
      }
      Py_DECREF(result);
      Py_DECREF(defer_obj);
      return 0;
    }

    if (PyErr_Occurred()) {
      Py_DECREF(defer_obj);
      return -1;
    }

    if (suspended && yielded) {
      /* If inner is itself a defer, drive it; then feed outer with result. */
      if (KosDefer_Check(yielded)) {
        int r = defer_drive(ctx, out_ev, yielded);
        if (r == 0) {
          PyObject *inner_result = KosStackBlock_PopRef(&CURRENT_STACK(ctx));
          if (! inner_result) {
            Py_DECREF(defer_obj);
            return -1;
          }
          KosDefer_FeedResult(defer, inner_result);
          if (KosStackBlock_PushRef(&CURRENT_STACK(ctx), inner_result) < 0) {
            Py_DECREF(inner_result);
            Py_DECREF(defer_obj);
            return -1;
          }
          Py_DECREF(inner_result);
          Py_DECREF(defer_obj);
          return 0;
        }
        if (r == 1) {
          Py_XDECREF(KosContext_PENDING_DEFER(ctx));
          KosContext_PENDING_DEFER(ctx) = defer_obj;
          return 1;
        }
        Py_DECREF(defer_obj);
        return -1;
      }
      int r = push_or_await(ctx, out_ev, yielded);
      Py_DECREF(yielded);
      if (r == 0) {
        PyObject *inner_result = KosStackBlock_PopRef(&CURRENT_STACK(ctx));
        if (! inner_result) {
          Py_DECREF(defer_obj);
          return -1;
        }
        KosDefer_FeedResult(defer, inner_result);
        if (KosStackBlock_PushRef(&CURRENT_STACK(ctx), inner_result) < 0) {
          Py_DECREF(inner_result);
          Py_DECREF(defer_obj);
          return -1;
        }
        Py_DECREF(inner_result);
        Py_DECREF(defer_obj);
        return 0;
      }
      if (r == 1) {
        Py_XDECREF(KosContext_PENDING_DEFER(ctx));
        KosContext_PENDING_DEFER(ctx) = defer_obj;
        return 1;
      }
      Py_DECREF(defer_obj);
      return -1;
    }

    Py_DECREF(defer_obj);
    PyErr_SetString(PyExc_RuntimeError, "defer Start returned without value or suspend");
    return -1;
  }
}


/*  Includes the implementation of all opcodes from the opcode/ directory.
*/

#define KOS_OP_HANDLER(name)                                            \
  KOS_OP_INLINE int op_##name(KosContext *ctx, KosEvent *out_ev,        \
                              KosOp *insn)
#define KOS_DROP_HANDLER(name)                                          \
  KOS_OP_INLINE int op_DROP_##name(KosContext *ctx, KosEvent *out_ev,   \
                                   KosOp *insn)
#define KOS_DROP 0
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
  KosDynNode *region = kos_find_node_for_raise(ctx);
  SAYF("[try_local] region=%p dyn_top=%p acting_pc=%u stack_len=%zu",
       (void *) region, (void *) KosContext_DYN_TOP(ctx),
       (unsigned) CURRENT_ACTING_PC(ctx),
       (size_t) (CURRENT_STACK(ctx) ? KosStackBlock_LEN(CURRENT_STACK(ctx)) : 0));
  if (! region) {
    if (CURRENT_FRAME(ctx)) {
      while (KosContext_DYN_TOP(ctx) != NULL
             && KosContext_DYN_TOP(ctx) != (KosDynNode *) CURRENT_FRAME(ctx)) {
        KosDynNode *top = KosContext_DYN_TOP(ctx);
        int top_may_swallow = (top && KosDynNode_HAS_CAP(top, KOS_DYN_CAP_CATCH));
        uint32_t catch_resume_pc = (uint32_t) -1;
        if (top && top_may_swallow) {
          if (KosDynNode_IS_CC(top))
            catch_resume_pc = KosDynCC_RESUME_PC((KosDynCC *) top);
          else if (KosDynNode_IS_WITH_OR_DYN_WITH(top))
            catch_resume_pc = KosDynWith_RESUME_PC((KosDynWith *) top);
        }
        /* Some nodes' pop can call PyErr_Clear(); preserve exception across pop.
           Barrier nodes return an error from kos_dyn_pop_top, so also preserve
           across that call to keep the original exception available for
           unwind_on_exception dispatch. */
        PyObject *saved_typ = NULL, *saved_val = NULL, *saved_tb = NULL;
        int needs_preserve = (top && KosDynNode_HAS_CAP(top, KOS_DYN_CAP_PRESERVE_ERR_ACROSS_POP))
            || (top && KosDynNode_HAS_CAP(top, KOS_DYN_CAP_BARRIER));
        if (needs_preserve && PyErr_Occurred())
          PyErr_Fetch(&saved_typ, &saved_val, &saved_tb);
        int pop_res = kos_dyn_pop_top(ctx, out_ev);
        if (saved_typ != NULL)
          PyErr_Restore(saved_typ, saved_val, saved_tb);
        if (pop_res == KOS_DYN_UNWIND_ASYNC_STARTED) {
          KosContext_UNWIND_TARGET_FRAME(ctx) = CURRENT_FRAME(ctx);
          return 1;
        }
        if (pop_res != 0) {
          /* Node refused to pop (e.g. a BARRIER node). Dispatch to the
             node's exception handler if it has the HANDLES_EXCEPTIONS cap. */
          if (top && KosDynNode_HAS_CAP(top, KOS_DYN_CAP_HANDLES_EXCEPTIONS)) {
            int exc_res = kos_dyn_exception_unwind(ctx, top, out_ev);
            if (exc_res == KOS_DYN_UNWIND_ASYNC_STARTED) {
              KosContext_UNWIND_TARGET_FRAME(ctx) = CURRENT_FRAME(ctx);
              return 1;
            }
            if (exc_res == 1) {
              return 1;
            }
            if (exc_res == 0) {
              continue;
            }
          }
          {
            PyObject *typ = NULL, *val = NULL, *tb = NULL;
            PyErr_Fetch(&typ, &val, &tb);
            inject_eval_frame_traceback(ctx, &typ, &val, &tb);
            PyErr_Clear();
            KosEvent_KIND(out_ev) = KOS_EV_RAISE;
            KosEvent_EXC_TYPE(out_ev) = typ;
            KosEvent_PAYLOAD(out_ev) = val;
            Py_XDECREF(tb);
          }
          KosContext_FLAGS(ctx) &= ~KOS_CTX_RUNNING;
          KosContext_FLAGS(ctx) |= KOS_CTX_DEAD;
          return 0;
        }
        /* Only continue when a node with may-swallow swallowed; other nodes
           may clear PyErr in their teardown (e.g. delegate close). */
        if (top_may_swallow && ! PyErr_Occurred()) {
          if (catch_resume_pc != (uint32_t) -1 && CURRENT_FRAME(ctx))
            CURRENT_PC(ctx) = catch_resume_pc;
          return 1;
        }
      }
    }
    {
      PyObject *typ = NULL, *val = NULL, *tb = NULL;
      PyErr_Fetch(&typ, &val, &tb);
      inject_eval_frame_traceback(ctx, &typ, &val, &tb);
      PyErr_Clear();
      KosEvent_KIND(out_ev) = KOS_EV_RAISE;
      KosEvent_EXC_TYPE(out_ev) = typ;
      KosEvent_PAYLOAD(out_ev) = val;
      Py_XDECREF(tb);
      KosContext_FLAGS(ctx) &= ~KOS_CTX_RUNNING;
      KosContext_FLAGS(ctx) |= KOS_CTX_DEAD;
      return 0;
    }
  }

  uint32_t handler_pc = KosDynNode_IS_TRY(region) ? KosDynTry_HANDLER_PC((KosDynTry *) region) : KosDynFinally_HANDLER_PC((KosDynFinally *) region);
  {
    KosTryHandler th;
    if (kos_try_handler_for((PyObject *) CURRENT_CODE(ctx), CURRENT_ACTING_PC(ctx), &th) == 0) {
      if (KosDynNode_IS_FINALLY(region) && th.finally_pc != KOS_EXTENT_NONE) {
        handler_pc = th.finally_pc;
      }
      else if (th.except_pc != KOS_EXTENT_NONE) {
        handler_pc = th.except_pc;
      }
    }
  }

  if (KosDynNode_IS_TRY(region) || KosDynNode_IS_FINALLY(region)) {
    if (KosStackBlock_ClearBlock(&CURRENT_STACK(ctx)) < 0)
      return -1;
  }

  CURRENT_PC(ctx) = handler_pc;
  if (KosDynNode_IS_TRY(region)) KosDynTry_ENABLED((KosDynTry *) region) = 0; else KosDynFinally_ENABLED((KosDynFinally *) region) = 0;

  /* Set aside exception in excinfo and clear so handler runs without PyErr set */
  {
    PyObject *typ = NULL, *val = NULL, *tb = NULL;
    PyErr_Fetch(&typ, &val, &tb);
    PyErr_SetExcInfo(typ, val, tb);
    PyErr_Clear();
  }

  if (KosDynNode_IS_FINALLY(region))
    KosDynFinally_HANDLING_RAISE((KosDynFinally *) region) = 1;

  return 1;
}


/**
   Phase 1: entry and revive normalization.

   Determines the initial KosEvalMode by dispatching through the current
   suspension state and send_val/throw_exc arguments.  Handles the
   EXCEPTION and RAISE outcomes in-place (filling out_ev and flagging the
   context dead).  Clears KosContext_SUSPENSION_EVENT after mode selection.

   :param mode_out: Receives the resolved KosEvalMode on KOS_STEP_CONTINUE.
   :return: KOS_STEP_CONTINUE when the loop should proceed;
            KOS_STEP_STOP_EVENT when out_ev is filled and eval should return.
*/
static KosEvalStep eval_phase_entry(
        KosContext *ctx,
        KosEvent *out_ev,
        PyObject *send_val,
        PyObject *throw_typ,
        PyObject *throw_exc,
        KosEvalMode *mode_out) {

  KosEvalMode mode = KOS_MODE_NORMAL;
  if (KosContext_PLAN(ctx)) {
    mode = revive_plan(ctx, send_val, throw_typ, throw_exc);
    if (mode != KOS_MODE_NORMAL && mode != KOS_MODE_TRANSFER)
      goto entry_mode_dispatch;
  } else if (send_val == NULL && throw_typ == NULL && throw_exc == NULL) {
    switch (KosContext_SUSPENSION_EVENT(ctx)) {
      case KOS_SUSP_NONE:
        mode = KOS_MODE_NORMAL;
        break;
      case KOS_SUSP_AWAIT:
        mode = raise_await(ctx);
        break;
      case KOS_SUSP_YIELD:
        mode = raise_yield(ctx);
        break;
      case KOS_SUSP_YIELD_FROM:
        mode = raise_yield_from(ctx);
        break;
      default:
        mode = KOS_MODE_NORMAL;
        break;
    }
  } else {
    switch (KosContext_SUSPENSION_EVENT(ctx)) {
      case KOS_SUSP_AWAIT:
        mode = revive_await(ctx, send_val, throw_typ, throw_exc);
        break;
      case KOS_SUSP_YIELD:
        mode = revive_yield(ctx, send_val, throw_typ, throw_exc);
        break;
      case KOS_SUSP_YIELD_FROM:
        mode = revive_yield_from(ctx, send_val, throw_typ, throw_exc);
        break;
      default:
        mode = revive_await(ctx, send_val, throw_typ, throw_exc);
        break;
    }
  }
  KosContext_SUSPENSION_EVENT(ctx) = KOS_SUSP_NONE;

entry_mode_dispatch:
  if (mode == KOS_MODE_EXCEPTION) {
    clear_pending_agen_on_error(ctx);
    if (try_local_exception_handler(ctx, out_ev)) {
      if (KosEvent_KIND(out_ev) == KOS_EV_AWAIT) {
        KosContext_SUSPENSION_EVENT(ctx) = KOS_SUSP_AWAIT;
        return KOS_STEP_STOP_EVENT;
      }
      *mode_out = KOS_MODE_NORMAL;
      return KOS_STEP_CONTINUE;
    }
    /* try_local_exception_handler returning 0 has already filled out_ev with
       KOS_EV_RAISE and the exception payload, and cleared PyErr.  Just mark
       the context dead and stop; do not call PyErr_Fetch again. */
    KosContext_FLAGS(ctx) &= ~KOS_CTX_RUNNING;
    KosContext_FLAGS(ctx) |= KOS_CTX_DEAD;
    return KOS_STEP_STOP_EVENT;
  }
  if (mode == KOS_MODE_RAISE) {
    KosEvent_KIND(out_ev) = KOS_EV_RAISE;
    KosEvent_PAYLOAD(out_ev) = NULL;
    KosEvent_EXC_TYPE(out_ev) = NULL;
    PyObject *raise_typ = NULL;
    PyObject *raise_tb  = NULL;
    PyErr_Fetch(&raise_typ, &KosEvent_PAYLOAD(out_ev), &raise_tb);
    inject_eval_frame_traceback(ctx, &raise_typ, &KosEvent_PAYLOAD(out_ev), &raise_tb);
    KosEvent_EXC_TYPE(out_ev) = raise_typ;
    Py_XDECREF(raise_tb);
    KosContext_FLAGS(ctx) &= ~KOS_CTX_RUNNING;
    KosContext_FLAGS(ctx) |= KOS_CTX_DEAD;
    return KOS_STEP_STOP_EVENT;
  }

  /* Resume delivery: if we revived with a value and top frame has native_handler, deliver to it */
  if (send_val != NULL && mode == KOS_MODE_NORMAL && CURRENT_FRAME(ctx)
      && KosDynFrame_NATIVE_HANDLER(CURRENT_FRAME(ctx)) != NULL) {
    PyObject *val = KosStackBlock_PopRef(&CURRENT_STACK(ctx));
    if (val) {
      typedef int (*NativeHandlerFn)(void *, void *, int, PyObject *);
      NativeHandlerFn fn =
          (NativeHandlerFn) KosDynFrame_NATIVE_HANDLER(CURRENT_FRAME(ctx));
      int res = fn(ctx, out_ev, 1, val);
      if (res < 0)
        return KOS_STEP_FATAL_ERROR;
      if (res == 1)
        return KOS_STEP_STOP_EVENT;
    }
  }

  *mode_out = mode;
  return KOS_STEP_CONTINUE;
}


/**
   Phase 2: transfer-plan synchronization.

   Drains any active transfer plan to completion.  When no frame and no
   hook remain after draining, emits a terminal KOS_EV_RETURN(None) and
   marks the context dead.

   :return: KOS_STEP_CONTINUE when the dispatch loop should start;
            KOS_STEP_STOP_EVENT when out_ev is filled and eval should return.
*/
static KosEvalStep eval_phase_transfer(KosContext *ctx, KosEvent *out_ev) {

  if (run_transfer_plan_to_completion(ctx, out_ev) < 0)
    return KOS_STEP_STOP_EVENT;

  if (! CURRENT_FRAME(ctx)) {
    KosEvent_KIND(out_ev) = KOS_EV_RETURN;
    KosEvent_PAYLOAD(out_ev) = Py_None;
    Py_INCREF(Py_None);
    KosContext_FLAGS(ctx) &= ~KOS_CTX_RUNNING;
    KosContext_FLAGS(ctx) |= KOS_CTX_DEAD;
    return KOS_STEP_STOP_EVENT;
  }

  return KOS_STEP_CONTINUE;
}


/**
   Phase 3: async exception check at each loop iteration top.

   When PyErr_Occurred() is set on entry to the dispatch loop (e.g. from a
   signal handler or a mid-loop async exception), calls process_raise to
   attempt local handling.  Sets *loop_continue when the caller should
   issue a bare continue rather than proceeding to dispatch.

   :param loop_continue: Set to 1 when the loop should continue without dispatch.
   :return: KOS_STEP_STOP_EVENT when out_ev is filled and eval should return;
            KOS_STEP_CONTINUE otherwise (including when *loop_continue is set).
*/
static KosEvalStep eval_phase_pending_exception(
        KosContext *ctx,
        KosEvent *out_ev,
        int *loop_continue) {

  *loop_continue = 0;
  if (! PyErr_Occurred())
    return KOS_STEP_CONTINUE;

  KosEvalMode raise_mode = process_raise(ctx, out_ev);
  if (raise_mode == KOS_MODE_RAISE)
    return KOS_STEP_STOP_EVENT;

  if (KosEvent_KIND(out_ev) == KOS_EV_AWAIT) {
    KosContext_SUSPENSION_EVENT(ctx) = KOS_SUSP_AWAIT;
    return KOS_STEP_STOP_EVENT;
  }

  *loop_continue = 1;
  return KOS_STEP_CONTINUE;
}


/**
   Phase 4: dispatch prelude — fetch, validate, and pre-advance.

   Fetches code and PC from the active hook runner or current frame, performs
   bounds and opcode-range checks, records acting_pc, pre-advances pc, and
   loads the instruction pointer into dc.

   :param dc: Filled with code, acting pc, and insn on success.
   :return: KOS_STEP_CONTINUE on success;
            KOS_STEP_FATAL_ERROR when a hard internal error is set via PyErr.
*/
static KosEvalStep eval_phase_prepare_dispatch(
        KosContext *ctx,
        KosEvent *out_ev,
        KosDispatchCtx *dc) {

  (void) out_ev;

  if (! CURRENT_FRAME(ctx)) {
    PyErr_SetString(PyExc_RuntimeError, "eval loop: no active frame");
    return KOS_STEP_FATAL_ERROR;
  }

  KosCode *code = CURRENT_CODE(ctx);
  uint32_t pc = CURRENT_PC(ctx);

  if (unlikely(pc >= KosCode_CODE_LEN(code))) {
    SAYF("[eval] pc=%u OUT OF RANGE (code_len=%u)\n",
         (unsigned) pc, (unsigned) KosCode_CODE_LEN(code));
    PyErr_SetString(PyExc_RuntimeError, "pc out of range");
    return KOS_STEP_FATAL_ERROR;
  }

  KosOp *insn = &KosCode_CODE(code)[pc];
  SAYF("[eval] pc=%u op=%s arg=%u\n",
       (unsigned) pc, kos_opcode_name(KosOp_OP(insn)), (unsigned) KosOp_ARG(insn));

  /* Record acting_pc (instruction we are executing) before advance. */
  KosDynFrame_ACTING_PC(CURRENT_FRAME(ctx)) = pc;

  /* Advance PC before dispatch so normal path advances. If handler returns 1
     (suspend, e.g. WITH enter/exit await) we roll back so next eval is same insn. */
  CURRENT_PC(ctx) = pc + 1;

  if (unlikely((KosOpCode) KosOp_OP(insn) > KOS_OP_THEEND)) {
    PyErr_Format(PyExc_RuntimeError, "invalid opcode %u",
                 (unsigned) KosOp_OP(insn));
    return KOS_STEP_FATAL_ERROR;
  }

  dc->code    = code;
  dc->pc      = pc;
  dc->insn    = insn;
  return KOS_STEP_CONTINUE;
}


/**
   Phase 6: post-opcode result handling.

   Called when an opcode handler returns a non-zero op_res.  Handles the
   exception path (via try_local_exception_handler), the WITH-family PC
   rollback for async enter/exit suspend, and suspension-event tagging for
   AWAIT and YIELD events.  Sets *loop_continue when an exception was locally
   handled and the loop should continue without stopping.

   :param op_res: The non-zero return value from the opcode handler.
   :param loop_continue: Set to 1 when exception was handled and loop should continue.
   :return: KOS_STEP_FATAL_ERROR when op_res < 0 and no exception/event (caller uses eval_ret -1);
            KOS_STEP_STOP_EVENT when out_ev is filled and eval should return (eval_ret 0);
            KOS_STEP_CONTINUE when *loop_continue is set.
*/
static KosEvalStep eval_phase_post_opcode(
        KosContext *ctx,
        KosEvent *out_ev,
        KosDispatchCtx *dc,
        int op_res,
        int *loop_continue) {

  *loop_continue = 0;

  if (op_res == KOS_OP_RES_CONTROL_RETURNED) {
    int r = kos_dynnode_on_control_returned(ctx, KosContext_DYN_TOP(ctx), out_ev);
    if (r == 0) {
      *loop_continue = 1;
      return KOS_STEP_CONTINUE;
    }
  }

  if (PyErr_Occurred()) {
    KosEvalMode raise_mode = process_raise(ctx, out_ev);
    if (raise_mode == KOS_MODE_RAISE)
      return KOS_STEP_STOP_EVENT;
    if (KosEvent_KIND(out_ev) == KOS_EV_AWAIT) {
      KosContext_SUSPENSION_EVENT(ctx) = KOS_SUSP_AWAIT;
      return KOS_STEP_STOP_EVENT;
    }
    *loop_continue = 1;
    return KOS_STEP_CONTINUE;
  }

  if (op_res == 1) {
    /* WITH enter/exit await: do not advance PC so resume dispatches
       the same opcode. Other opcodes (e.g. YIELD) use 1 for suspend
       but resume at the next instruction. */
    KosOpCode op = (KosOpCode) KosOp_OP(dc->insn);
    if (op == KOS_OP_PUSH_WITH || op == KOS_OP_POP_WITH
        || op == KOS_OP_PUSH_DYN_WITH || op == KOS_OP_POP_DYN_WITH) {
      CURRENT_PC(ctx) = dc->pc;
      KosDynFrame_ACTING_PC(CURRENT_FRAME(ctx)) = dc->pc;
    }
  }

  /* Record why we suspended so next entry can choose revive path */
  if (KosEvent_KIND(out_ev) == KOS_EV_AWAIT) {
    KosContext_SUSPENSION_EVENT(ctx) = KOS_SUSP_AWAIT;
  } else if (KosEvent_KIND(out_ev) == KOS_EV_YIELD) {
    KosContext_SUSPENSION_EVENT(ctx) = KOS_SUSP_YIELD;
  }

  /* op_res < 0 with no PyErr and no suspension event is a fatal internal error */
  return (op_res < 0) ? KOS_STEP_FATAL_ERROR : KOS_STEP_STOP_EVENT;
}


int koskari_eval(KosContext *ctx, KosEvent *out_ev, PyObject *send_val, PyObject *throw_typ, PyObject *throw_exc) {
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

  /* Phase 1: entry/revive normalization */
  KosEvalMode mode = KOS_MODE_NORMAL;
  if (eval_phase_entry(ctx, out_ev, send_val, throw_typ, throw_exc, &mode) != KOS_STEP_CONTINUE)
    goto koskari_eval_cleanup;

  /* Phase 2: transfer-plan synchronization (only when mode warrants it) */
  if (mode == KOS_MODE_NORMAL || mode == KOS_MODE_TRANSFER) {
    if (unlikely(KosContext_PLAN(ctx)))
      if (eval_phase_transfer(ctx, out_ev) != KOS_STEP_CONTINUE)
        goto koskari_eval_cleanup;
  }

/* The optable and all TARGET_* labels must remain in koskari_eval because
   GCC/Clang computed-goto requires label addresses (&&TARGET_name) and the
   indirect goto (goto *optable[op]) to be in the same function body. */
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
    if (unlikely(KosContext_PY_RESUME_KIND(ctx) != KOS_RESUME_NONE))
      KosContext_PY_RESUME_KIND(ctx) = KOS_RESUME_NONE;
#if KOS_STACK_TRACE
    trace_stack("  after ", CURRENT_STACK(ctx));
#endif
    if (KosContext_PLAN(ctx) && run_transfer_plan_to_completion(ctx, out_ev) < 0) {
      eval_ret = 0;
      goto koskari_eval_cleanup;
    }

    /* Phase 3: async pending-exception check */
    {
      int loop_continue = 0;
      if (eval_phase_pending_exception(ctx, out_ev, &loop_continue) == KOS_STEP_STOP_EVENT) {
        eval_ret = 0;
        goto koskari_eval_cleanup;
      }
      if (loop_continue)
        continue;
    }

    /* Native frame: run native handler as frame evaluator (skip bytecode dispatch) */
    if (CURRENT_FRAME(ctx)
        && KosDynFrame_NATIVE_HANDLER(CURRENT_FRAME(ctx)) != NULL) {
      typedef int (*NativeHandlerFn)(void *, void *, int, PyObject *);
      NativeHandlerFn fn = (NativeHandlerFn) KosDynFrame_NATIVE_HANDLER(CURRENT_FRAME(ctx));
      int op_res = fn(ctx, out_ev, 0, NULL);
      if (op_res != 0) {
        KosDispatchCtx dc_native = { 0 };
        dc_native.code = NULL;
        dc_native.pc = 0;
        dc_native.insn = NULL;
        int loop_continue = 0;
        KosEvalStep ps = eval_phase_post_opcode(ctx, out_ev, &dc_native, op_res, &loop_continue);
        if (ps == KOS_STEP_FATAL_ERROR) {
          eval_ret = -1;
          goto koskari_eval_cleanup;
        }
        if (ps == KOS_STEP_STOP_EVENT) {
          eval_ret = 0;
          goto koskari_eval_cleanup;
        }
        if (loop_continue)
          continue;
      }
      goto next_insn;
    }

    /* Phase 4: dispatch prelude */
#if KOS_STACK_TRACE
    trace_stack("  before", CURRENT_STACK(ctx));
#endif
    KosDispatchCtx dc;
    {
      KosEvalStep pds = eval_phase_prepare_dispatch(ctx, out_ev, &dc);
      if (pds == KOS_STEP_FATAL_ERROR) {
        eval_ret = -1;
        goto koskari_eval_cleanup;
      }
    }

    /* Phase 5: opcode dispatch.
       Computed-goto dispatch stays here; see comment above optable[]. */
    {
      int op_res;

#if KOS_USE_COMPUTED_GOTO
      goto *optable[KosOp_OP(dc.insn)];
#define OP(name, style) TARGET_##name: op_res = op_##name(ctx, out_ev, dc.insn); if (likely(! op_res)) goto next_insn; goto handle_op_result;
#include "opcodes.incl"
#undef OP
      handle_op_result:
#else
      switch (KosOp_OP(dc.insn)) {
#define OP(name, style) case KOS_OP_##name: op_res = op_##name(ctx, out_ev, dc.insn); break;
#include "opcodes.incl"
#undef OP
      default:
        PyErr_Format(PyExc_RuntimeError, "invalid opcode %u",
                     (unsigned) KosOp_OP(dc.insn));
        eval_ret = -1;
        goto koskari_eval_cleanup;
      }
#endif

      /* Phase 6: post-opcode result handling */
      if (unlikely(op_res != 0)) {
        int loop_continue = 0;
        KosEvalStep ps = eval_phase_post_opcode(ctx, out_ev, &dc, op_res, &loop_continue);
        if (ps == KOS_STEP_FATAL_ERROR) {
          eval_ret = -1;
          goto koskari_eval_cleanup;
        }
        if (ps == KOS_STEP_STOP_EVENT) {
          eval_ret = 0;
          goto koskari_eval_cleanup;
        }
        if (loop_continue)
          continue;
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
   base: parent exception type (e.g. PyExc_BaseException or PyExc_RuntimeError)
*/
static int register_koskari_exception(
    PyObject *mod,
    const char *full_name,
    const char *attr_name,
    PyObject *base,
    PyObject **out_exc) {

  PyObject *bases = PyTuple_Pack(1, base);
  if (! bases)
    return -1;
  *out_exc = PyErr_NewException(full_name, bases, NULL);
  Py_DECREF(bases);
  if (! *out_exc)
    return -1;
  return PyModule_AddObject(mod, attr_name, *out_exc);
}


int koskari_eval_init(PyObject *mod) {

  PyObject *dict = PyModule_GetDict(mod);
  if (! dict)
    return -1;

  /* Initialize the import fromlist sentinel used by IMPORT / IMPORT_REL. */
  if (! _import_fromlist) {
    PyObject *star = PyUnicode_FromString("*");
    if (! star)
      return -1;
    _import_fromlist = PyTuple_Pack(1, star);
    Py_DECREF(star);
    if (! _import_fromlist)
      return -1;
  }

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
  STR_CONST(str_class, "__class__");

  /* Initialize KosEvaluatorToken type */
  if (PyType_Ready(&KosEvaluatorTokenType) < 0)
    return -1;
  Py_INCREF(&KosEvaluatorTokenType);
  if (PyModule_AddObject(mod, "EvaluatorToken",
                         (PyObject *) &KosEvaluatorTokenType) < 0) {
    Py_DECREF(&KosEvaluatorTokenType);
    return -1;
  }

  /* Initialize exception types */
  if (register_koskari_exception(mod,
                                 "koskari.lib.KoskariContinuationDead",
                                 "KoskariContinuationDead",
                                 (PyObject *) PyExc_RuntimeError,
                                 &KoskariContinuationDead) < 0)
    return -1;
  if (register_koskari_exception(mod,
                                 "koskari.lib.KoskariContinuationApply",
                                 "KoskariContinuationApply",
                                 (PyObject *) PyExc_BaseException,
                                 &KoskariContinuationApply) < 0)
    return -1;
  if (register_koskari_exception(mod,
                                 "koskari.lib.KoskariContinuationError",
                                 "KoskariContinuationError",
                                 (PyObject *) PyExc_BaseException,
                                 &KoskariContinuationError) < 0)
    return -1;

  /* Initialize dynchain module */
  if (koskari_dynchain_init(mod))
    return -1;

  return 0;
}


int koskari_eval_clear(PyObject *mod) {

  /* Clear import sentinel and string constants initialized in koskari_eval_init. */
  Py_CLEAR(_import_fromlist);
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
  Py_CLEAR(str_class);

  /* Null out exception type pointers (module dict owns the refs) */
  KoskariContinuationDead = NULL;
  KoskariContinuationApply = NULL;
  KoskariContinuationError = NULL;

  while (context_freelist) {
    KosContext *next = KosContext_FREELIST_NEXT(context_freelist);
    PyMem_Free(context_freelist);
    context_freelist = next;
  }
  context_freecount = 0;

  koskari_dynchain_clear(mod);

  return 0;
}


// The end.
