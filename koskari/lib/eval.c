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
      (no boundary event); only the top-level frame produces KOS_RES_*
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
      KOS_RES_RAISE, propagating the exception upward through the Python
      async call chain to the owning eval context.

   :author: Christopher O'Brien <obriencj@preoccupied.net>
   :license: GNU General Public License v3
*/


#include "lib.h"
#include "hints.h"
#include "debug.h"
#include "operator.h"
#include "tracing.h"

#include <stdint.h>
#include <frameobject.h>
#include <time.h>


/* eval.c-local static inline helpers (not KosEvalState bodies; see eval/eval_state.c). */

typedef struct {
  PyObject *left;
  PyObject *right;
} KosEvalStackLR;


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
static PyObject *str_class = NULL;
static PyObject *str_getattr = NULL;
static PyObject *str_name = NULL;
static PyObject *str_package = NULL;
static PyObject *str_asyncio_future_blocking = NULL;

/* KoskariContinuationApply payload attributes (see cont.c raise sites) */
static PyObject *str_args = NULL;
static PyObject *str_continuation = NULL;
static PyObject *str_target = NULL;


/*
  Sentinel fromlist for the import opcodes (see kos_import_module).
  Passing ("*",) as fromlist to PyImport_ImportModuleLevelObject causes it
  to return the deepest (leaf) submodule rather than the topmost package,
  matching importlib.import_module semantics.  Initialized in
  koskari_eval_init.
*/
static PyObject *_import_fromlist = NULL;


/* === Static forward declarations === */

static void inject_eval_frame_traceback(KosContext *ctx,
    PyObject **exc_type, PyObject **exc_val, PyObject **exc_tb);

static void kos_eval_mark_context_dead(KosContext *ctx);

static KosOpResult process_raise(KosEvalState *es);

static KosOpResult eval_parent_return_deliver(KosEvalState *es, PyObject *val);

static KosOpResult eval_event_yield(KosEvalState *es, PyObject *val);

static KosOpResult eval_event_await(KosEvalState *es, PyObject *val);

static KosOpResult eval_event_return(KosEvalState *es, PyObject *val);

static KosOpResult eval_event_return_at_frame(KosEvalState *es, PyObject *val);

static KosOpResult eval_event_return_ng(KosEvalState *es, PyObject *val);

static KosOpResult kos_eval_post_opcode_control_returned_after_hook(
        KosEvalState *es);



#include "eval/eval_chain.c"
#include "eval/eval_state.c"
#include "eval/eval_utils.c"
#include "eval/eval_obj.c"
#include "eval/eval_gil.c"
#include "eval/eval_cache.c"


/**
   On exception: if we are inside a Koskari async gen step (NEXT or ITER_NEXT),
   detach the gen frame, close the gen, and clear NEXT / FOREACH boundary state.
*/
static void clear_kos_agen_boundary_on_error(KosEvalState *es) {

  KosDynFrame *cf;
  KosDynNode *top;

  cf = KosEvalState_FRAME(es);
  top = KosEvalState_DYN_TOP(es);
  KosDynNext *dn;
  KosDynForeach *fe;
  KosDynYieldFrom *yf = NULL;
  KosDynNode *w;
  KosPyAsyncGen *agen;

  if (! cf || ! top || ! KosDynNode_IS_FRAME(top))
    return;
  {
    KosDynNode *par = KosDynNode_PARENT(top);
    if (! par || ! KosDynNode_IS_GEN(par))
      return;
  }

  dn = kos_dyn_find_next_above_frame(es);
  fe = kos_dyn_find_awaiting_foreach(es);
  if (! dn && ! fe) {
    for (w = KosDynNode_PARENT((KosDynNode *) cf); w; w = KosDynNode_PARENT(w)) {
      if (KosDynNode_IS_YIELD_FROM(w)
          && KosDynNode_HAS_CAP(w, KOS_DYN_CAP_RECEIVES_ITER_BOUNDARY)
          && KosDynYieldFrom_KIND((KosDynYieldFrom *) w) == (uint8_t) 4) {
        yf = (KosDynYieldFrom *) w;
        break;
      }
    }
  }

  if (! dn && ! fe && ! yf)
    return;

  if (dn)
    agen = (KosPyAsyncGen *) KosDynNext_AGEN_OBJ((KosDynNode *) dn);
  else if (fe)
    agen = (KosPyAsyncGen *) KosDynForeach_ITER(fe);
  else
    agen = (KosPyAsyncGen *) KosDynYieldFrom_DELEGATE(yf);

  {
    KosDynFrame *detached_frame = NULL;
    if (kos_dyn_detach_top_frame(es, &detached_frame) == 0 && detached_frame) {
      agen->closed = 1;
      agen->in_progress = 0;
      kos_dynframe_free(detached_frame);
    }
  }

  if (fe)
    KosDynForeach_ITER_STEP_PHASE(fe) = KOS_FOREACH_ITER_STEP_IDLE;
  if (dn)
    kos_dyn_unlink_next_node(es, dn);
  if (yf)
    (void) kos_dyn_unlink_yield_from(es, yf);
}


/**
   Revive the active transfer plan with send_val or throw_exc. Consumes
   send_val/throw_exc for the plan. May set exception in context. :return:
   KOS_MODE_TRANSFER to continue draining plan, KOS_MODE_EXCEPTION if
   throw_exc set, KOS_MODE_NORMAL if plan done
*/
static KosEvalMode revive_plan(
    KosEvalState *es,
    PyObject *send_val,
    PyObject *throw_typ,
    PyObject *throw_exc) {

  KosTransferPlan *plan = KosEvalState_PLAN(es);

  if (throw_exc != NULL) {
    PyErr_SetObject(throw_typ, throw_exc);
    return KOS_MODE_EXCEPTION;
  }

  if (send_val != NULL && plan) {
    Py_XINCREF(send_val);
    Py_XDECREF(KosTransferPlan_STEP_RESUME_INJECT(plan));
    KosTransferPlan_STEP_RESUME_INJECT(plan) = send_val;
  }

  return KOS_MODE_TRANSFER;
}


/**
   Revive from AWAIT: push await result or set throw_exc. Use PY_RESUME_INJECT
   when set (driver pre-stored await result, e.g. coro.c
   throw-into-awaitable), else send_val. Complete pending WITH exit if
   applicable.

   When throw_exc is set and py_await_iter is non-NULL, resolves a callable
   throw and forwards with single-arg throw(exc) before the VM exception path
   (align with coroutine_throw WAITING_PY). On StopIteration, completes the
   await like a normal resume. On non-NULL throw result, returns EVAL_AWAIT
   with the next awaitable (re-suspend).

   :return: EVAL_NORMAL, EVAL_AWAIT (with re-suspend value), or EVAL_RAISE
*/
static KosOpResult revive_await(
        KosEvalState *es,
        PyObject *send_val,
        PyObject *throw_typ,
        PyObject *throw_exc) {

  if (throw_exc != NULL) {
    PyObject *typ_eff = throw_typ ? throw_typ
        : (PyObject *) Py_TYPE(throw_exc);

    SAYF("[await] revive_await throw typ=%p exc=%p\n",
         (void *) throw_typ, (void *) throw_exc);

    if (! KosEvalState_PythonAwaitIter(es)) {
      PyErr_SetObject(typ_eff, throw_exc);
      return EVAL_RAISE();
    }
    {
      PyObject *typ = typ_eff;
      PyObject *val = throw_exc;
      PyObject *tb = Py_None;
      PyObject *res;
      PyObject *throw_meth;
      Py_INCREF(typ);
      Py_INCREF(val);
      Py_INCREF(tb);
      PyErr_NormalizeException(&typ, &val, &tb);
      throw_meth = PyObject_GetAttr(KosEvalState_PythonAwaitIter(es), str_throw);
      if (! throw_meth || ! PyCallable_Check(throw_meth)) {
        if (! throw_meth && PyErr_ExceptionMatches(PyExc_AttributeError)) {
          PyErr_Clear();
          PyErr_SetObject(typ_eff, throw_exc);
        } else if (throw_meth) {
          PyErr_Format(PyExc_TypeError,
                       "%R object attribute throw is not callable",
                       KosEvalState_PythonAwaitIter(es));
        }
        Py_XDECREF(throw_meth);
        Py_DECREF(typ);
        Py_DECREF(val);
        Py_XDECREF(tb);
        KosEvalState_PythonAwaitClearIter(es);
        return EVAL_RAISE();
      }
      /* Single-arg throw(exc) avoids deprecated (typ, val, tb) on generators */
      res = PyObject_CallOneArg(throw_meth, val);
      Py_DECREF(throw_meth);
      Py_DECREF(typ);
      Py_DECREF(val);
      Py_XDECREF(tb);
      if (res) {
        PyErr_Clear();
        return EVAL_AWAIT(res);
      }
      if (PyErr_ExceptionMatches(PyExc_StopIteration)) {
        PyObject *await_result = NULL;
        if (PyGen_FetchStopIterationValue(&await_result) < 0) {
          KosEvalState_PythonAwaitClearIter(es);
          return EVAL_RAISE();
        }
        if (! await_result) {
          await_result = Py_None;
          Py_INCREF(await_result);
        }
        KosEvalState_PythonAwaitClearIter(es);
        KosEvalState_PythonResumeSet(es, await_result, KOS_RESUME_AWAIT);
        PyErr_Clear();
        /* Fall through: push inject like a normal successful resume */
      }
      else {
        KosEvalState_PythonAwaitClearIter(es);
        return EVAL_RAISE();
      }
    }
  }
  PyObject *to_push = NULL;
  uint8_t resume_kind_unused;
  KosEvalState_PythonResumeTake(es, &to_push, &resume_kind_unused);
  (void) resume_kind_unused;
  if (to_push == NULL && send_val != NULL) {
    to_push = send_val;
  }
  if (to_push != NULL && KosEvalState_FRAME(es)) {
    if (KosEvalState_StackPushRef(es, to_push) < 0)
      return EVAL_RAISE();
  }
  {
    if (kos_dyn_complete_pending_exit(es, 1) < 0)
      return EVAL_RAISE();
  }
  return EVAL_NORMAL();
}


/**
   Revive from YIELD: pop install placeholder (Py_None from kos_dyn_gen_install),
   then push send_val or set throw_exc. When send_val is NULL (e.g. __anext__),
   push Py_None per async gen protocol. Caller must have called
   kos_dyn_gen_install before koskari_eval (e.g. KosPyAsyncGen drivers, tests).
*/
static KosEvalMode revive_yield(
    KosEvalState *es,
    PyObject *send_val,
    PyObject *throw_typ,
    PyObject *throw_exc) {

  PyObject *to_push;

  SAYF("[gen] revive_yield ENTER suspend=%d send_val=%p "
       "throw_typ=%p throw_exc=%p dyn_top=%p frame_top=%p\n",
       KosEvalState_SuspensionEvent(es),
       (void *) send_val, (void *) throw_typ, (void *) throw_exc,
       (void *) KosEvalState_DYN_TOP(es),
       (void *) KosEvalState_FRAME(es));

  if (throw_exc != NULL) {
    if (KosEvalState_FRAME(es))
      (void) KosEvalState_StackPopRef(es);
    PyErr_SetObject(throw_typ, throw_exc);
    return KOS_MODE_EXCEPTION;
  }
  if (KosEvalState_FRAME(es)) {
    (void) KosEvalState_StackPopRef(es);
    to_push = (send_val != NULL) ? send_val : Py_None;
    if (KosEvalState_StackPushRef(es, to_push) < 0)
      return KOS_MODE_EXCEPTION;
  }
  SAYF("[gen] revive_yield DONE frame_top=%p dyn_top=%p\n",
       (void *) KosEvalState_FRAME(es),
       (void *) KosEvalState_DYN_TOP(es));
  return KOS_MODE_NORMAL;
}


/**
   Build the trace metadata dict for one frame. crossed marks entries
   recorded while the raise walk crossed (and popped) the frame, as
   opposed to frames still live at escape time.
*/
static PyObject *dynchain_frame_meta(KosDynFrame *frame, int crossed) {

  PyObject *meta = PyDict_New();
  if (! meta)
    return NULL;

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
  PyDict_SetItemString(meta, "crossed", crossed ? Py_True : Py_False);
  return meta;
}


static void decorate_exception_with_dynchain(
    KosContext *ctx,
    PyObject *exc_val) {

  if (! ctx || ! exc_val || ! PyExceptionInstance_Check(exc_val)) {
    return;
  }

  PyObject *frames = PyList_New(0);
  if (! frames) {
    PyErr_Clear();
    return;
  }

  /* Carry forward frames recorded while the raise walk crossed them;
     they are no longer on the live chain below. */
  {
    PyObject *prior = PyObject_GetAttrString(exc_val,
                                             "__koskari_dynchain__");
    if (! prior) {
      PyErr_Clear();
    } else {
      if (PyList_Check(prior)) {
        for (Py_ssize_t i = 0; i < PyList_GET_SIZE(prior); i++) {
          PyObject *meta = PyList_GET_ITEM(prior, i);
          PyObject *crossed = PyDict_Check(meta)
              ? PyDict_GetItemString(meta, "crossed") : NULL;
          if (crossed && PyObject_IsTrue(crossed))
            PyList_Append(frames, meta);
        }
      }
      Py_DECREF(prior);
    }
  }

  {
    KosEvalState *active_es = kos_tls_active_eval_for_ctx(ctx);
    KosDynNode *chain_top = active_es
        ? KosEvalState_DYN_TOP(active_es)
        : KosContext_DYN_TOP(ctx);

    for (KosDynNode *node = chain_top; node; node = KosDynNode_PARENT(node)) {
      if (! KosDynNode_IS_FRAME(node)) {
        continue;
      }
      PyObject *meta = dynchain_frame_meta((KosDynFrame *) node, 0);
      if (! meta) {
        PyErr_Clear();
        continue;
      }
      PyList_Append(frames, meta);
      Py_DECREF(meta);
    }
  }

  if (PyObject_SetAttrString(exc_val, "__koskari_dynchain__", frames) < 0) {
    PyErr_Clear();
  }

  Py_DECREF(frames);
}


/**
   Record the current frame into the in-flight exception's
   __koskari_dynchain__ metadata before the raise walk pops it. PyErr is
   preserved across the call; failures are swallowed (trace decoration is
   best-effort). See eval.h.
*/
void kos_eval_note_crossed_frame(KosEvalState *es) {

  PyObject *typ = NULL, *val = NULL, *tb = NULL;

  if (! es || ! KosEvalState_FRAME(es) || ! PyErr_Occurred())
    return;

  KosEvalState_FlushLiveRegsToFrame(es);
  PyErr_Fetch(&typ, &val, &tb);
  PyErr_NormalizeException(&typ, &val, &tb);

  if (val && PyExceptionInstance_Check(val)) {
    PyObject *frames = PyObject_GetAttrString(val, "__koskari_dynchain__");
    if (! frames) {
      PyErr_Clear();
      frames = PyList_New(0);
    } else if (! PyList_Check(frames)) {
      Py_DECREF(frames);
      frames = PyList_New(0);
    }
    if (frames) {
      PyObject *meta = dynchain_frame_meta(KosEvalState_FRAME(es), 1);
      if (meta) {
        PyList_Append(frames, meta);
        Py_DECREF(meta);
        if (PyObject_SetAttrString(val, "__koskari_dynchain__", frames) < 0)
          PyErr_Clear();
      } else {
        PyErr_Clear();
      }
      Py_DECREF(frames);
    } else {
      PyErr_Clear();
    }
  }

  PyErr_Restore(typ, val, tb);
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
  // I cannot tell you how long this took to debug...
  PyErr_NormalizeException(exc_type, exc_val, exc_tb);
  #endif

  decorate_exception_with_dynchain(ctx, *exc_val);

  KosEvalState _inj_es;
  KosEvalState_Init(&_inj_es, ctx);
  KosDynFrame *frame_top = KosEvalState_FRAME(&_inj_es);
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


static void kos_eval_mark_context_dead(KosContext *ctx) {

  KosContext_FLAGS(ctx) &= ~KOS_CTX_RUNNING;
  KosContext_FLAGS(ctx) |= KOS_CTX_DEAD;
}


/**
   Inject traceback into the current PyErr in-place, leave PyErr set, and
   mark the context dead.  Call only when PyErr is set.  When es is non-NULL,
   flushes live pc/acting_pc onto the top frame first so traceback sees the
   current instruction.
*/
void kos_eval_prepare_boundary_raise(
    KosContext *ctx,
    KosEvalState *es) {

  PyObject *typ = NULL, *val = NULL, *tb = NULL;

  if (es)
    KosEvalState_FlushLiveRegsToFrame(es);
  PyErr_Fetch(&typ, &val, &tb);
  inject_eval_frame_traceback(ctx, &typ, &val, &tb);
  PyErr_Restore(typ, val, tb);
  kos_eval_mark_context_dead(ctx);
}


/**
   Intercept a KoskariContinuationApply exception and install the appropriate
   transfer plan.  PyErr must be set to KoskariContinuationApply on entry.

   Own-token path: the continuation targets this eval context.  Extract the
   continuation and resume value, clear PyErr, and call
   kos_invoke_continuation to install a direct-apply transfer plan.

   Foreign-token path: the continuation targets a different eval context.
   Fetch and clear PyErr, insert a KosDynCCRaise sentinel at the root of the
   dynamic chain, and build a NULL-target transfer plan that unwinds all local
   WITH/finally extents and then re-raises the original exception via the
   sentinel's unwind callback.  The eval loop then exits with KOS_RES_RAISE so
   the exception propagates upward to the owning context.

   :return: EVAL_REPROLOGUE on success, EVAL_RAISE on error
*/
static KosOpResult handle_continuation_apply(KosEvalState *es) {

  KosContext *ctx = KosEvalState_CONTEXT(es);

  PyObject *typ = NULL, *val = NULL, *tb = NULL;
  PyErr_Fetch(&typ, &val, &tb);
  Py_XDECREF(tb);

  if (! val) {
    Py_XDECREF(typ);
    PyErr_SetString(PyExc_RuntimeError,
                    "KoskariContinuationApply: missing exception value");
    return EVAL_RAISE();
  }

  PyObject *exc_target = PyObject_GetAttr(val, str_target);
  if (! exc_target) {
    Py_DECREF(typ);
    Py_DECREF(val);
    return EVAL_RAISE();
  }

  int is_own = (exc_target == (PyObject *) KosContext_TOKEN(ctx));
  Py_DECREF(exc_target);

  if (is_own) {
    PyObject *cont_obj = PyObject_GetAttr(val, str_continuation);
    PyObject *args_obj = PyObject_GetAttr(val, str_args);
    KosContinuation *k = NULL;
    PyObject *resume_val = NULL;

    if (! cont_obj || ! args_obj)
      goto own_fail;

    if (KosPyDelimitedContinuation_Check(cont_obj))
      k = KosPyDelimitedContinuation_CAPTURED((KosPyDelimitedContinuation *) cont_obj);
    else if (KosPyContinuation_Check(cont_obj))
      k = KosPyContinuation_CAPTURED((KosPyContinuation *) cont_obj);
    else {
      PyErr_Format(PyExc_TypeError,
                   "KoskariContinuationApply.continuation must be Continuation"
                   " or DelimitedContinuation, got %R", cont_obj);
      goto own_fail;
    }

    if (! PyTuple_Check(args_obj)) {
      PyErr_Format(PyExc_TypeError,
                   "KoskariContinuationApply.args must be tuple, got %R",
                   args_obj);
      goto own_fail;
    }

    if (PyTuple_GET_SIZE(args_obj) > 0) {
      resume_val = PyTuple_GET_ITEM(args_obj, 0);
      Py_INCREF(resume_val);
    } else {
      resume_val = Py_None;
      Py_INCREF(resume_val);
    }

    if (kos_invoke_continuation(es, k, resume_val, 0, cont_obj) == 0) {
      Py_DECREF(resume_val);
      Py_DECREF(args_obj);
      Py_DECREF(typ);
      Py_DECREF(val);
      Py_DECREF(cont_obj);
      return EVAL_REPROLOGUE();
    }
    if (KosEvalState_PLAN(es) || KosEvalState_PLAN_STACK(es))
      kos_abandon_transfer_nest(es);
    if (! PyErr_Occurred())
      PyErr_SetString(PyExc_RuntimeError,
                      "KoskariContinuationApply: invoke failed without PyErr");
    Py_DECREF(resume_val);

own_fail:
    Py_XDECREF(args_obj);

    Py_DECREF(typ);
    Py_DECREF(val);
    Py_XDECREF(cont_obj);

    if (! PyErr_Occurred())
      PyErr_SetString(PyExc_RuntimeError,
                      "KoskariContinuationApply: invalid continuation");
    return EVAL_RAISE();
  }

  /* Foreign-token path: unwind this context to null and re-raise. */
  KosTransferPlan *plan = NULL;
  if (kos_build_cc_unwind_plan(es, typ, val, &plan) < 0) {
    Py_DECREF(typ);
    Py_DECREF(val);
    return EVAL_RAISE();
  }
  Py_DECREF(typ);
  Py_DECREF(val);

  KosEvalState_ArmTransferPlan(es, plan);
  return EVAL_REPROLOGUE();
}


/**
   Unified exception ingress (the RAISE hub).  All PyErr / KOS_RES_RAISE
   ingress converges here: post-opcode results (apply_kos_op_result),
   prologue and signal-poll pending PyErr (eval_phase_pending_exception),
   revive-throw (eval_phase_entry KOS_MODE_EXCEPTION), and transfer-plan
   step failures (run_transfer_plan_to_completion).  Two deliberate
   non-ingress carve-outs live in run_transfer_plan_to_completion: the
   foreign cc-unwind sentinel re-emit (signal delivery, not a failed
   transfer; must not see try_local) and a pending KoskariContinuationApply
   between drain steps (expected in-flight state; keep stepping).

   Ordering contract:
     1. guard        - no PyErr: SystemError + prepare_boundary_raise,
                       EVAL_RAISE
     2. cc test      - PyErr_ExceptionMatches(KoskariContinuationApply),
                       exactly once
     3. nest abandon - kos_abandon_transfer_nest when a plan or plan stack
                       is live AND the exception is not a continuation
                       signal
     4. cc dispatch  - continuation signal: handle_continuation_apply
                       (own token: invoke; foreign: cc-unwind plan);
                       REPROLOGUE re-enters the loop with a plan armed
     5. agen clear   - clear_kos_agen_boundary_on_error before the
                       exception can escape an async-gen boundary step
     6. try_local    - kos_dyn_try_local_raise_body walks TRY / FINALLY /
                       WITH / unwind extents
     7. outcome map  - REPROLOGUE: handler applied, restart prologue.
                       AWAIT: async unwind started; tag KOS_SUSP_AWAIT,
                       mark suspended, propagate awaitable (caller owns
                       tl.val).
                       RAISE: propagate; kos_eval_prepare_boundary_raise
                       already ran inside try_local - never call it again
                       (boundary-control-reemit.md invariant 7.4).
                       FATAL: mark context dead, EVAL_FATAL.

   :return: EVAL_REPROLOGUE to continue, EVAL_AWAIT on async unwind,
            EVAL_RAISE to propagate (PyErr set), EVAL_FATAL on internal
            fault
*/
static KosOpResult process_raise(KosEvalState *es) {
  KosContext *ctx = KosEvalState_CONTEXT(es);

  if (! PyErr_Occurred()) {
    PyErr_SetString(PyExc_SystemError,
                    "process_raise: no exception set");
    kos_eval_prepare_boundary_raise(ctx, es);
    return EVAL_RAISE();
  }

  int is_cc_apply = (KoskariContinuationApply
                     && PyErr_ExceptionMatches(KoskariContinuationApply));

  if (! is_cc_apply
      && (KosEvalState_PLAN(es) || KosEvalState_PLAN_STACK(es)))
    kos_abandon_transfer_nest(es);

  if (is_cc_apply)
    return handle_continuation_apply(es);

  clear_kos_agen_boundary_on_error(es);

  {
    KosOpResult tl = kos_dyn_try_local_raise_body(es);
    switch (tl.kind) {
    case KOS_RES_REPROLOGUE:
      return EVAL_REPROLOGUE();
    case KOS_RES_AWAIT:
      KosEvalState_SetSuspensionEvent(es, KOS_SUSP_AWAIT);
      KosEvalState_MarkSuspendedNotRunning(es);
      return tl;
    case KOS_RES_FATAL:
      kos_eval_mark_context_dead(ctx);
      return EVAL_FATAL();
    default:
      /* RAISE: PyErr set by kos_eval_prepare_boundary_raise in try_local */
      return EVAL_RAISE();
    }
  }
}


/**
   Resolve KosOp_ARG(insn) as a prompt_id constant from the current code.
   Returns -1 on error, 0 on success with *out_prompt_id set.
*/
static int get_prompt_id_from_insn(
    KosEvalState *es,
    KosOp *insn,
    uint32_t *out_prompt_id) {

  PyObject *cid = KosEvalState_CONST(es, KosOp_ARG(insn));
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


/* === Exception types === */

PyObject *KoskariContinuationDead = NULL;
PyObject *KoskariContinuationApply = NULL;
PyObject *KoskariContinuationError = NULL;


/* === KosContext implementation === */


#define CONTEXT_MAX_FREE 16  /* cap for cached KosContext; tunable */

static size_t context_freecount = 0;
static KosContext *context_freelist = NULL;


/**
   Return a KosContext struct to the freelist or free it.  Struct
   teardown only; callers must have released all owned references.
*/
static void context_release(KosContext *ctx) {
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


KosContext *KosContext_New(KosDynFrame *top) {

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

  KosContext_TOKEN(ctx) = evaluator_token_new();
  if (! KosContext_TOKEN(ctx)) {
    context_release(ctx);
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
        context_release(ctx);
        return NULL;
      }
    }
  }

  KosContext_DYN_TOP(ctx) = (KosDynNode *) top;
  KosContext_FRAME_TOP(ctx) = top;

  KosContext_PENDING_RETURN_VALUE(ctx) = NULL;
  KosContext_PENDING_RETURN_TO_PARENT(ctx) = 0;

  /* Initialize transfer plan state */
  KosContext_PLAN(ctx) = NULL;
  KosContext_PLAN_STACK(ctx) = NULL;
  Py_CLEAR(KosContext_PY_AWAIT_ITER(ctx));

  /* Initialize Python await bridging */
  KosContext_PY_RESUME_INJECT(ctx) = NULL;
  KosContext_PY_RESUME_KIND(ctx) = KOS_RESUME_NONE;

  /* Initialize exception injection */
  KosContext_INJECT_MODE(ctx) = KOS_INJECT_NONE;
  KosContext_INJECT_TYPE(ctx) = NULL;
  KosContext_INJECT_VALUE(ctx) = NULL;

  KosContext_FLAGS(ctx) = 0;

  { /* Lazy and evil */
    KosEvalState es;

    KosEvalState_Init(&es, ctx);
    KosEvalState_ThawFromContext(&es);
    kos_dyn_rebuild_live_from_chain(&es);
    KosEvalState_FreezeToContext(&es);
  }

  return ctx;
}


void KosContext_Free(KosContext *ctx) {

  /*
   * Memory teardown only: releases dyn nodes and operand stacks via
   * kos_dynchain_free without running extent protocol (WITH __exit__,
   * PARAM restore, WIND hooks). Callers with a frozen async generator must
   * run KosPyAsyncGen_Teardown first so GEN.active is disposed before ctx.
   */
  if (! ctx)
    return;

  Py_XDECREF(KosContext_TOKEN(ctx));
  KosContext_TOKEN(ctx) = NULL;

  if (KosContext_DYN_TOP(ctx))
    kos_dynchain_free(KosContext_DYN_TOP(ctx), NULL);
  KosContext_DYN_TOP(ctx) = NULL;

  /* Free any active transfer plan */
  kos_free_transfer_plan(KosContext_PLAN(ctx));
  while (KosContext_PLAN_STACK(ctx)) {
    KosTransferPlan *p = KosContext_PLAN_STACK(ctx);
    KosContext_PLAN_STACK(ctx) = KosTransferPlan_SUSPENDED(p);
    kos_free_transfer_plan(p);
  }

  Py_CLEAR(KosContext_PY_AWAIT_ITER(ctx));
  Py_XDECREF(KosContext_PY_RESUME_INJECT(ctx));
  Py_XDECREF(KosContext_INJECT_TYPE(ctx));
  KosContext_INJECT_TYPE(ctx) = NULL;
  Py_XDECREF(KosContext_INJECT_VALUE(ctx));
  Py_XDECREF(KosContext_PENDING_RETURN_VALUE(ctx));
  KosContext_PENDING_RETURN_VALUE(ctx) = NULL;
  if (KosContext_EXC_STASH(ctx)) {
    SAYF("[eval] context teardown dropping %zd parked exception(s)\n",
         PyList_GET_SIZE(KosContext_EXC_STASH(ctx)));
    Py_CLEAR(KosContext_EXC_STASH(ctx));
  }
  Py_XDECREF(KosContext_EXC_INFO_TYPE(ctx));
  KosContext_EXC_INFO_TYPE(ctx) = NULL;
  Py_XDECREF(KosContext_EXC_INFO_VALUE(ctx));
  KosContext_EXC_INFO_VALUE(ctx) = NULL;
  Py_XDECREF(KosContext_EXC_INFO_TB(ctx));
  KosContext_EXC_INFO_TB(ctx) = NULL;
  Py_XDECREF(KosContext_DOMAIN(ctx));
  KosContext_DOMAIN(ctx) = NULL;

  context_release(ctx);
}


int KosContext_Traverse(
    KosContext *ctx,
    visitproc visit,
    void *arg) {

  KosDynNode *dyn_top;
  KosTransferPlan *plan;
  KosEvalState *active_es;
  int res;

  if (! ctx)
    return 0;

  Py_VISIT(KosContext_TOKEN(ctx));
  Py_VISIT(KosContext_DOMAIN(ctx));
  Py_VISIT(ctx->py_await_iter);
  Py_VISIT(KosContext_PY_RESUME_INJECT(ctx));
  Py_VISIT(KosContext_INJECT_TYPE(ctx));
  Py_VISIT(KosContext_INJECT_VALUE(ctx));
  Py_VISIT(KosContext_EXC_STASH(ctx));
  Py_VISIT(KosContext_EXC_INFO_TYPE(ctx));
  Py_VISIT(KosContext_EXC_INFO_VALUE(ctx));
  Py_VISIT(KosContext_EXC_INFO_TB(ctx));

  dyn_top = KosContext_DYN_TOP(ctx);
  plan = KosContext_PLAN(ctx);
  if ((! dyn_top || ! plan)
      && (KosContext_FLAGS(ctx) & KOS_CTX_RUNNING)) {
    active_es = kos_tls_active_eval_for_ctx(ctx);
    if (active_es) {
      if (! dyn_top)
        dyn_top = KosEvalState_DYN_TOP(active_es);
      if (! plan)
        plan = KosEvalState_PLAN(active_es);
    }
  }

  res = kos_dynchain_traverse(dyn_top, NULL, visit, arg);
  if (res)
    return res;

  res = kos_transferplan_traverse(plan, visit, arg);
  if (res)
    return res;

  return 0;
}


/* === Frame namespace helpers (for VAR_* opcodes) === */


/**
   Get the name constant for a VAR_* opcode. Borrowed reference.
*/
static PyObject *get_name_from_insn(KosEvalState *es, KosOp *insn) {
  return KosEvalState_CONST(es, KosOp_ARG(insn));
}


/**
   Deref name in the current frame namespace. New reference or NULL.
*/
static PyObject *frame_ns_deref(KosEvalState *es, PyObject *name) {
  PyObject *ns = KosEvalState_CurrentNamespace(es);

  if (KosNamespace_Check(ns))
    return KosNamespace_Deref(ns, name);

  PyErr_Format(PyExc_TypeError, "frame namespace must be Namespace,"
               " got %R", ns);
  return NULL;
}


/**
   Store value into name in the current frame namespace. Caller retains
   ownership of val.
*/
static int frame_ns_store(KosEvalState *es, PyObject *name, PyObject *val) {
  PyObject *ns = KosEvalState_CurrentNamespace(es);

  if (KosNamespace_Check(ns))
    return KosNamespace_Store(ns, name, val);

  PyErr_Format(PyExc_TypeError,
               "frame namespace must be Namespace, got %R", ns);
  return -1;
}


/**
   Bind name to reference in the current frame namespace.
*/
static int frame_ns_bind(KosEvalState *es, PyObject *name, PyObject *ref) {
  PyObject *ns = KosEvalState_CurrentNamespace(es);

  if (KosNamespace_Check(ns))
    return KosNamespace_Bind(ns, name, ref);

  PyErr_Format(PyExc_TypeError,
               "frame namespace must be Namespace, got %R", ns);
  return -1;
}


/**
   Clear name from the current frame namespace.
*/
static int frame_ns_clear(KosEvalState *es, PyObject *name) {
  PyObject *ns = KosEvalState_CurrentNamespace(es);
  if (KosNamespace_Check(ns))
    return KosNamespace_Clear(ns, name);
  PyErr_Format(PyExc_TypeError,
               "frame namespace must be Namespace, got %R", ns);
  return -1;
}


/**
   Deref name in the current frame namespace, clear the binding, and
   return the value. New reference or NULL.
*/
static PyObject *frame_ns_take(KosEvalState *es, PyObject *name) {
  PyObject *ns = KosEvalState_CurrentNamespace(es);
  if (! KosNamespace_Check(ns)) {
    PyErr_Format(PyExc_TypeError,
                "frame namespace must be Namespace, got %R", ns);
    return NULL;
  }

  PyObject *val = KosNamespace_Deref(ns, name);
  if (! val)
    return NULL;

  if (KosNamespace_Clear(ns, name) < 0) {
    Py_DECREF(val);
    return NULL;
  }
  return val;
}


/**
   Get reference for name in the current frame namespace. Namespace only.
   Borrowed reference; caller must INCREF if keeping.
*/
static PyObject *frame_ns_ref(KosEvalState *es, PyObject *name) {
  PyObject *ns = KosEvalState_CurrentNamespace(es);

  if (! KosNamespace_Check(ns)) {
    PyErr_Format(PyExc_TypeError,
                 "frame namespace must be Namespace for VAR_REF, got %R", ns);
    return NULL;
  }

  return KosNamespace_Ref(ns, name);
}


/**
   Resolve a module for the IMPORT / IMPORT_FROM / IMPORT_STAR opcodes.

   For a relative import (level > 0) the frame's globals namespace
   supplies __name__ and __package__; a temporary dict holding those keys
   is passed to PyImport_ImportModuleLevelObject (which requires dict or
   NULL, not KosNamespace) so Python can resolve the anchor package.

   The leaf flag selects the fromlist: the ("*",) sentinel returns the
   deepest (leaf) submodule, matching importlib.import_module; a NULL
   fromlist returns the root package while still importing the full
   dotted chain, matching a plain Python "import foo.bar".

   :param es: eval state (supplies frame globals for relative anchors)
   :param name: module name str, or Py_None for a bare relative import
   :param level: number of leading dots; 0 = absolute
   :param leaf: non-zero to return the leaf module; zero for the root
   :return: new module reference, or NULL with error set
*/
static PyObject *kos_import_module(
    KosEvalState *es,
    PyObject *name,
    long level,
    int leaf) {

  PyObject *fromlist = leaf ? _import_fromlist : NULL;
  PyObject *globals_dict = NULL;

  if (name == Py_None)
    name = NULL;

  if (level > 0) {
    PyObject *globals_ns = KosEvalState_GlobalsNamespace(es);

    PyObject *name_val = KosNamespace_Deref(globals_ns, str_name);
    if (! name_val)
      return NULL;

    PyObject *package_val = KosNamespace_Deref(globals_ns, str_package);
    if (! package_val) {
      Py_DECREF(name_val);
      return NULL;
    }

    globals_dict = PyDict_New();
    if (! globals_dict) {
      Py_DECREF(name_val);
      Py_DECREF(package_val);
      return NULL;
    }

    if (PyDict_SetItem(globals_dict, str_name, name_val) ||
        PyDict_SetItem(globals_dict, str_package, package_val)) {
      Py_DECREF(name_val);
      Py_DECREF(package_val);
      Py_DECREF(globals_dict);
      return NULL;
    }
    Py_DECREF(name_val);
    Py_DECREF(package_val);
  }

  PyObject *mod = PyImport_ImportModuleLevelObject(
    name, globals_dict, NULL, fromlist, (int) level);
  Py_XDECREF(globals_dict);
  return mod;
}


/* === Opcode handlers (always inlined; return KosOpResult via EVAL_* macros) === */


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
   SHIFT, CALL on DelimitedContinuation, prompt - continuations
 */


static KosOpResult kos_await_dispatch(KosEvalState *es, PyObject *value) {
  return push_or_await_plain(es, value, 1);
}


/* === CALL === */


/**
   CALL: arg = number of positional arguments N. Stack has [callable, arg_0,
   ..., arg_{N-1}] (callable oldest). Call callable with those args, drop
   them, then push result or await it.
*/


/* Pop right (container) then left (item); PySequence_Contains(right, left).
   Branch to KosOp_ARG(insn) when cmp == branch_when (CHECK_IN: 0,
   CHECK_NOT_IN: 1). */
static int kos_check_contains(
    KosEvalState *es,
    KosOp *insn,
    int branch_when) {

  KosEvalStackLR lr = {0};
  if (KOS_EVALSTATE_POP_ARGS(es, lr))
    return -1;

  int cmp = PySequence_Contains(lr.right, lr.left);
  Py_DECREF(lr.left);
  Py_DECREF(lr.right);
  if (unlikely(cmp < 0))
    return -1;

  if (cmp == branch_when)
    KosEvalState_SET_PC(es, KosOp_ARG(insn));

  return 0;
}


/* Pop right then left; branch to KosOp_ARG(insn) when (left == right) == branch_when_equal
   (CHECK_IS: 0 so branch when not equal; CHECK_IS_NOT: 1 so branch when equal). */
static int kos_check_identity(
    KosEvalState *es,
    KosOp *insn,
    int branch_when_equal) {

  KosEvalStackLR lr = {0};
  if (KOS_EVALSTATE_POP_ARGS(es, lr))
    return -1;

  int cmp = (lr.left == lr.right);
  Py_DECREF(lr.left);
  Py_DECREF(lr.right);
  if (cmp == branch_when_equal)
    KosEvalState_SET_PC(es, KosOp_ARG(insn));

  return 0;
}


/* Pop right then left; call check(left, right). Branch to KosOp_ARG(insn) when res == 0. */
static int kos_check_two_arg(
    KosEvalState *es,
    KosOp *insn,
    int (*check)(PyObject *, PyObject *)) {

  KosEvalStackLR lr = {0};
  if (KOS_EVALSTATE_POP_ARGS(es, lr))
    return -1;

  int res = check(lr.left, lr.right);
  Py_DECREF(lr.left);
  Py_DECREF(lr.right);
  if (unlikely(res < 0))
    return -1;

  if (res == 0)
    KosEvalState_SET_PC(es, KosOp_ARG(insn));

  return 0;
}


/* Pop one value; branch to KosOp_ARG(insn) when it is NOT an empty-list
   singleton (nil or bracket-nil) — the squashed form of NILP followed
   by JUMP_FALSE. */
static int kos_check_nil(
    KosEvalState *es,
    KosOp *insn) {

  PyObject *val = KosEvalState_StackPopRef(es);
  if (! val)
    return -1;

  int is_nil = KosNilP(val);
  Py_DECREF(val);

  if (! is_nil)
    KosEvalState_SET_PC(es, KosOp_ARG(insn));

  return 0;
}


/* Helper for CHECK_EQ/GE/GT/LE/LT/NE: EXTRA row is (COUNT, LABEL). Peek the
   top COUNT operands and chain-compare them left-to-right with RichCompareBool;
   jump to the LABEL target if the chain fails (any adjacent pair compares
   false). The operands are dropped either way. */
static int kos_check_cmp(
    KosEvalState *es,
    KosOp *insn,
    int cmp_op) {

  KosCode *code = KosEvalState_CurrentCode(es);
  KosExtra row = KosCode_GetExtra(code, KosOp_ARG(insn));
  size_t count = (size_t) row.a0;

  PyObject *const *args = KosEvalState_StackTailPtr(es, count);
  if (unlikely(! args))
    return -1;

  int res = 1;
  for (size_t i = 1; i < count; i++) {
    res = PyObject_RichCompareBool(args[i - 1], args[i], cmp_op);
    if (res <= 0)
      break;
  }

  KosEvalState_StackDropNoErr(es, count);

  if (res < 0)
    return -1;

  if (res == 0)
    KosEvalState_SET_PC(es, row.a1);

  return 0;
}


/* === THEEND, BREAK, CONTINUE, YIELD, YIELD_FROM === */

/* THEEND: no runtime implementation; hardcoded error (not a "stub" to be implemented). */

/**
   THEEND: arg unused. End-of-code sentinel; has no runtime implementation.
   If execution reaches THEEND, raise a hardcoded unimplemented error.
*/


/**
   Clear agen->ctx dyn/frame head and graft gen onto the consumer
   (KosEvalState_CONTEXT(es)). Frozen gens use ACTIVE; bootstrap gens use
   agen->ctx dyn_top only.
*/
static int kos_attach_agen_gen_flatten(
    KosEvalState *es,
    KosPyAsyncGen *agen,
    KosDynGen *gen) {

  if (KosDynGen_ACTIVE(gen)) {
    KosContext_DYN_TOP(agen->ctx) = NULL;
    KosContext_FRAME_TOP(agen->ctx) = NULL;
    return kos_dyn_gen_install(es, gen);
  }

  if (agen->ctx && KosContext_DYN_TOP(agen->ctx))
    return kos_dyn_gen_install_bootstrap(es, agen->ctx, gen);

  PyErr_SetString(PyExc_RuntimeError, "async generator has no active frame");
  return -1;
}


/**
   Resolve the agen's GEN node and flatten it onto the consumer ctx.
   Sets RuntimeError when the agen has no GEN.  Shared core of the
   NEXT / ITER_NEXT / YIELD_FROM attach variants below; each caller
   runs its own undo on failure.

   :return: 0 on success, -1 on error (PyErr set)
*/
static int attach_agen_gen_checked(
    KosEvalState *es,
    KosPyAsyncGen *agen) {

  KosDynGen *gen = (KosDynGen *) agen->gen;

  if (! gen) {
    PyErr_SetString(PyExc_RuntimeError,
                    "async generator has no active frame");
    return -1;
  }
  return kos_attach_agen_gen_flatten(es, agen, gen);
}


/**
   Install GEN onto consumer ctx (Koskari flatten). Clears agen->ctx chain
   so state lives only in GEN. kos_dyn_gen_install pushes Py_None.
   Caller must have already: checked in_progress, handled closed/ctx.
   Caller must have pushed KOS_DYN_NEXT before calling (NEXT opcode).
   On error: aborts NEXT node, resets in_progress; DECREFs iter and
   default_val.
*/
static int op_attach_koskari_agen_frame(
    KosEvalState *es,
    KosPyAsyncGen *agen,
    PyObject *iter,
    PyObject *default_val) {

  if (attach_agen_gen_checked(es, agen) < 0) {
    kos_dyn_next_abort_after_failed_attach(es);
    Py_DECREF(iter);
    Py_XDECREF(default_val);
    agen->in_progress = 0;
    return -1;
  }

  return 0;
}


/**
   ITER_NEXT variant: install GEN onto consumer ctx. On error clear
   FOREACH iter_step_phase when region is a FOREACH node.
*/
static int op_attach_koskari_agen_frame_for_iter_next(
    KosEvalState *es,
    KosPyAsyncGen *agen,
    KosDynNode *region) {

  if (attach_agen_gen_checked(es, agen) < 0) {
    if (KosDynNode_IS_FOREACH(region))
      KosDynForeach_ITER_STEP_PHASE((KosDynForeach *) region) =
          KOS_FOREACH_ITER_STEP_IDLE;
    agen->in_progress = 0;
    return -1;
  }

  return 0;
}


/**
   YIELD_FROM: install delegate Koskari async gen onto consumer ctx.  dyn_top
   must be KOS_DYN_YIELD_FROM.  On error pops yield-from node.
*/
static int op_attach_koskari_agen_frame_for_yield_from(
    KosEvalState *es,
    KosPyAsyncGen *agen,
    KosDynYieldFrom *yf) {

  if (attach_agen_gen_checked(es, agen) < 0) {
    if (kos_dyn_pop_yield_from(es, yf) < 0)
      return -1;
    agen->in_progress = 0;
    return -1;
  }

  return 0;
}


/* === NEXT, ITER_NEXT === */


/**
   Route a return value to a live delimited-invoke bookmark: a DELIM
   carrying RETURN_FRAME + RETURN_PC (POP_PROMPT-mode push-graft or
   INPLACE-mode in-place invoke).  Shared by the POP_PROMPT opcode (the
   reset body completed normally) and the RETURN unwind walk (a return
   crossed the reset boundary); both deliver val to the k-invocation
   site.  val is a new reference, consumed on every path.

   :return: EVAL_NORMAL when routed (execution resumes at the invoke
            site) or when an async unwind started (pending return
            staged); EVAL_RAISE on error
*/
static KosOpResult kos_eval_route_delim_return(
    KosEvalState *es,
    KosDynDelim *ret_delim,
    PyObject *val) {

  SAYF("[eval] route_delim_return: unwinding to return frame=%p node=%p",
       (void *) KosDynDelim_RETURN_FRAME(ret_delim),
       (void *) KosDynDelim_RETURN_NODE(ret_delim));

  KosTransitionReq treq;
  if (KosDynDelim_RETURN_NODE(ret_delim) != NULL) {
    treq = (KosTransitionReq) {
      .kind = KOS_TRANS_UNWIND_TO_NODE,
      .xfer_event = KOS_XFER_EVENT_SHIFT,
      .target_node = KosDynDelim_RETURN_NODE(ret_delim),
    };
  } else {
    treq = (KosTransitionReq) {
      .kind = KOS_TRANS_UNWIND_TO_FRAME,
      .xfer_event = KOS_XFER_EVENT_SHIFT,
      .target_frame = KosDynDelim_RETURN_FRAME(ret_delim),
    };
  }
  {
    KosOpResult _kr = KosEvalState_TransitionOp(es, &treq);
    if (_kr.kind == KOS_RES_RAISE) {
      Py_DECREF(val);
      return EVAL_RAISE();
    }
    if (_kr.kind != KOS_RES_NORMAL) {
      /* REPROLOGUE or AWAIT: async unwind started; stage pending return */
      Py_XDECREF(_kr.val);
      KosEvalState_SetPendingReturn(es, val, 0);
      return EVAL_NORMAL();
    }
  }

  SAYF("[eval] route_delim_return after unwind: dyn_top=%p kind=%s",
       (void *) KosEvalState_CurrentNode(es),
       KosEvalState_CurrentNode(es)
           ? kos_dynnode_kind_name(
                 KosDynNode_KIND(KosEvalState_CurrentNode(es)))
           : "NULL");

  if (KosDynDelim_MODE(ret_delim) == KOS_DYN_DELIM_MODE_INPLACE) {
    if (kos_dyn_finish_inplace_pop_return(es, ret_delim, val) < 0)
      return EVAL_RAISE();
  } else if (kos_dyn_finish_delimited_pop_return(es, ret_delim, val) < 0) {
    return EVAL_RAISE();
  }
  return EVAL_NORMAL();
}


/*
  Includes the implementation of all opcodes from the opcode/ directory.
*/
/* The THEEND barrier handler reports the same fault as an off-the-end fetch;
   the reporter is defined below (with the dispatch loop). */
static void kos_eval_report_pc_bounds_error(KosEvalState *es);
#define KOS_OP_HANDLER(name) \
  KOS_OP_INLINE KosOpResult op_##name(KosEvalState *es, KosOp *insn)

/* Cold variant of KOS_OP_HANDLER: same signature, but kept OUT of the inlined
   dispatch function (the dispatch label calls it instead).  For large,
   rarely-executed opcodes whose body would only bloat the hot loop. */
#define KOS_OP_HANDLER_COLD(name) \
  static KOS_NOINLINE KosOpResult op_##name(KosEvalState *es, KosOp *insn)

#define CURRENT_NS() KosEvalState_NAMESPACE(es)

/* Namespace-presence guard for opcode handlers.  es->namespace is a live
   register, always set during valid execution, so this catches only an
   internal state-machine bug — compiled out unless KOS_EVAL_SAFETYNET.
   Keeping it a macro keeps the 35+ (and growing) ns-touching handlers slim
   in the single inlined dispatch function. */
#if KOS_EVAL_SAFETYNET
#define KOS_OP_REQUIRE_NS(ns)                                           \
  do {                                                                  \
    if (unlikely(! (ns))) {                                             \
      PyErr_SetString(PyExc_RuntimeError, "opcode on NULL namespace");  \
      return EVAL_RAISE();                                              \
    }                                                                   \
  } while (0)
#else
#define KOS_OP_REQUIRE_NS(ns)  ((void) 0)
#endif

#include "handlers.incl"
#undef CURRENT_NS
#undef KOS_OP_REQUIRE_NS
#undef KOS_OP_HANDLER_COLD
#undef KOS_OP_HANDLER


/**
   Invoke the native frame handler on the current eval frame.  val is a new
   reference transferred to the handler (val may be NULL for the iteration
   call; delivery call passes the return value).  Returns EVAL_FATAL() if no
   handler is installed.
*/
static KosOpResult kos_eval_invoke_native_handler(
    KosEvalState *es,
    PyObject *val) {

  KosDynFrame *frame = KosEvalState_FRAME(es);
  if (! frame || ! KosDynFrame_NATIVE_HANDLER(frame)) {
    Py_XDECREF(val);
    return EVAL_FATAL();
  }

  KosNativeHandlerFn fn = (KosNativeHandlerFn) KosDynFrame_NATIVE_HANDLER(frame);
  return fn(es, val);
}


/* Forward declaration: defined after eval_step_native_iteration. */
static KosOpResult apply_kos_op_result(KosEvalState *es, KosOpResult res);


/**
   Run revive_await and normalize its outcome for eval_phase_entry: on
   KOS_RES_AWAIT tag the suspension and return the terminal result;
   otherwise set *mode (EXCEPTION on RAISE, NORMAL otherwise) and return
   EVAL_NORMAL.
*/
static KosOpResult entry_revive_await(
    KosEvalState *es,
    PyObject *send_val,
    PyObject *throw_typ,
    PyObject *throw_exc,
    KosEvalMode *mode) {

  KosOpResult ra = revive_await(es, send_val, throw_typ, throw_exc);

  if (ra.kind == KOS_RES_AWAIT) {
    KosEvalState_SetSuspensionEvent(es, KOS_SUSP_AWAIT);
    KosEvalState_MarkSuspendedNotRunning(es);
    return ra;
  }
  *mode = (ra.kind == KOS_RES_RAISE) ? KOS_MODE_EXCEPTION : KOS_MODE_NORMAL;
  return EVAL_NORMAL();
}


/**
   Phase 1: entry and revive normalization.

   Determines the initial KosEvalMode by dispatching through the current
   suspension state and send_val/throw_exc arguments.  Handles the EXCEPTION
   outcome in-place via process_raise (the unified RAISE ingress).  Clears
   suspension event after mode selection.

   :param mode_out: Receives the resolved KosEvalMode on EVAL_NORMAL return.
   :return: EVAL_NORMAL when the dispatch loop should proceed;
            terminal KosOpResult when eval should stop.
*/
static KosOpResult eval_phase_entry(
    KosEvalState *es,
    PyObject *send_val,
    PyObject *throw_typ,
    PyObject *throw_exc,
    KosEvalMode *mode_out) {

  KosEvalMode mode = KOS_MODE_NORMAL;
  if (KosEvalState_PLAN(es)) {
    mode = revive_plan(es, send_val, throw_typ, throw_exc);
    if (mode != KOS_MODE_NORMAL && mode != KOS_MODE_TRANSFER)
      goto entry_mode_dispatch;

  } else if (send_val == NULL && throw_typ == NULL && throw_exc == NULL) {
    SAYF("[eval] phase_entry branch: send/throw all NULL, suspend=%d\n",
         KosEvalState_SuspensionEvent(es));
    switch (KosEvalState_SuspensionEvent(es)) {
      case KOS_SUSP_NONE:
        mode = KOS_MODE_NORMAL;
        break;
      case KOS_SUSP_AWAIT:
        if (KosEvalState_PythonAwaitIter(es)) {
          mode = KOS_MODE_NORMAL;
        } else {
          KosOpResult _ra = entry_revive_await(es, Py_None, NULL, NULL,
                                               &mode);
          if (_ra.kind == KOS_RES_AWAIT)
            return _ra;
        }
        break;
      case KOS_SUSP_YIELD:
        /* anext() passes NULL for send; treat as Py_None per async gen protocol */
        SAY("[eval] phase_entry -> revive_yield(Py_None)");
        mode = revive_yield(es, Py_None, throw_typ, throw_exc);
        break;
      default:
        mode = KOS_MODE_NORMAL;
        break;
    }

  } else {
    SAYF("[eval] phase_entry branch: send_val=%p throw_typ=%p throw_exc=%p "
         "suspend=%d\n",
         (void *) send_val, (void *) throw_typ, (void *) throw_exc,
         KosEvalState_SuspensionEvent(es));
    switch (KosEvalState_SuspensionEvent(es)) {
      case KOS_SUSP_YIELD:
        SAY("[eval] phase_entry -> revive_yield(send_val)");
        mode = revive_yield(es, send_val, throw_typ, throw_exc);
        break;
      case KOS_SUSP_AWAIT:
      default: {
        KosOpResult _ra = entry_revive_await(es, send_val, throw_typ,
                                             throw_exc, &mode);
        if (_ra.kind == KOS_RES_AWAIT)
          return _ra;
        break;
      }
    }
  }
  KosEvalState_ClearSuspensionEvent(es);

entry_mode_dispatch:
  if (mode == KOS_MODE_EXCEPTION) {
    KosOpResult pr = process_raise(es);
    SAYF("[eval] phase_entry process_raise kind=%d\n", (int) pr.kind);

    if (pr.kind == KOS_RES_REPROLOGUE) {
      *mode_out = KOS_MODE_NORMAL;
      return EVAL_NORMAL();
    }
    /* AWAIT (tagged by the hub) / RAISE / FATAL are terminal here */
    return pr;
  }

  /* Resume delivery: if we revived with a value and top frame has native_handler, deliver to it */
  if (send_val != NULL
      && mode == KOS_MODE_NORMAL
      && KosEvalState_FRAME(es)
      && KosDynFrame_NATIVE_HANDLER(KosEvalState_FRAME(es))) {

    PyObject *val = KosEvalState_StackPopRef(es);
    if (val) {
      /* val is a new ref; handler takes ownership */
      KosOpResult hr = kos_eval_invoke_native_handler(es, val);
      KosOpResult hs = apply_kos_op_result(es, hr);
      if (hs.kind != KOS_RES_NORMAL
          && hs.kind != KOS_RES_REPROLOGUE
          && hs.kind != KOS_RES_DISPATCH)
        return hs;
      /* NORMAL / REPROLOGUE / DISPATCH: handler finished cleanly; proceed
         into dispatch (DISPATCH is loop-internal and must not escape) */
    }
  }

  *mode_out = mode;
  return EVAL_NORMAL();
}


/**
   Phase 2: transfer-plan synchronization.

   Drains any active transfer plan to completion.  When no frame and no
   hook remain after draining, emits a terminal RETURN(None) and marks
   the context dead.  Abandonment outcomes from the unified RAISE ingress
   (AWAIT / RAISE / FATAL) are terminal here.

   :return: EVAL_NORMAL when the dispatch loop should start;
            terminal KosOpResult when eval should stop.
*/
static KosOpResult eval_phase_transfer(KosEvalState *es) {

  KosContext *ctx = KosEvalState_CONTEXT(es);
  KosOpResult xfer = run_transfer_plan_to_completion(es);

  if (xfer.kind == KOS_RES_REPROLOGUE)
    return EVAL_NORMAL();
  if (xfer.kind != KOS_RES_NORMAL)
    return xfer;  /* AWAIT / RAISE / FATAL */

  if (! KosEvalState_FRAME(es)) {
    kos_eval_mark_context_dead(ctx);
    Py_INCREF(Py_None);
    return EVAL_RETURN(Py_None);
  }

  return EVAL_NORMAL();
}


/**
   Phase 3: async exception check at each loop iteration top.

   When PyErr_Occurred() is set on entry to the dispatch loop (e.g. from a
   signal handler or a mid-loop async exception), calls process_raise to
   attempt local handling.

   :return: EVAL_NORMAL when no PyErr;
            EVAL_REPROLOGUE when handled locally (restart prologue);
            terminal KosOpResult when eval should stop.
*/
static KosOpResult eval_phase_pending_exception(KosEvalState *es) {
  if (! PyErr_Occurred())
    return EVAL_NORMAL();
  return process_raise(es);
}


/**
   YIELD half of dispatch_boundary_event. Caller already ran
   KOS_TRANS_WALK_HANDLE_YIELD.  val is a new ref; ownership is transferred to
   the boundary consumer (NEXT / FOREACH / YIELD_FROM).  Every boundary kind
   hands off and returns control here; the eval loop resumes the receiver in
   normal mode, which drives any further hop (a YIELD_FROM receiver re-emits the
   value as its own yield on its next dispatch).
*/
static KosOpResult eval_event_yield_deliver_to_boundary(
    KosEvalState *es, PyObject *val) {

  if (kos_dyn_deliver_yield_to_iter_boundary(es, val) < 0)
    return EVAL_FATAL();
  return EVAL_REPROLOGUE();
}



/**
   Stage A of post-opcode YIELD: transition walk only (freeze is walk side).
*/
static KosOpResult eval_yield_handle_walk_and_freeze(KosEvalState *es) {
  KosTransitionReq treq = { .kind = KOS_TRANS_WALK_HANDLE_YIELD };
  KosTransitionResult tres = {0};

  if (KosEvalState_Transition(es, &treq, &tres) < 0) {
    return EVAL_FATAL();
  }
  return EVAL_NORMAL();
}


/**
   Centralized YIELD handler.  Flushes live registers to the frame, marks the
   generator dead-after-yield, walks the dynchain, handles optional async-gen
   boundary delivery, and tags the suspension event.  val is a new reference;
   ownership transfers to the caller or boundary consumer.
*/
static KosOpResult eval_event_yield(KosEvalState *es, PyObject *val) {
  KosEvalState_FlushLiveRegsToFrame(es);
  KosEvalState_MarkDeadAfterYield(es);

  SAYF("[eval] post_opcode KOS_RES_YIELD -> walk_handle_yield dyn_top=%p\n",
       (void *) KosEvalState_CurrentNode(es));
  {
    KosOpResult ws = eval_yield_handle_walk_and_freeze(es);
    if (ws.kind != KOS_RES_NORMAL)
      return ws;
  }
  if (kos_dyn_need_kos_agen_boundary_deliver(es))
    return eval_event_yield_deliver_to_boundary(es, val);
  KosEvalState_SetSuspensionEvent(es, KOS_SUSP_YIELD);
  return EVAL_YIELD(val);
}


/**
   Centralized AWAIT handler.  Demand-checks, marks suspended, and tags the
   suspension event.  val ownership transfers on success, or is released on
   demand denial.
*/
static KosOpResult eval_event_await(KosEvalState *es, PyObject *val) {
  if (kos_eval_in_demand(es)) {
    Py_DECREF(val);
    KosContext_FLAGS(KosEvalState_CONTEXT(es)) &= ~KOS_CTX_DEAD;
    KosContext_FLAGS(KosEvalState_CONTEXT(es)) |= KOS_CTX_RUNNING;
    kos_demand_set_sync_violation(
        "async suspension is not allowed in demand_sync");
    KosOpResult rs;
    KOS_TRACE_POST_OP_WRAP(event_raise, rs = process_raise(es));
    return rs;
  }

  KosEvalState_MarkSuspendedNotRunning(es);
  KOS_TRACE_POST_OP_WRAP(event_await,
      KosEvalState_SetSuspensionEvent(es, KOS_SUSP_AWAIT));
  return EVAL_AWAIT(val);
}


/**
   Unwind handler.  Called from eval_event_return_ng when try/with/wind nodes
   sit above the frame (DYN_TOP != FRAME).  Runs the transition walk to clear
   them, then delegates to eval_event_return_at_frame.  val consumed.
*/
static inline KosOpResult eval_event_return(
    KosEvalState *es,
    PyObject *val) {

  KosDynNode *parent = KosDynNode_PARENT(KosEvalState_FRAME(es));
  int has_parent = (parent != NULL && ! KosDynNode_IS_GEN(parent));

  KosTransitionReq treq = {
    .kind = KOS_TRANS_WALK_RETURN_TO_FRAME,
    .target_frame = KosEvalState_FRAME(es),
    .payload = val,
  };
  KosTransitionResult tres = {0};
  if (KosEvalState_Transition(es, &treq, &tres) < 0) {
    Py_DECREF(val);
    return EVAL_FATAL();
  }
  int unw = tres.raw;
  if (unw == KOS_DYN_UNWIND_ASYNC_STARTED) {
    KosEvalState_SetPendingReturn(es, val, has_parent);
    return EVAL_REPROLOGUE();
  }
  if (unw == KOS_DYN_UNWIND_FINALLY_TAKING_RETURN) {
    return EVAL_REPROLOGUE();
  }
  if (unw == KOS_DYN_UNWIND_DELIM_TAKING_RETURN) {
    /* The return crossed a reset boundary: the walk stopped at a DELIM
       with a live invoke bookmark (at dyn_top).  Deliver val to the
       k-invocation site instead of the frame's parent. */
    KosOpResult rr = kos_eval_route_delim_return(
        es, (KosDynDelim *) KosEvalState_DYN_TOP(es), val);
    if (rr.kind == KOS_RES_NORMAL)
      return EVAL_REPROLOGUE();
    return rr;
  }
  if (unw < 0) {
    Py_DECREF(val);
    return EVAL_FATAL();
  }

  return eval_event_return_at_frame(es, val);
}


/**
   NG fast path: plain Koskari frame-to-frame return.

   Inline retarget from child (being freed) to parent.  Skips the chain
   walk, the flush (frame is freed), and the agen boundary check (not
   reachable from a frame whose parent is itself a frame).  val consumed.
*/
static KosOpResult eval_ng_plain_return(
    KosEvalState *es,
    KosDynNode *child,
    KosDynNode *parent,
    PyObject *val) {

  /* Inline retarget: equivalent to commit_dyn_top + apply_live(flush=0),
     but with no chain walk — parent is a known FRAME node, so every live
     register is read directly from it. */
  KosEvalState_DYN_TOP(es) = parent;
  KosEvalState_FRAME(es) = (KosDynFrame *) parent;
  KosEvalState_BLOCK(es) = &KosDynNode_STACK(parent);
  KosEvalState_NAMESPACE(es) = KosDynNode_NS(parent);
  KosEvalState_CODE(es) = KosDynFrame_CODE((KosDynFrame *) parent);
  es->pc = KosDynFrame_PC((KosDynFrame *) parent);
  es->acting_pc = KosDynFrame_ACTING_PC((KosDynFrame *) parent);
  kos_dynframe_free((KosDynFrame *) child);

  if (KosEvalState_StackPushRef(es, val) < 0) {
    Py_DECREF(val);
    return EVAL_FATAL();
  }
  Py_DECREF(val);

  /* Control-returned hook fast-exit, inlined: parent (now dyn_top) is a
     FRAME; only a WITH directly beneath it can demand the hook.  On this
     path no PyErr is set, no plan is armed, and parent has no native
     handler (checked by the case-1 guard), so the dispatch loop can
     resume fetching directly — no prologue re-entry. */
  {
    KosDynNode *pp = KosDynNode_PARENT(parent);
    if (likely(! pp || ! KosDynNode_IS_WITH(pp)))
      return EVAL_DISPATCH();
  }
  return kos_eval_post_opcode_control_returned_after_hook(es);
}


/**
   Unified RETURN dispatch.  Called when DYN_TOP == FRAME — no try/with/wind
   nodes above it (either naturally, or after the unwind walk completes).

   Reads PARENT(FRAME) and dispatches to the correct terminus.  GEN is
   transparent: kos_dyn_gen_unwind_exhausted pops the frame and GEN together,
   then the consumer node above the GEN is the real terminus.  val consumed.
*/
static KosOpResult eval_event_return_at_frame(
    KosEvalState *es,
    PyObject *val) {

  KosDynNode *child = (KosDynNode *) KosEvalState_FRAME(es);
  KosDynNode *parent = KosDynNode_PARENT(child);

  /* Case 1: plain frame-to-frame — the dominant hot path. */
  if (likely(parent
             && KosDynNode_IS_FRAME(parent)
             && ! KosDynFrame_NATIVE_HANDLER((KosDynFrame *) parent))) {
    KosOpResult r;
    KOS_TRACE_POST_OP_WRAP(parent_return,
        r = eval_ng_plain_return(es, child, parent, val));
    return r;
  }

  /* Case 2: gen parent — dispatch on the consumer node above the GEN.
     The consumer is the true terminus; GEN is unwound on the way out. */
  if (parent && KosDynNode_IS_GEN(parent)) {
    KosDynNode *consumer = KosDynNode_PARENT(parent);

    if (! consumer) {
      KosEvalState_MarkDeadAfterReturn(es);
      return EVAL_RETURN(val);
    }

    if (KosDynNode_IS_NEXT(consumer)) {
      int r = kos_dyn_next_deliver_return_node(
          es, (KosDynNext *) consumer, val);
      if (r < 0) {
        if (PyErr_Occurred())
          return EVAL_FATAL();
        return EVAL_REPROLOGUE();
      }
      return EVAL_REPROLOGUE();
    }

    if (KosDynNode_IS_FOREACH(consumer)) {
      if (kos_dyn_foreach_deliver_return_kos_agen(
              es, (KosDynForeach *) consumer, val) < 0)
        return EVAL_FATAL();
      return EVAL_REPROLOGUE();
    }

    if (KosDynNode_IS_YIELD_FROM(consumer)) {
      int r = kos_dyn_yield_from_deliver_inner_return(
          es, (KosDynYieldFrom *) consumer, val);
      if (r < 0) {
        KosOpResult rs;
        KOS_TRACE_POST_OP_WRAP(event_raise, rs = process_raise(es));
        return rs;
      }
      return EVAL_REPROLOGUE();
    }

    PyErr_Format(PyExc_RuntimeError,
                 "eval_event_return: unexpected gen consumer kind %u",
                 (unsigned) KosDynNode_KIND(consumer));
    Py_DECREF(val);
    return EVAL_FATAL();
  }

  /* Case 3: any other non-NULL, non-GEN parent (WIND, TRY, WITH, native
     frame, etc.) — pop the callee frame and let eval_parent_return_deliver
     handle the context transition and any control-return hooks. */
  if (parent) {
    if (unlikely(kos_dyn_pop_frame(es) < 0)) {
      Py_DECREF(val);
      return EVAL_FATAL();
    }
    return eval_parent_return_deliver(es, val);
  }

  /* Case 4: no parent — root frame, eval done. */
  KosEvalState_MarkDeadAfterReturn(es);
  return EVAL_RETURN(val);
}


/**
   Next-generation RETURN handler.  Guards on DYN_TOP == FRAME (no
   try/with/wind unwind needed), then dispatches via eval_event_return_at_frame.
   When the guard fails, the unwind walk in eval_event_return handles it and
   converges on the same dispatch on completion.  val consumed.
*/
static inline KosOpResult eval_event_return_ng(
    KosEvalState *es,
    PyObject *val) {

  if (unlikely(KosEvalState_DYN_TOP(es)
               != (KosDynNode *) KosEvalState_FRAME(es)))
    return eval_event_return(es, val);

  return eval_event_return_at_frame(es, val);
}


/**
   Run WIND/WITH control hook after a CONTROL_RETURNED delivery.

   Inline early exit: a FRAME node only has hook work when its parent is a
   WITH in a pending await phase.  Any other frame-topped chain — the
   dominant plain-return case — skips the dispatch (and its trace wrap)
   entirely.  Mirrors the first branch of kos_dynnode_on_control_returned.
*/
static KosOpResult kos_eval_post_opcode_control_returned_after_hook(
    KosEvalState *es) {

  KosDynNode *node = KosEvalState_CurrentNode(es);
  if (likely(node && KosDynNode_IS_FRAME(node))) {
    KosDynNode *parent = KosDynNode_PARENT(node);
    if (likely(! parent || ! KosDynNode_IS_WITH(parent)))
      return EVAL_REPROLOGUE();
  }

  KosOpResult r;
  KOS_TRACE_POST_OP_WRAP(control_returned,
      r = kos_dynnode_on_control_returned(es, KosEvalState_CurrentNode(es)));
  if (r.kind == KOS_RES_RAISE) {
    KosOpResult rs;
    KOS_TRACE_POST_OP_WRAP(event_raise, rs = process_raise(es));
    return rs;
  }
  return EVAL_REPROLOGUE();
}


/**
   After RETURN opcode pop: iterator boundary, native handler or stack push,
   then kos_dynnode_on_control_returned (WIND/WITH).  val is a new ref;
   ownership is consumed by this function.
*/
static inline KosOpResult eval_parent_return_deliver_impl(
    KosEvalState *es, PyObject *val) {

  if (unlikely(KosDynFrame_NATIVE_HANDLER(KosEvalState_FRAME(es)) != NULL)) {
    KosOpResult hr = kos_eval_invoke_native_handler(es, val);
    return apply_kos_op_result(es, hr);
  }

  if (KosEvalState_StackPushRef(es, val) < 0) {
    Py_DECREF(val);
    return EVAL_FATAL();
  }

  Py_DECREF(val);
  return kos_eval_post_opcode_control_returned_after_hook(es);
}


static KosOpResult eval_parent_return_deliver(KosEvalState *es, PyObject *val) {
  KosOpResult ret;
  KOS_TRACE_POST_OP_WRAP(parent_return,
      ret = eval_parent_return_deliver_impl(es, val));
  return ret;
}


/**
   Top-of-loop transfer drain.
*/
static KosOpResult eval_step_drain_transfer_plan(KosEvalState *es) {
  if (! KosEvalState_PLAN(es))
    return EVAL_NORMAL();
  {
    KosOpResult xfer = run_transfer_plan_to_completion(es);
    if (xfer.kind != KOS_RES_NORMAL)
      return xfer;  /* REPROLOGUE / AWAIT / RAISE / FATAL */
  }
  return EVAL_NORMAL();
}


/**
   One native-frame iteration: run handler with no delivery (val=NULL).
   Caller must only invoke when the eval frame has NATIVE_HANDLER.
*/
static KosOpResult eval_step_native_iteration(KosEvalState *es) {
  KosOpResult res = kos_eval_invoke_native_handler(es, NULL);
  if (res.kind == KOS_RES_NORMAL || res.kind == KOS_RES_REPROLOGUE)
    return EVAL_REPROLOGUE();
  {
    KosOpResult ar = apply_kos_op_result(es, res);
    /* DISPATCH is loop-internal; this path returns into the prologue, so
       normalize it (the prologue re-checks are harmless here). */
    if (ar.kind == KOS_RES_DISPATCH)
      return EVAL_REPROLOGUE();
    return ar;
  }
}


/**
   Route a non-NORMAL opcode result through the appropriate event handler.
   NORMAL and REPROLOGUE both map to EVAL_REPROLOGUE (restart cg_prologue).
*/
static KosOpResult apply_kos_op_result(KosEvalState *es, KosOpResult res) {
  switch (res.kind) {
  case KOS_RES_NORMAL:
  case KOS_RES_REPROLOGUE:
    return EVAL_REPROLOGUE();

  case KOS_RES_RAISE: {
    KosOpResult r;
    KOS_TRACE_POST_OP_WRAP(event_raise, r = process_raise(es));
    return r;
  }

  case KOS_RES_YIELD: {
    KosOpResult r;
    KOS_TRACE_POST_OP_WRAP(event_yield, r = eval_event_yield(es, res.val));
    return r;
  }

  case KOS_RES_AWAIT: {
    KosOpResult r;
    KOS_TRACE_POST_OP_WRAP(event_await, r = eval_event_await(es, res.val));
    return r;
  }

  case KOS_RES_RETURN: {
    KosOpResult r;
    KOS_TRACE_POST_OP_WRAP(return_boundary, r = eval_event_return_ng(es, res.val));
    return r;
  }

  case KOS_RES_CONTROL_RETURNED:
    return kos_eval_post_opcode_control_returned_after_hook(es);

  case KOS_RES_FATAL:
  default:
    SAY("[eval] FATAL: apply_kos_op_result: unknown or fatal KosOpResultKind\n");
    return EVAL_FATAL();
  }
}


static void kos_eval_report_pc_bounds_error(KosEvalState *es) {

  KosDynFrame *rep_pf = KosEvalState_FRAME(es);

  if (! rep_pf) {
    PyErr_SetString(PyExc_SystemError,
                    "koskari eval: no interpreter frame during "
                    "instruction fetch");
    return;
  }

  KosCode *rep_code = KosEvalState_CODE(es);
  if (! rep_code)
    rep_code = KosDynFrame_CODE(rep_pf);
  uint32_t rep_pc = KosEvalState_PC(es);

  if (! rep_code) {
    PyErr_Format(PyExc_SystemError,
                 "koskari eval: frame has no bytecode (pc=%u)",
                 (unsigned) rep_pc);
    return;
  }

  uint32_t rep_len = KosCode_CODE_LEN(rep_code);

  if (rep_pc >= rep_len) {
    PyErr_Format(PyExc_SystemError,
                  "koskari eval: program counter out of bounds "
                  "(pc=%u, len=%u)",
                  (unsigned) rep_pc,
                  (unsigned) rep_len);
    return;
  }

  PyErr_SetString(PyExc_SystemError,
                  "koskari eval: instruction fetch failed");
}


/**
  Dispatch prelude: next PC and acting PC live only on es (no heap write).
  Code from es->code first (hot cache); if NULL, KosDynFrame_CODE (e.g. tail
  reset cleared heap slot before es->code was updated).  On OOB, goto on_bad.
*/

/* GIL poll: stack locals in dispatch loops only (not KosContext / KosEvalState). */
#define KOS_EVAL_GIL_POLL           1024u

// 5 milliseconds (5,000,000 nanoseconds)
#define KOS_EVAL_GIL_POLL_INTERVAL_NS    5000000ULL


static inline uint64_t kos_eval_gil_poll_now_ns(void) {
  struct timespec ts;

  if (unlikely(clock_gettime(CLOCK_MONOTONIC, &ts) != 0))
    return 0;
  return (uint64_t) ts.tv_sec * 1000000000ULL + (uint64_t) ts.tv_nsec;
}



/* Internal-invariant safety net. See KOS_EVAL_SAFETYNET in hints.h.
   Build with -DKOS_EVAL_SAFETYNET=1 to turn a would-be segfault from such an
   internal misfire into a clean diagnostic while tracking down the cause. */
#if KOS_EVAL_SAFETYNET
/* es->code can be transiently NULL only through an internal bug (e.g. a tail
   reset that failed to retarget it); guard the fetch against dereferencing it. */
# define KOS_EVAL_SAFETYNET_NULLCODE(code, on_bad)                         \
    do { if (unlikely(! (code))) goto on_bad; } while (0)
#else
# define KOS_EVAL_SAFETYNET_NULLCODE(code, on_bad)  ((void) 0)
#endif

/* No per-opcode PC bounds check: every code object ends in a THEEND barrier
   (appended in KosCode_New), and jump targets are validated in-bounds, so
   straight-line dispatch always hits a terminal RETURN or THEEND before pc
   could reach code_len -- arr[pc] is provably valid. THEEND faults exactly as
   an off-the-end fetch would. The es->code null check is an internal-invariant
   guard, gated behind KOS_EVAL_SAFETYNET (off by default). */
#define KOS_EVAL_PREPARE_NEXT_INSN(es, insn_lvalue, on_bad)                \
  do {                                                                     \
    KosCode *_kos_code = KosEvalState_CODE(es);                            \
    uint32_t _kos_pc = KosEvalState_PC(es);                                \
    KOS_EVAL_SAFETYNET_NULLCODE(_kos_code, on_bad);                        \
    KosOp _kos_insn = KosCode_CODE(_kos_code)[_kos_pc];                    \
    SAYF("[eval] pc=%u op=%s arg=%u\n",                                    \
         (unsigned) _kos_pc, kos_opcode_name(_kos_insn.op),                \
         (unsigned) _kos_insn.arg);                                        \
    (es)->acting_pc = _kos_pc;                                             \
    (es)->pc = _kos_pc + 1u;                                               \
    (insn_lvalue) = _kos_insn;                                             \
  } while (0)


/**
   Opcode loop for GCC/Clang: computed goto, tail-dispatch on RUN, prologue
   via cg_prologue.  Must be a single function: label addresses and optable
   live here only.
*/
static inline KosOpResult koskari_eval_dispatch_computed_goto(KosEvalState *es) {

  #define OP(name, ...)  &&cg_TARGET_##name,
  static const void *const optable[] = {
    #include "opcodes.incl"
  };
  #undef OP

  KosOp insn;
  KosOpResult op_res;
  uint32_t gil_poll_ops = 0;
  uint64_t gil_poll_last_ns = kos_eval_gil_poll_now_ns();

  goto cg_prologue;

cg_prologue:
  /* es live fields via Init, kos_dyn_* retarget after chain surgery, freeze at
     cleanup; chain errors propagate without rebuild. */

  #if KOS_STACK_TRACE
  {
    KosStackBlock **_kos_tr = KosEvalState_BLOCK(es);
    trace_stack("  after ",
                (_kos_tr && *_kos_tr) ? *_kos_tr : NULL);
  }
  #endif

  /* Pending PyErr from a suspended transfer thunk (e.g. RAISE in a wind
     before-thunk) must run abandon + try_local before draining the plan;
     otherwise the wind EXTENT_ACTION can complete while PyErr is still set. */
  {
    KosOpResult pend = eval_phase_pending_exception(es);
    if (pend.kind != KOS_RES_NORMAL) {
      if (pend.kind == KOS_RES_REPROLOGUE) goto cg_prologue;
      return pend;
    }
  }

  {
    KosOpResult xfer = eval_step_drain_transfer_plan(es);
    if (xfer.kind != KOS_RES_NORMAL) {
      if (xfer.kind == KOS_RES_REPROLOGUE) goto cg_prologue;
      return xfer;
    }
  }

  if (KosEvalState_FRAME(es)
      && KosDynFrame_NATIVE_HANDLER(KosEvalState_FRAME(es)) != NULL) {
    KosOpResult ns = eval_step_native_iteration(es);
    if (ns.kind == KOS_RES_REPROLOGUE) goto cg_prologue;
    return ns;
  }

  #if KOS_STACK_TRACE
  {
    KosStackBlock **_kos_tr = KosEvalState_BLOCK(es);
    trace_stack("  before",
                (_kos_tr && *_kos_tr) ? *_kos_tr : NULL);
  }
  #endif

  goto cg_poll;

cg_poll:
  {
    gil_poll_ops = 0;
    if (koskari_gil_drop_requested()) {
      uint64_t now_ns = kos_eval_gil_poll_now_ns();
      if (gil_poll_last_ns && now_ns &&
          (now_ns - gil_poll_last_ns > KOS_EVAL_GIL_POLL_INTERVAL_NS)) {
        Py_BEGIN_ALLOW_THREADS
        gil_poll_last_ns = now_ns;
        Py_END_ALLOW_THREADS
      }
    }
  }

  PyErr_CheckSignals();
  if (unlikely(PyErr_Occurred())) {
    KosOpResult pend = eval_phase_pending_exception(es);
    if (pend.kind != KOS_RES_NORMAL) {
      if (pend.kind == KOS_RES_REPROLOGUE) goto cg_prologue;
      return pend;
    }
  }

  KOS_EVAL_PREPARE_NEXT_INSN(es, insn, cg_pc_bounds_error);
  goto *optable[insn.op];

  #define OP(name, ...)                                               \
    cg_TARGET_##name:                                                 \
      KOS_TRACE_OP_WRAP(name, op_res = op_##name(es, &insn));         \
      if (likely(op_res.kind == KOS_RES_NORMAL)) {                    \
        if (unlikely((++gil_poll_ops > KOS_EVAL_GIL_POLL)))           \
          goto cg_poll;                                               \
        KOS_EVAL_PREPARE_NEXT_INSN(es, insn, cg_pc_bounds_error);     \
        goto *optable[insn.op];                                       \
      }                                                               \
      goto cg_handle_op_result;

  #include "opcodes.incl"
  #undef OP

#if KOS_EVAL_SAFETYNET
cg_pc_bounds_error:
  kos_eval_report_pc_bounds_error(es);
  return EVAL_FATAL();
#endif

cg_handle_op_result: {
  KosEvalState_PythonResumeSet(es, NULL, KOS_RESUME_NONE);
  KosOpResult r = apply_kos_op_result(es, op_res);
  if (r.kind == KOS_RES_DISPATCH) {
    /* Plain frame-to-frame return: live regs already retargeted, no
       pending PyErr / plan / native handler.  Skip the prologue and go
       straight back to instruction fetch; the poll counter still bounds
       signal-check latency via cg_poll. */
    if (unlikely(++gil_poll_ops > KOS_EVAL_GIL_POLL))
      goto cg_poll;
    KOS_EVAL_PREPARE_NEXT_INSN(es, insn, cg_pc_bounds_error);
    goto *optable[insn.op];
  }
  if (r.kind == KOS_RES_REPROLOGUE) goto cg_prologue;
  return r;
}

  /* Unreachable */
  return EVAL_FATAL();
}


#undef KOS_EVAL_PREPARE_NEXT_INSN


KosOpResult koskari_eval(
    KosContext *ctx,
    PyObject *send_val,
    PyObject *throw_typ,
    PyObject *throw_exc) {

  KosOpResult eval_ret = EVAL_RAISE();
  PyObject *host_exc_t = NULL, *host_exc_v = NULL, *host_exc_tb = NULL;

  if (! ctx) {
    PyErr_SetString(PyExc_RuntimeError, "NULL context for koskari_eval");
    return EVAL_RAISE();
  }

  /* Swap in this context's handler-visible exception state (sys.exc_info),
     saving the host's. Contexts interleaved on one thread must not see
     each other's handled exception; the reverse swap runs at cleanup. */
  PyErr_GetExcInfo(&host_exc_t, &host_exc_v, &host_exc_tb);
  if (host_exc_v || KosContext_EXC_INFO_VALUE(ctx)) {
    PyErr_SetExcInfo(KosContext_EXC_INFO_TYPE(ctx),
                     KosContext_EXC_INFO_VALUE(ctx),
                     KosContext_EXC_INFO_TB(ctx));
    KosContext_EXC_INFO_TYPE(ctx) = NULL;
    KosContext_EXC_INFO_VALUE(ctx) = NULL;
    KosContext_EXC_INFO_TB(ctx) = NULL;
  }

  KosEvalState es;
  KosEvalState_Init(&es, ctx);
  KosEvalState_ThawFromContext(&es);
  if (KosEvalState_DYN_TOP(&es))
    kos_dyn_rebuild_live_from_chain(&es);

  /* On fresh entry, verify that FAST-slot code runs in a capable namespace.
     Resumes are safe: the namespace was already validated on first entry. */
  if (KosCode_USES_FAST(KosEvalState_CODE(&es))) {
    PyObject *ns = KosEvalState_NAMESPACE(&es);
    if (! KosNamespace_SupportsFast(ns)) {
      PyErr_Format(PyExc_RuntimeError,
                   "code uses fast slots but namespace does not support"
                   " them: %R", ns);
      eval_ret = EVAL_RAISE();
      goto koskari_eval_cleanup;
    }
  }

  KosContext_FLAGS(ctx) |= KOS_CTX_RUNNING;

  KosEvalState_EnterDomainScope(&es);
  kos_tls_push_active_eval(&es);

  /* Phase 1: entry/revive normalization */
  KosEvalMode mode = KOS_MODE_NORMAL;
  {
    KosOpResult pe = eval_phase_entry(&es, send_val, throw_typ, throw_exc,
                                      &mode);
    if (pe.kind != KOS_RES_NORMAL && pe.kind != KOS_RES_REPROLOGUE) {
      eval_ret = pe;
      goto koskari_eval_cleanup;
    }
  }

  /* Phase 2: transfer-plan synchronization (only when mode warrants it) */
  if (mode == KOS_MODE_NORMAL || mode == KOS_MODE_TRANSFER) {
    if (unlikely(KosEvalState_PLAN(&es))) {
      KosOpResult pt = eval_phase_transfer(&es);
      if (pt.kind != KOS_RES_NORMAL && pt.kind != KOS_RES_REPROLOGUE) {
        eval_ret = pt;
        goto koskari_eval_cleanup;
      }
    }
  }

  /* Clear resume kind once before loop; post_opcode clears it when handling
     non-zero results so the hot path (tail-dispatch) never touches it. */
  KosEvalState_PythonResumeSet(&es, NULL, KOS_RESUME_NONE);
  eval_ret = koskari_eval_dispatch_computed_goto(&es);

koskari_eval_cleanup:
  kos_tls_pop_active_eval();
  KosEvalState_FlushLiveRegsToFrame(&es);
  KosEvalState_FreezeToContext(&es);
  KosContext_FLAGS(ctx) &= ~KOS_CTX_RUNNING;
  KosEvalState_LeaveDomainScope(&es);

  /* Save this context's handler-visible exception state and restore the
     host's (the reverse of the entry swap). */
  {
    PyObject *ct = NULL, *cv = NULL, *ctb = NULL;
    PyErr_GetExcInfo(&ct, &cv, &ctb);
    if (cv || host_exc_v) {
      KosContext_EXC_INFO_TYPE(ctx) = ct;
      KosContext_EXC_INFO_VALUE(ctx) = cv;
      KosContext_EXC_INFO_TB(ctx) = ctb;
      PyErr_SetExcInfo(host_exc_t, host_exc_v, host_exc_tb);
    } else {
      Py_XDECREF(ct);
      Py_XDECREF(cv);
      Py_XDECREF(ctb);
      Py_XDECREF(host_exc_t);
      Py_XDECREF(host_exc_v);
      Py_XDECREF(host_exc_tb);
    }
  }
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

  /* Initialize the import fromlist sentinel used by kos_import_module. */
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
  STR_CONST(str_aclose, "aclose");
  STR_CONST(str_aenter, "__aenter__");
  STR_CONST(str_aexit, "__aexit__");
  STR_CONST(str_args, "args");
  STR_CONST(str_asend, "asend");
  STR_CONST(str_asyncio_future_blocking, "_asyncio_future_blocking");
  STR_CONST(str_athrow, "athrow");
  STR_CONST(str_await, "__await__");
  STR_CONST(str_class, "__class__");
  STR_CONST(str_close, "close");
  STR_CONST(str_continuation, "continuation");
  STR_CONST(str_enter, "__enter__");
  STR_CONST(str_exit, "__exit__");
  STR_CONST(str_getattr, "__getattr__");
  STR_CONST(str_name, "__name__");
  STR_CONST(str_package, "__package__");
  STR_CONST(str_send, "send");
  STR_CONST(str_target, "target");
  STR_CONST(str_throw, "throw");
  // STR_CONST(str_getattribute, "__getattribute__");

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

  Py_CLEAR(str_aclose);
  Py_CLEAR(str_aenter);
  Py_CLEAR(str_aexit);
  Py_CLEAR(str_args);
  Py_CLEAR(str_asend);
  Py_CLEAR(str_asyncio_future_blocking);
  Py_CLEAR(str_athrow);
  Py_CLEAR(str_await);
  Py_CLEAR(str_class);
  Py_CLEAR(str_close);
  Py_CLEAR(str_continuation);
  Py_CLEAR(str_enter);
  Py_CLEAR(str_exit);
  Py_CLEAR(str_getattr);
  Py_CLEAR(str_name);
  Py_CLEAR(str_package);
  Py_CLEAR(str_send);
  Py_CLEAR(str_target);
  Py_CLEAR(str_throw);

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
