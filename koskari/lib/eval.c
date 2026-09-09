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
      handle_continuation_apply.  The exception is cleared and a
      cc-unwind goal is armed: it leaves every extent in the current
      context down to its root (running their exits with no active
      exception) and then delivers the exception as a boundary raise.
      The eval loop exits with KOS_RES_RAISE, propagating the exception
      upward through the Python async call chain to the owning eval
      context.

   :author: Christopher O'Brien <obriencj@preoccupied.net>
   :license: GNU General Public License v3
*/


#include "lib.h"
#include "xfer_goal.h"
#include "hints.h"
#include "debug.h"
#include "operator.h"
#include "tracing.h"
#include "matcher.h"

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

static KosOpResult eval_event_yield(
    KosEvalState *es,
    KosDynGen *gen,
    PyObject *val);

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


int KosEval_IsFuture(PyObject *val) {
  return kos_isfuture(val);
}
#include "eval/eval_cache.c"


/**
   On exception: if we are inside a Koskari async gen step (NEXT or ITER_NEXT),
   detach the gen frame, close the gen, and clear NEXT / FOREACH node state.
*/
static void clear_kos_agen_iter_node_on_error(KosEvalState *es) {

  KosDynFrame *cf;
  KosDynNode *top;
  KosDynNode *node;
  KosPyAsyncGen *agen;

  cf = KosEvalState_FRAME(es);
  top = KosEvalState_DYN_TOP(es);

  if (! cf || ! top || ! KosDynNode_IS_FRAME(top))
    return;
  {
    KosDynNode *par = KosDynNode_PARENT(top);
    if (! par || ! KosDynNode_IS_GEN(par))
      return;
  }

  node = kos_dyn_find_iter_node(es);
  if (! node)
    return;

  agen = (KosPyAsyncGen *) kos_dyn_iter_node_iter_obj(node);
  if (! agen)
    return;

  {
    KosDynFrame *detached_frame = NULL;
    if (kos_dyn_detach_top_frame(es, &detached_frame) == 0 && detached_frame) {
      KosIterSlots *islots;
      if (KosIter_CheckSlots((PyObject *) agen, &islots))
        islots->ki_finish((PyObject *) agen, 1);
      kos_dynframe_free(detached_frame);
    }
  }

  kos_dyn_iter_node_abort(es, node);
}


/* === KosIter provider step drive === */


/**
   One iteration request against a iteration node's armed record: the
   single site API for "give me the next element of this thing". A
   KosIter provider drives through its suite (graft-capable, mode
   updates recorded); a sync iterator steps through tp_iternext; a
   python async iterator through its eager-stepped __anext__. send_val
   is borrowed (protocol send); *fast_out receives a new reference on
   DELIVERED/EXHAUSTED, and the awaitable payload on EXIT.
*/
KosIterRequestStatus KosIterRequest_Next(
    KosEvalState *es,
    KosDynNode *node,
    PyObject *send_val,
    PyObject **fast_out) {

  KosIterRequest *req = kos_dyn_iter_request(node);
  PyObject *iter = req->iter;
  KosIterSlots *islots = req->islots;

#if KOS_EVAL_SAFETYNET
  {
    KosIterSlots *fresh = NULL;
    int is_provider = KosIter_CheckSlots(iter, &fresh);
    if ((is_provider ? fresh : NULL) != islots) {
      PyErr_SetString(PyExc_SystemError,
                      "iteration request suite does not match its iterator");
      return KOS_IREQ_ERROR;
    }
  }
#endif

  if (islots) {
    PyObject *out = NULL;

    /* Armed before the step: an INSTALLED outcome delivers through the
       iteration node, and deliveries can elect it mid-step. */
    kos_dyn_iter_request_set_mode(node, KOS_IREQ_INSTALLED);

    switch (islots->ki_step(es, iter, send_val, &out)) {
      case KOS_ITER_VALUE:
        kos_dyn_iter_request_set_mode(node, KOS_IREQ_IDLE);
        *fast_out = out;
        return KOS_IREQ_DELIVERED;

      case KOS_ITER_EXHAUSTED:
        kos_dyn_iter_request_set_mode(node, KOS_IREQ_IDLE);
        *fast_out = out;
        return KOS_IREQ_EXHAUSTED;

      case KOS_ITER_INSTALLED:
        return KOS_IREQ_CONTINUES;

      case KOS_ITER_INSTALLED_VALUE:
        kos_dyn_iter_request_set_mode(node, KOS_IREQ_GRAFT_VALUE);
        return KOS_IREQ_CONTINUES;

      case KOS_ITER_ERROR:
      default:
        kos_dyn_iter_request_set_mode(node, KOS_IREQ_IDLE);
        /* A StopIteration ending a provider step is that provider's
           exhaustion -- builtins.map truncation parity for a mapped
           function's StopIteration included. Converting here keeps
           the semantics the raise walk applies at armed nodes, which
           the mode reset above would otherwise bypass. */
        if (PyErr_ExceptionMatches(PyExc_StopIteration)) {
          PyObject *payload = NULL;
          if (PyGen_FetchStopIterationValue(&payload) < 0)
            return KOS_IREQ_ERROR;
          kos_dyn_iter_request_finish(node, 1);
          *fast_out = payload;
          return KOS_IREQ_EXHAUSTED;
        }
        return KOS_IREQ_ERROR;
    }
  }

  {
    PyObject *value = NULL;
    PyObject *_await = NULL;

    switch (op_get_next_from_iter(es, &_await, iter, &value)) {
      case GET_NEXT_VALUE:
        *fast_out = value;
        return KOS_IREQ_DELIVERED;

      case GET_NEXT_EXHAUSTED:
        *fast_out = NULL;
        return KOS_IREQ_EXHAUSTED;

      case GET_NEXT_SUSPEND:
        /* Parked python async step: the awaited completion IS the
           element, delivered through the iteration node at revive. */
        kos_dyn_iter_request_set_mode(node, KOS_IREQ_AWAITING_PY);
        *fast_out = _await;
        return KOS_IREQ_EXIT;

      case GET_NEXT_ERROR:
      default:
        return KOS_IREQ_ERROR;
    }
  }
}


/**
   Deliver a typed event into a iteration node's request: the one entry
   the eval's delivery points call; the site completes through its
   delivery tail. value is consumed.

   :return: EVAL_NORMAL when the site completed and eval continues,
            EVAL_RAISE with PyErr set, EVAL_FATAL on delivery failure
*/
KosOpResult KosIterRequest_Deliver(
    KosEvalState *es,
    KosDynNode *node,
    KosIterWakeEvent ev,
    PyObject *value) {

  KosIterRequest *req = kos_dyn_iter_request(node);

  if (! req) {
    Py_XDECREF(value);
    PyErr_SetString(PyExc_RuntimeError,
                    "iter request delivery to a non-iteration node");
    return EVAL_RAISE();
  }

  switch (ev) {
    case KOS_IWAKE_VALUE:
      if (kos_dyn_iter_node_deliver_yield(es, node, value) < 0)
        return EVAL_FATAL();
      return EVAL_NORMAL();

    case KOS_IWAKE_END: {
      /* A generator completing at the site's iteration node arrives with its
         frame chain still installed; unwind it before delivering. */
      KosDynNode *child = (KosDynNode *) KosEvalState_FRAME(es);
      if (child && KosEvalState_DYN_TOP(es) == child) {
        KosDynNode *parent = KosDynNode_PARENT(child);
        if (parent && KosDynNode_IS_GEN(parent)) {
          if (kos_dyn_teardown_gen_exhausted(es) < 0) {
            Py_XDECREF(value);
            return EVAL_FATAL();
          }
        }
      }

      if (kos_dyn_iter_node_deliver_exhausted(es, node, value) < 0) {
        /* A re-emitting site (1-arg NEXT) completes by raising; a
           delivery failure without an exception is fatal. */
        if (KosDynNode_IS_YIELD_FROM(node) || PyErr_Occurred())
          return EVAL_RAISE();
        return EVAL_FATAL();
      }
      return EVAL_NORMAL();
    }

    case KOS_IWAKE_EXCEPTION:
    default:
      /* The raise walk owns exception routing until the wake layer
         lands; nothing calls this yet. */
      Py_XDECREF(value);
      PyErr_SetString(PyExc_SystemError,
                      "iter request exception delivery not yet routed");
      return EVAL_RAISE();
  }
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
        PyObject *throw_exc,
        KosObligation *out_return) {

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
  /* Take transfers the stored inject's reference to us (owned == 1);
     the send_val fallback is borrowed from the caller. */
  int owned = KosEvalState_PythonResumeTake(es, &to_push, &resume_kind_unused);
  (void) resume_kind_unused;
  if (to_push == NULL && send_val != NULL) {
    to_push = send_val;
  }
  if (to_push != NULL && KosEvalState_FRAME(es)) {
    KosDynNode *node = kos_dyn_find_iter_node(es);
    if (node && kos_dyn_iter_node_awaiting_py(node)) {
      /* The pending await belongs to the armed iteration node: a python
         async step's awaited result IS the element. */
      KosOpResult dr;

      if (! owned)
        Py_INCREF(to_push);
      dr = KosIterRequest_Deliver(es, node, KOS_IWAKE_VALUE, to_push);
      if (dr.kind == KOS_RES_FATAL) {
        if (! PyErr_Occurred())
          PyErr_SetString(PyExc_RuntimeError,
                          "node delivery failed at revive");
        return EVAL_RAISE();
      }
      if (dr.kind != KOS_RES_NORMAL)
        return dr;
      return EVAL_NORMAL();
    }

    if (KosDynFrame_RESUME_FN(KosEvalState_FRAME(es))) {
      /* the frame is waiting on a resume: the awaited value is its
         delivery, staged for the entry's delivery step */
      if (! owned)
        Py_INCREF(to_push);
      KosEvalState_StageDelivery(es, to_push);
      owned = 0;
    } else if (KosEvalState_StackPushRef(es, to_push) < 0) {
      if (owned)
        Py_DECREF(to_push);
      return EVAL_RAISE();
    }
  }
  if (owned)
    Py_DECREF(to_push);
  {
    /* the awaited value may complete a node's pending leave; a return
       parked on that node is handed back for the entry phase to resume */
    KosOpResult sr = kos_dyn_settle_top(es, out_return);
    if (sr.kind != KOS_RES_NORMAL)
      return sr;
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

  /* A frame waiting on a resume (a yield-from delegation in flight)
     took no placeholder at install and takes its delivery through the
     pending slot; a throw raises at the frame, and the raise walk
     disarms the resume when it unwinds the delegation's node. */
  if (KosEvalState_FRAME(es)
      && KosDynFrame_RESUME_FN(KosEvalState_FRAME(es))) {
    if (throw_exc != NULL) {
      PyErr_SetObject(throw_typ, throw_exc);
      return KOS_MODE_EXCEPTION;
    }
    to_push = (send_val != NULL) ? send_val : Py_None;
    Py_INCREF(to_push);
    KosEvalState_StageDelivery(es, to_push);
    return KOS_MODE_NORMAL;
  }
  if (throw_exc != NULL) {
    if (KosEvalState_FRAME(es))
      Py_XDECREF(KosEvalState_StackPopRef(es));
    PyErr_SetObject(throw_typ, throw_exc);
    return KOS_MODE_EXCEPTION;
  }
  if (KosEvalState_FRAME(es)) {
    /* pop_ref hands back an owned reference: releasing it here is
       what pays off the placeholder push -- a drive that staged the
       sent value ahead of us leaves that value on top, and dropping
       the reference silently leaked one per send. */
    Py_XDECREF(KosEvalState_StackPopRef(es));
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

  /* Carry forward every previously recorded frame. Crossed entries
     were popped by the raise walk; the rest were recorded at the
     escape of an earlier eval, whose context died with that escape
     (see kos_eval_prepare_boundary_raise). Either way none of them
     can still be on this context's live chain, so appending the live
     chain below cannot duplicate them. */
  {
    PyObject *prior = PyObject_GetAttrString(exc_val,
                                             "__koskari_dynchain__");
    if (! prior) {
      PyErr_Clear();
    } else {
      if (PyList_Check(prior)) {
        for (Py_ssize_t i = 0; i < PyList_GET_SIZE(prior); i++) {
          PyObject *meta = PyList_GET_ITEM(prior, i);
          if (PyDict_Check(meta))
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
   kos_invoke_continuation to arm a transfer goal.

   Foreign-token path: the continuation targets a different eval context.
   Fetch and clear PyErr and arm a cc-unwind goal that leaves every local
   extent down to the context's root and then delivers the exception as
   a boundary raise, so the eval loop exits with KOS_RES_RAISE and the
   exception propagates upward to the owning context.

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
    kos_xfer_goal_abandon_all(es);
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

  /* Foreign-token path: leave this context down to its root, then
     deliver the signal as a boundary raise. A generator context's root
     is its GEN, which is the driver boundary; anywhere else the root is
     the empty chain. */
  {
    KosDynNode *seam = KosEvalState_DYN_TOP(es);
    KosXferGoal *goal;

    while (seam && KosDynNode_PARENT(seam))
      seam = KosDynNode_PARENT(seam);
    if (seam && ! KosDynNode_IS_GEN(seam))
      seam = NULL;

    goal = kos_xfer_goal_new(NULL, seam, val, 1, (uint32_t) -1, NULL, 1);
    Py_DECREF(typ);
    Py_DECREF(val);
    if (! goal)
      return EVAL_RAISE();
    kos_xfer_goal_arm(es, goal);
  }
  return EVAL_REPROLOGUE();
}


/**
   Unified exception ingress (the RAISE hub).  All PyErr / KOS_RES_RAISE
   ingress converges here: post-opcode results (apply_kos_op_result),
   prologue and signal-poll pending PyErr (eval_phase_pending_exception),
   revive-throw (eval_phase_entry KOS_MODE_EXCEPTION), and transfer goal
   step failures (kos_eval_drain_goal).  One deliberate non-ingress
   carve-out lives in kos_eval_drain_goal: a foreign cc-unwind's boundary
   raise (signal delivery, not a failed transfer; must not see try_local).

   Ordering contract:
     1. guard        - no PyErr: SystemError + prepare_boundary_raise,
                       EVAL_RAISE
     2. cc test      - PyErr_ExceptionMatches(KoskariContinuationApply),
                       exactly once
     3. nest abandon - kos_xfer_goal_abandon_all when a goal is armed
                       AND the exception is not a continuation signal
     4. cc dispatch  - continuation signal: handle_continuation_apply
                       (own token: invoke; foreign: cc-unwind goal);
                       REPROLOGUE re-enters the loop with a goal armed
     5. agen clear   - clear_kos_agen_iter_node_on_error before the
                       exception can escape an async-gen node step
     6. try_local    - kos_dyn_unwind_raise walks TRY / FINALLY /
                       WITH / unwind extents
     7. outcome map  - REPROLOGUE: handler applied, restart prologue.
                       AWAIT: async unwind started; tag KOS_SUSP_AWAIT,
                       mark suspended, propagate awaitable (caller owns
                       tl.val).
                       RAISE: propagate; kos_eval_prepare_boundary_raise
                       already ran inside try_local - never call it again
                       (archive boundary-control-reemit.md invariant 7.4).
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

  /* The raise supersedes any staged delivery: the walk propagates the
     exception through the resume instead, and the frame it was staged for may
     be popped or replaced by a transfer. Every ingress into the raise
     machinery passes here, so this is the one place the slot drops. */
  Py_CLEAR(KosEvalState_PENDING_VALUE(es));

  int is_cc_apply = (KoskariContinuationApply
                     && PyErr_ExceptionMatches(KoskariContinuationApply));

  if (! is_cc_apply && KosEvalState_GOAL(es))
    kos_xfer_goal_abandon_all(es);

  if (is_cc_apply)
    return handle_continuation_apply(es);

  clear_kos_agen_iter_node_on_error(es);

  {
    KosOpResult tl = kos_dyn_unwind_raise(es);
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

  PyObject *cid = KosOp_OBJ(insn);
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
   Print current segment address/id and length when KOS_ENABLE_DEBUG;
   also print top two slot values as integers when PyLong.
*/
static void trace_stack(const char *prefix, KosSegment *stack) {
  SAYF("[eval] %s stack=%p", prefix, (void *) stack);
  if (stack) {
    size_t n = KosSegment_LEN(stack);
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
   Resolve one protocol method of mgr the way Python resolves a special
   method: on the type, never on the instance. A plain function
   (KosFunction or PyFunction) is returned unbound with *self_bind set,
   so the caller passes mgr as the first argument and no bound-method
   object is built; any other descriptor is bound through its __get__;
   a plain class attribute is returned as is.

   :return: 1 found (*out is a new ref), 0 absent with no error set,
     -1 on error
*/
static int resolve_special_method(
    PyObject *mgr,
    PyObject *name,
    PyObject **out,
    int *self_bind) {

  PyTypeObject *tp = Py_TYPE(mgr);
  PyObject *descr = PyType_Lookup(tp, name);   /* borrowed */

  *self_bind = 0;
  if (! descr)
    return 0;

  if (is_plain_instance_method(descr)) {
    Py_INCREF(descr);
    *out = descr;
    *self_bind = 1;
    return 1;
  }
  if (Py_TYPE(descr)->tp_descr_get) {
    PyObject *bound = Py_TYPE(descr)->tp_descr_get(descr, mgr, (PyObject *) tp);
    if (! bound)
      return -1;
    *out = bound;
    return 1;
  }
  Py_INCREF(descr);
  *out = descr;
  return 1;
}


/**
   Resolve enter/exit on mgr. Prefer __aenter__/__aexit__ if present (async
   protocol), else __enter__/__exit__ (sync). Returns new refs in *out_enter
   and *out_exit, and the KOS_WITH_SELF_* word for the pair in *out_self.

   :return: 0 on success, -1 on error (TypeError when mgr supports
     neither protocol)
*/
static int resolve_context_enter_exit(
    PyObject *mgr,
    PyObject **out_enter,
    PyObject **out_exit,
    uint8_t *out_self) {

  PyObject *enter = NULL;
  PyObject *exit = NULL;
  int enter_self = 0;
  int exit_self = 0;
  int r;

  r = resolve_special_method(mgr, str_aenter, &enter, &enter_self);
  if (r < 0)
    return -1;
  if (r > 0) {
    r = resolve_special_method(mgr, str_aexit, &exit, &exit_self);
    if (r < 0) {
      Py_DECREF(enter);
      return -1;
    }
    if (r == 0)
      Py_CLEAR(enter);
  }

  if (! enter) {
    r = resolve_special_method(mgr, str_enter, &enter, &enter_self);
    if (r < 0)
      return -1;
    if (r == 0) {
      PyErr_Format(PyExc_TypeError,
                   "'%.200s' object does not support the context manager"
                   " protocol", Py_TYPE(mgr)->tp_name);
      return -1;
    }
    r = resolve_special_method(mgr, str_exit, &exit, &exit_self);
    if (r < 0) {
      Py_DECREF(enter);
      return -1;
    }
    if (r == 0) {
      Py_DECREF(enter);
      PyErr_Format(PyExc_TypeError,
                   "'%.200s' object does not support the context manager"
                   " protocol (missed __exit__ method)",
                   Py_TYPE(mgr)->tp_name);
      return -1;
    }
  }

  *out_enter = enter;
  *out_exit = exit;
  *out_self = (uint8_t) ((enter_self ? KOS_WITH_SELF_ENTER : 0)
                         | (exit_self ? KOS_WITH_SELF_EXIT : 0));
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
    KosContext *active = kos_ctx_current();

    ctx->domain = NULL;
    ctx->param_refs = NULL;

    if (active) {
      /* fork: private copy of the creating context's shadow table, so
         the parameterize extents applied at this moment are captured.
         The cells are shared by pointer. The root scope is not copied;
         it is reached by resolution through the domain. */
      PyObject *cells = KosContext_PARAM_REFS(active);
      if (cells && PyDict_GET_SIZE(cells) > 0) {
        ctx->param_refs = PyDict_Copy(cells);
        if (! ctx->param_refs) {
          Py_DECREF(KosContext_TOKEN(ctx));
          KosContext_TOKEN(ctx) = NULL;
          context_release(ctx);
          return NULL;
        }
      }
      ctx->domain = KosContext_DOMAIN(active);
      Py_XINCREF(ctx->domain);

    } else {
      /* no active context: a root context under an explicit
         `with Domain():` scope anchors to that domain, and so resolves
         against its root scope; otherwise a fresh domain is minted --
         every context has a domain, created as needed */
      PyObject *domain = kos_domain_current();
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
  }

  KosContext_DYN_TOP(ctx) = (KosDynNode *) top;
  KosContext_FRAME_TOP(ctx) = top;

  KosContext_GOAL(ctx) = NULL;
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
    kos_dyn_resync_chain(&es);
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

  while (KosContext_GOAL(ctx)) {
    KosXferGoal *goal = KosContext_GOAL(ctx);
    KosContext_GOAL(ctx) = KosXferGoal_OUTER(goal);
    kos_xfer_goal_free(goal);
  }

  Py_CLEAR(KosContext_PY_AWAIT_ITER(ctx));
  Py_XDECREF(KosContext_PY_RESUME_INJECT(ctx));
  Py_XDECREF(KosContext_INJECT_TYPE(ctx));
  KosContext_INJECT_TYPE(ctx) = NULL;
  Py_XDECREF(KosContext_INJECT_VALUE(ctx));
  KosExc_Clear(KosContext_EXC_INFO(ctx));
  Py_XDECREF(KosContext_DOMAIN(ctx));
  KosContext_DOMAIN(ctx) = NULL;
  Py_XDECREF(KosContext_PARAM_REFS(ctx));
  KosContext_PARAM_REFS(ctx) = NULL;

  context_release(ctx);
}


/**
   Make a context reusable after a completion at its root: drop the
   finished frame chain and clear the completion residue. A context
   with an armed transfer goal is left untouched -- the caller frees
   it instead.
*/
int KosContext_Recycle(KosContext *ctx) {

  if (KosContext_GOAL(ctx))
    return -1;

  if (KosContext_DYN_TOP(ctx))
    kos_dynchain_free(KosContext_DYN_TOP(ctx), NULL);
  KosContext_DYN_TOP(ctx) = NULL;
  KosContext_FRAME_TOP(ctx) = NULL;

  KosContext_FLAGS(ctx) &= ~(KOS_CTX_DEAD | KOS_CTX_RUNNING);
  KosContext_SUSPENSION_EVENT(ctx) = KOS_SUSP_NONE;
  Py_CLEAR(KosContext_PY_AWAIT_ITER(ctx));
  Py_CLEAR(KosContext_PY_RESUME_INJECT(ctx));
  KosContext_PY_RESUME_KIND(ctx) = 0;

  return 0;
}


int KosContext_Traverse(
    KosContext *ctx,
    visitproc visit,
    void *arg) {

  KosDynNode *dyn_top;
  KosXferGoal *goal;
  KosEvalState *active_es;
  int res;

  if (! ctx)
    return 0;

  Py_VISIT(KosContext_TOKEN(ctx));
  Py_VISIT(KosContext_DOMAIN(ctx));
  Py_VISIT(KosContext_PARAM_REFS(ctx));
  Py_VISIT(ctx->py_await_iter);
  Py_VISIT(KosContext_PY_RESUME_INJECT(ctx));
  Py_VISIT(KosContext_INJECT_TYPE(ctx));
  Py_VISIT(KosContext_INJECT_VALUE(ctx));
  if (KosExc_Traverse(KosContext_EXC_INFO(ctx), visit, arg))
    return -1;

  /* between drives the chain and goal rest on the context; while a
     drive is running they have moved onto the active eval state */
  dyn_top = KosContext_DYN_TOP(ctx);
  goal = KosContext_GOAL(ctx);
  if (KosContext_FLAGS(ctx) & KOS_CTX_RUNNING) {
    active_es = kos_tls_active_eval_for_ctx(ctx);
    if (active_es) {
      Py_VISIT(KosEvalState_PENDING_VALUE(active_es));
      if (! dyn_top)
        dyn_top = KosEvalState_DYN_TOP(active_es);
      if (! goal)
        goal = KosEvalState_GOAL(active_es);
    }
  }

  res = kos_dynchain_traverse(dyn_top, NULL, visit, arg);
  if (res)
    return res;

  /* the active goal and every parked outer behind it */
  for (; goal; goal = KosXferGoal_OUTER(goal)) {
    res = kos_xfer_goal_traverse(goal, visit, arg);
    if (res)
      return res;
  }

  return 0;
}


/* === Frame namespace helpers (for VAR_* opcodes) === */


/**
   Deref name in the current frame namespace. New reference or NULL.
*/
static PyObject *frame_ns_deref(KosEvalState *es, PyObject *name) {
  PyObject *ns = KosEvalState_CurrentNamespace(es);

  if (KosNamespace_Check(ns))
    return KosNamespace_Resolve(ns, name);

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


/* === THEEND, BREAK, CONTINUE, YIELD, YIELD_FROM === */

/* THEEND: no runtime implementation; hardcoded error (not a "stub" to be implemented). */

/**
   THEEND: arg unused. End-of-code sentinel; has no runtime implementation.
   If execution reaches THEEND, raise a hardcoded unimplemented error.
*/


/* === NEXT, ITER_NEXT === */


/**
   Route a walk's answer into the evaluator: a RAISE enters the raise
   ingress so a local handler can take it, an AWAIT is tagged for the
   driver, and everything else passes. Applied once, at the site that
   consumes a walk's raw result; a result that has already been through
   here must not come back.
*/
static KosOpResult kos_eval_walk_outcome(KosEvalState *es, KosOpResult r) {
  if (r.kind == KOS_RES_RAISE) {
    KosOpResult pr;
    KOS_TRACE_POST_OP_WRAP(event_raise, pr = process_raise(es));
    return pr;
  }
  if (r.kind == KOS_RES_AWAIT)
    return eval_event_await(es, r.val);
  return r;
}


/**
   Route a return value to a live delimited-invoke bookmark: a DELIM
   carrying RETURN_FRAME + RETURN_PC (POP_PROMPT-mode push-graft or
   INPLACE-mode in-place invoke).  Shared by the POP_PROMPT opcode (the
   reset body completed normally) and the RETURN unwind walk (a return
   crossed the delimiting reset); both deliver val to the k-invocation
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

  {
    KosOpResult wr;
    KosDynNode *pending = NULL;
    if (KosDynDelim_RETURN_NODE(ret_delim) != NULL)
      wr = kos_dyn_unwind_to_node(es, KosDynDelim_RETURN_NODE(ret_delim),
                                  KOS_XFER_EVENT_SHIFT, &pending);
    else
      wr = kos_dyn_unwind_to_frame(es, KosDynDelim_RETURN_FRAME(ret_delim),
                                   KOS_XFER_EVENT_SHIFT, &pending);
    if (wr.kind == KOS_RES_REPROLOGUE || wr.kind == KOS_RES_AWAIT) {
      /* a leave went into a thunk or onto an awaitable; val parks on
         that node and re-enters this route once the node has finished */
      if (kos_dyn_ob_park_return(pending, val, KOS_PENDING_RETURN_DELIM,
                                 (KosDynNode *) ret_delim) < 0) {
        Py_XDECREF(wr.val);
        return EVAL_RAISE();
      }
      return wr;
    }
    if (wr.kind != KOS_RES_NORMAL) {
      Py_DECREF(val);
      return wr;
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
/* Every handler has the KosThreadFn prototype: insn is the handler's
   own KosOp (read through KosOp_ARG, KosOp_OBJ, KosOp_ROW, KosOp_SLOT),
   and the delivery
   argument is unused by an opcode.  The one shape lets an instruction's
   fn pointer reach any handler, and lets a handler be a musttail
   target.  Each handler is its own function, reached only through the
   instructions that name it. */
#define KOS_OP_HANDLER(name)                                            \
  static KOS_PRESERVE_NONE KosOpResult op_##name(                       \
      KosEvalState *es, void *insn, PyObject *kos_op_delivery_)

/* The cold variant names a large, rarely-run opcode; under threaded
   dispatch every handler is out of line, so it differs only in the
   explicit noinline. */
#define KOS_OP_HANDLER_COLD(name)                                       \
  static KOS_NOINLINE KOS_PRESERVE_NONE KosOpResult op_##name(         \
      KosEvalState *es, void *insn, PyObject *kos_op_delivery_)

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

/**
   The signal that ends an in-eval delivery (a control-returned hook, a
   flattened generator's yield landing on its consumer). The delivery
   left a plain bytecode frame current with nothing staged, no error,
   and no plan armed: DISPATCH, so the trampoline threads from es->pc
   without a prologue pass, and a handler that ran the delivery itself
   threads on directly. Anything else (a value staged for a resume, a
   codeless frame, a plan) is the prologue's to deliver.
*/
static inline KosOpResult kos_eval_delivery_done(KosEvalState *es) {
  KosDynFrame *frame = KosEvalState_FRAME(es);

  if (likely(frame
             && KosDynFrame_CODE(frame)
             && ! KosDynFrame_RESUME_FN(frame)
             && ! KosEvalState_PENDING_VALUE(es)
             && ! KosEvalState_GOAL(es)
             && ! PyErr_Occurred()))
    return EVAL_DISPATCH();
  return EVAL_REPROLOGUE();
}


#include "eval/eval_electors.c"

/* Cross-handler helpers defined inside handlers.incl after some of
   their users (the include follows opcodes.incl order): the shared
   OBJECT_NEXT step core lives in OBJECT_NEXT.c but VAR_NEXT / FAST_NEXT
   precede it. */
static KosOpResult object_next_step(
    KosEvalState *es,
    PyObject *iter,
    PyObject *default_val,
    uint16_t n);

/* The return arms op_RETURN threads through directly (defined with the
   return machinery below; the trampoline's own fast arm uses the same
   functions for every other producer of a RETURN signal). */
static inline int kos_return_is_plain(
    KosEvalState *es,
    KosDynNode **out_child,
    KosDynNode **out_parent);
KOS_OP_INLINE int kos_return_is_region(
    KosEvalState *es,
    KosDynNode **out_child,
    KosDynNode **out_parent,
    KosDynFrame **out_ft);
KOS_OP_INLINE KosOpResult eval_ng_plain_return(
    KosEvalState *es,
    KosDynNode *child,
    KosDynNode *parent,
    PyObject *val);
KOS_OP_INLINE KosOpResult eval_ng_region_return(
    KosEvalState *es,
    KosDynNode *child,
    KosDynNode *parent,
    KosDynFrame *ft,
    PyObject *val);
KOS_OP_INLINE int kos_return_is_resume(
    KosEvalState *es,
    KosDynNode **out_parent);
static int kos_return_take_resume(
    KosEvalState *es,
    KosDynNode *parent,
    PyObject *val,
    KosResumeFn *out_resume,
    void **out_state);


/**
   Finish a star-shaped call for the CALL_*STAR and TAIL_CALL_*STAR
   handlers. Pops kwd (opa & 2) and pos (opa & 1) from the stack, then
   the subject too when subject is NULL; otherwise subject is an owned
   reference the handler sourced itself. Steals subject either way.

   With attr_name NULL the subject is the callable. Otherwise the subject
   is the receiver: attr_name is resolved on it through cache and the
   receiver is prepended to the positional tuple when the resolution asks
   for it. pos is coerced to a tuple and kwd to a dict; a non-iterable
   or non-mapping operand raises TypeError naming its type. Returns the
   call result for RESULT_RELAY, or a raise.
*/
static KosOpResult kos_op_call_star(
    KosEvalState *es,
    PyObject *subject,
    PyObject *attr_name,
    KosCacheSlot *cache,
    unsigned opa,
    int tail) {

  PyObject *kwd = NULL;
  PyObject *pos = NULL;
  PyObject *callable = NULL;
  PyObject *args = NULL;
  KosOpResult res = EVAL_RAISE();

  if (opa & 2) {
    kwd = KosEvalState_StackPopRef(es);
    if (! kwd)
      goto done;
  }

  if (opa & 1) {
    pos = KosEvalState_StackPopRef(es);
    if (! pos)
      goto done;
  }

  if (! subject) {
    subject = KosEvalState_StackPopRef(es);
    if (! subject)
      goto done;
  }

  if (! pos) {
    pos = PyTuple_New(0);
    if (! pos)
      goto done;

  } else if (! PyTuple_CheckExact(pos)) {
    PyObject *fixed = PySequence_Tuple(pos);
    if (! fixed) {
      if (PyErr_ExceptionMatches(PyExc_TypeError)) {
        PyErr_Format(PyExc_TypeError,
                     "argument after *: must be an iterable, not %.100s",
                     Py_TYPE(pos)->tp_name);
      }
      goto done;
    }
    Py_SETREF(pos, fixed);
  }

  if (kwd && ! PyDict_Check(kwd)) {
    PyObject *fixed = PyDict_New();
    if (! fixed)
      goto done;
    if (PyDict_Update(fixed, kwd) < 0) {
      Py_DECREF(fixed);
      if (PyErr_ExceptionMatches(PyExc_TypeError)
          || PyErr_ExceptionMatches(PyExc_AttributeError)) {
        PyErr_Format(PyExc_TypeError,
                     "argument after **: must be a mapping, not %.100s",
                     Py_TYPE(kwd)->tp_name);
      }
      goto done;
    }
    Py_SETREF(kwd, fixed);
  }

  if (! attr_name) {
    callable = subject;
    subject = NULL;
    args = pos;
    pos = NULL;

  } else {
    int send_self = kos_resolve_named_attr(subject, attr_name, cache,
                                           &callable);
    if (send_self < 0)
      goto done;

    if (send_self) {
      args = pos;
      pos = NULL;

    } else {
      Py_ssize_t n = PyTuple_GET_SIZE(pos);
      args = PyTuple_New(n + 1);
      if (! args)
        goto done;

      Py_INCREF(subject);
      PyTuple_SET_ITEM(args, 0, subject);
      for (Py_ssize_t i = 0; i < n; i++) {
        PyObject *item = PyTuple_GET_ITEM(pos, i);
        Py_INCREF(item);
        PyTuple_SET_ITEM(args, i + 1, item);
      }
    }
  }

  res = KosEvalState_CallArgs(es, callable, args, kwd, tail);

 done:
  Py_XDECREF(kwd);
  Py_XDECREF(pos);
  Py_XDECREF(subject);
  Py_XDECREF(callable);
  Py_XDECREF(args);
  return res;
}


#include "handlers.incl"
#undef CURRENT_NS
#undef KOS_OP_REQUIRE_NS
#undef KOS_OP_HANDLER_COLD
#undef KOS_OP_HANDLER
#undef KOS_ELECTOR
#undef KOS_ELECTOR_PAIR_SAFETYNET


/* The handler table finalize resolves each KosOp's fn from.  Position
   is opcodes.incl order, which is also the KOS_OP_* enum order, so no
   designated initializers are needed.  Taking a hot handler's address
   gives it an out-of-line copy beside its inlined uses in the dispatch
   loop; the loop keeps calling the inlined form. */
#define OP(name, ...) op_##name,
const KosThreadFn kos_op_handlers[] = {
  #include "opcodes.incl"
};
#undef OP


/**
   Complete a native frame with a return value: the return unwind walk
   from the frame, FINALLY/async-unwind outcomes honored, frame
   detached and freed, value delivered to the parent (push-or-await).
   The handler slot must already be cleared. value consumed.
*/
KosOpResult KosNativeFrame_Return(KosEvalState *es, PyObject *value) {

  KosDynFrame *detached = NULL;
  KosDynDelim *took = NULL;
  KosOpResult unw = kos_dyn_unwind_return(es, KosEvalState_FRAME(es), value,
                                          KOS_PENDING_RETURN_NATIVE, &took);

  /* a FINALLY took the return, a leave is in flight with the value
     parked, or a leave raised; the walk consumed value on each. Every
     answer here is raw: the trampoline, or KosEval_ResumeReturn, routes
     it. */
  if (unw.kind != KOS_RES_NORMAL)
    return unw;
  if (took) {
    /* A native frame runs no bytecode, so no delimited-invoke bookmark
       can sit above it. Fail loudly rather than mis-deliver. */
    Py_DECREF(value);
    PyErr_SetString(PyExc_SystemError,
                    "native frame return walk stopped at a"
                    " delimited-invoke bookmark");
    return EVAL_RAISE();
  }

  if (kos_dyn_detach_top_frame(es, &detached) < 0) {
    Py_DECREF(value);
    return EVAL_RAISE();
  }
  if (detached)
    kos_dynframe_free(detached);

  {
    KosOpResult pr = KosEvalState_PushOrAwait(es, value);
    if (pr.kind == KOS_RES_NORMAL)
      return EVAL_REPROLOGUE();
    return pr;
  }
}


/**
   Complete a native frame by handing value to the eval loop's return
   machinery, which owns every delivery terminus (parent frame,
   iteration node, drive root). Clears the program slot and, when cache
   is given, arms the frame to park itself there when the return path
   frees it (KOS_FRAME_PARK_TO_CACHE; native_state becomes the cache
   slot). The caller returns this result from its handler. value is
   consumed.
*/
KosOpResult KosNativeFrame_ReturnPark(
    KosEvalState *es,
    PyObject *value,
    KosDynFrame **cache) {

  KosDynFrame *frame = KosEvalState_FRAME(es);

  if (cache) {
    KosDynFrame_PARK_CACHE(frame) = (void *) cache;
    KosDynFrame_FLAGS(frame) |= KOS_FRAME_PARK_TO_CACHE;
  } else {
    KosDynFrame_PARK_CACHE(frame) = NULL;
  }

  return EVAL_RETURN(value);
}


/**
   Push a code-less frame, reusing a parked frame from cache when one
   is available. Either way the frame gets an owned fill of the live
   namespace (kos_dyn_push_frame_native's contract): the parked frame
   was stripped of its fill at park, so the reinstall attaches a fresh
   one before the push publishes the frame's namespace to the live
   register. The caller fills resume_name itself.
*/
static KosDynFrame *kos_push_or_reuse_codeless_frame(
    KosEvalState *es,
    KosDynFrame **cache) {

  if (cache && *cache) {
    PyObject *live_ns = KosEvalState_NAMESPACE(es);
    KosDynFrame *frame = *cache;

    if (kos_eval_check_frame_room(es, KosEvalState_FRAME(es), 1) < 0)
      return NULL;
    *cache = NULL;
    Py_XINCREF(live_ns);
    KosDynFrame_NS(frame) = live_ns;
    kos_dyn_push_ready_frame(es, frame);
    return frame;
  }

  return kos_dyn_push_frame_native(es);
}


KosDynFrame *KosResumeFrame_Install(
    KosEvalState *es,
    const char *name,
    KosDynFrame **cache) {

  KosDynFrame *frame = kos_push_or_reuse_codeless_frame(es, cache);
  if (! frame)
    return NULL;

  KosDynFrame_RESUME_NAME(frame) = name;
  return frame;
}


/**
   One next-element request against iter from inside a native frame
   handler: push the iteration node, run the request, and clean the
   node up on every immediately-final outcome. See eval.h for the
   per-status contract.
*/
KosIterRequestStatus KosNativeFrame_RequestNext(
    KosEvalState *es,
    PyObject *iter,
    PyObject **out) {

  *out = NULL;

  if (kos_dyn_push_next_node(es, iter, NULL, 1) < 0)
    return KOS_IREQ_ERROR;

  switch (KosIterRequest_Next(es, KosEvalState_DYN_TOP(es), NULL, out)) {
    case KOS_IREQ_DELIVERED:
      kos_dyn_next_abort_after_failed_attach(es);
      return KOS_IREQ_DELIVERED;

    case KOS_IREQ_EXHAUSTED:
      Py_CLEAR(*out);
      kos_dyn_next_abort_after_failed_attach(es);
      return KOS_IREQ_EXHAUSTED;

    case KOS_IREQ_CONTINUES:
      return KOS_IREQ_CONTINUES;

    case KOS_IREQ_EXIT:
      return KOS_IREQ_EXIT;

    case KOS_IREQ_ERROR:
    default:
      kos_dyn_next_abort_after_failed_attach(es);
      return KOS_IREQ_ERROR;
  }
}


KosOpResult KosEvalState_NextResume(
    KosEvalState *es,
    PyObject *iter,
    PyObject *send_val,
    KosResumeFn resume,
    void *state) {

  KosDynFrame *frame = KosEvalState_FRAME(es);
  PyObject *out = NULL;

  (void) send_val;  /* no caller sends into Next yet; matches the
                        design's future Send entry point */

  switch (KosNativeFrame_RequestNext(es, iter, &out)) {
    case KOS_IREQ_DELIVERED:
      if (! resume) {
        /* OBJECT_NEXT's own default: the element lands on the stack */
        return KosEvalState_StackPUSH(es, out) < 0
            ? EVAL_RAISE() : EVAL_NORMAL();
      }
      return kos_entry_value(es, frame, out, resume, state);

    case KOS_IREQ_CONTINUES: {
      /* If the install left a real bytecode frame current (e.g. a
         generator attached), dispatch continues there directly with
         no trampoline hop; only a codeless result needs the bounce.
         With a resume, it is armed for the delivery either way. */
      KosDynFrame *cur = KosEvalState_FRAME(es);
      if (resume) {
        KosDynFrame_RESUME_FN(frame) = (void *) resume;
        KosDynFrame_RESUME_STATE(frame) = state;
      }
      return (cur && KosDynFrame_CODE(cur)) ? EVAL_NORMAL() : EVAL_REPROLOGUE();
    }

    case KOS_IREQ_EXIT:
      if (! resume)
        return EVAL_AWAIT(out);
      KosDynFrame_RESUME_FN(frame) = (void *) resume;
      KosDynFrame_RESUME_STATE(frame) = state;
      return EVAL_AWAIT(out);

    case KOS_IREQ_EXHAUSTED:
      PyErr_SetNone(PyExc_StopIteration);
      return EVAL_RAISE();

    case KOS_IREQ_ERROR:
    default:
      return EVAL_RAISE();
  }
}


/* Forward declaration: defined after the delivery step. */
static KosOpResult apply_kos_op_result(KosEvalState *es, KosOpResult res);

/* Forward declaration: defined below. */
static KosOpResult eval_step_resume_delivery(KosEvalState *es);


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
    KosEvalMode *mode,
    KosObligation *out_return) {

  KosOpResult ra = revive_await(es, send_val, throw_typ, throw_exc,
                                out_return);

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
  KosObligation resume_return;

  resume_return.kind = KOS_OB_NONE;
  if (send_val == NULL && throw_typ == NULL && throw_exc == NULL) {
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
                                               &mode, &resume_return);
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
                                             throw_exc, &mode,
                                             &resume_return);
        if (_ra.kind == KOS_RES_AWAIT)
          return _ra;
        break;
      }
    }
  }
  KosEvalState_ClearSuspensionEvent(es);

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

  /* The revive completed a node whose leave had a return parked on it
     (a wind whose after thunk tail-called the awaitable has no frame to
     return through). With that node gone, the return continues instead
     of the frame's bytecode. The answer has been through the ingress:
     a RAISE or an AWAIT is terminal, anything else goes on to dispatch. */
  if (mode == KOS_MODE_NORMAL && resume_return.kind == KOS_OB_RETURN) {
    KosOpResult rr = KosEval_ResumeReturn(es, &resume_return);
    switch (rr.kind) {
    case KOS_RES_NORMAL:
    case KOS_RES_REPROLOGUE:
    case KOS_RES_DISPATCH:
      *mode_out = KOS_MODE_NORMAL;
      return EVAL_NORMAL();
    default:
      return rr;
    }
  }

  /* Revived with a value (given, or staged by the revive) and the top
     frame is waiting on one: deliver. */
  if ((send_val != NULL || KosEvalState_PENDING_VALUE(es))
      && mode == KOS_MODE_NORMAL
      && KosEvalState_FRAME(es)
      && KosDynFrame_RESUME_FN(KosEvalState_FRAME(es))) {

    KosOpResult rs = eval_step_resume_delivery(es);
    if (rs.kind != KOS_RES_NORMAL
        && rs.kind != KOS_RES_REPROLOGUE
        && rs.kind != KOS_RES_DISPATCH)
      return rs;
    /* NORMAL / REPROLOGUE / DISPATCH: resume finished cleanly; proceed
       into dispatch (DISPATCH is loop-internal and must not escape) */
  }

  *mode_out = mode;
  return EVAL_NORMAL();
}


/**
   Phase 2: transfer goal drive.

   Drives the armed transfer goal as far as the chain allows.  When no frame and no
   hook remain after draining, emits a terminal RETURN(None) and marks
   the context dead.  Abandonment outcomes from the unified RAISE ingress
   (AWAIT / RAISE / FATAL) are terminal here.

   :return: EVAL_NORMAL when the dispatch loop should start;
            terminal KosOpResult when eval should stop.
*/
static KosOpResult eval_phase_transfer(KosEvalState *es) {

  KosContext *ctx = KosEvalState_CONTEXT(es);
  KosOpResult xfer = kos_eval_drain_goal(es);

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
   Centralized YIELD handler.  Flushes live registers to the frame, marks the
   generator dead-after-yield, freezes gen (the running frame's GEN, from
   kos_dyn_find_gen), delivers to the consuming iteration node (NEXT /
   FOREACH / YIELD_FROM) when there is one, and otherwise tags the
   suspension event.  val is a new reference; ownership transfers to the
   caller or consuming iteration node.  A YIELD_FROM receiver re-emits
   the value as its own yield on its next dispatch.

   A yield delivered to a flattened consumer ends in kos_eval_delivery_done:
   DISPATCH when the consumer is a plain bytecode frame, which op_YIELD
   threads into directly; REPROLOGUE when the prologue must deliver (a
   consumer waiting on a resume). A yield with no consumer on the chain
   is the terminal EVAL_YIELD the adapters drive.
*/
static KosOpResult eval_event_yield(
    KosEvalState *es,
    KosDynGen *gen,
    PyObject *val) {

  /* No flush here: the freeze's retarget flushes the outgoing frame's
     pcs when it switches to the consumer's. */
  SAYF("[eval] post_opcode KOS_RES_YIELD -> freeze gen=%p dyn_top=%p\n",
       (void *) gen, (void *) KosEvalState_CurrentNode(es));
  if (kos_dyn_gen_freeze(es, gen) < 0) {
    Py_DECREF(val);
    return EVAL_FATAL();
  }
  {
    KosDynNode *node = kos_dyn_find_iter_node(es);
    if (node) {
      KosOpResult dr = KosIterRequest_Deliver(es, node,
                                              KOS_IWAKE_VALUE, val);
      if (dr.kind == KOS_RES_NORMAL)
        return kos_eval_delivery_done(es);
      if (dr.kind == KOS_RES_RAISE) {
        KosOpResult rs;
        KOS_TRACE_POST_OP_WRAP(event_raise, rs = process_raise(es));
        return rs;
      }
      return dr;
    }
  }
  /* Only a yield that stops the evaluator leaves the context not
     running; a delivered yield keeps threading, and the context stays
     RUNNING so KosContext_Traverse keeps visiting the live state. */
  KosEvalState_MarkDeadAfterYield(es);
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

  KosDynDelim *took = NULL;
  KosOpResult unw = kos_dyn_unwind_return(es, KosEvalState_FRAME(es), val,
                                          KOS_PENDING_RETURN_FRAME, &took);

  /* a FINALLY took the return, a leave is in flight with val parked, or
     a leave raised; the walk consumed val on each */
  if (unw.kind != KOS_RES_NORMAL)
    return kos_eval_walk_outcome(es, unw);
  if (took) {
    /* The return crossed a delimiting reset: the walk stopped at a DELIM
       with a live invoke bookmark (at dyn_top).  Deliver val to the
       k-invocation site instead of the frame's parent. */
    KosOpResult rr = kos_eval_route_delim_return(es, took, val);
    if (rr.kind == KOS_RES_NORMAL)
      return EVAL_REPROLOGUE();
    return kos_eval_walk_outcome(es, rr);
  }

  return eval_event_return_at_frame(es, val);
}


/**
   Case-1 return predicate: parent is a plain Koskari frame, so the return
   is a straight frame-to-frame pop with no node deliveries.  Assumes
   DYN_TOP == FRAME is already established.  The single definition, shared
   by eval_event_return_at_frame and kos_return_is_plain.

   eval_ng_plain_return sets es->code from the parent, pushes the value
   and returns EVAL_DISPATCH(), bypassing the prologue entirely. Both
   conjuncts below guard that, for separate reasons: without code there
   is nothing to dispatch, and with a resume pending the prologue is
   what would have delivered the value to it. Only codeless frames
   carry resumes today, so either check alone would do; they are
   written out because a bytecode frame with a call in flight would
   satisfy one and not the other.
*/
static inline int kos_return_parent_is_plain(KosDynNode *parent) {
  return parent
      && KosDynNode_IS_FRAME(parent)
      && KosDynFrame_CODE((KosDynFrame *) parent)
      && ! KosDynFrame_RESUME_FN((KosDynFrame *) parent);
}


/**
   Whole plain-return predicate: no dynamic extent above the returning
   frame, and its parent is another plain Koskari frame.  Sets out_child
   and out_parent and returns 1 when it holds.

   Used by the dispatch-loop fast arm to route a KOS_RES_RETURN straight
   into eval_ng_plain_return, skipping apply_kos_op_result and the guard
   chain it would otherwise walk to reach the same place.  Stricter than
   the chain it short-circuits only in requiring a non-NULL frame, so a
   miss can never do more than fall through to the general path.
*/
static inline int kos_return_is_plain(
    KosEvalState *es,
    KosDynNode **out_child,
    KosDynNode **out_parent) {

  KosDynNode *child = (KosDynNode *) KosEvalState_FRAME(es);
  KosDynNode *parent;

  if (unlikely(! child || KosEvalState_DYN_TOP(es) != child))
    return 0;

  parent = KosDynNode_PARENT(child);
  if (unlikely(! kos_return_parent_is_plain(parent)))
    return 0;

  *out_child = child;
  *out_parent = parent;
  return 1;
}


/**
   NG fast path: plain Koskari frame-to-frame return.

   Inline retarget from child (being freed) to parent.  Skips the chain
   walk, the flush (frame is freed), and the agen iteration-node check (not
   reachable from a frame whose parent is itself a frame).  val consumed.
*/
KOS_OP_INLINE KosOpResult eval_ng_plain_return(
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

  if (KosEvalState_StackPUSH(es, val) < 0)
    return EVAL_FATAL();

  /* No control-returned hook: parent (now dyn_top) is a frame, and a
     WITH beneath it is parent's own extent, pending or not, whose hook
     runs when parent itself returns. No PyErr is set, no plan is armed,
     and parent has no resume (the case-1 guard), so the trampoline can
     thread directly -- no prologue re-entry. */
  return EVAL_DISPATCH();
}


/**
   Region-parent return predicate: the frame returns into a chain node
   that passes a callee's value through to the frame beneath it -- any
   node that is not itself a frame, a generator, or an iteration node
   with a step in flight (the receiving side of a value-grafted invoke,
   which is a return terminus). Every retarget fact is cache-available:
   the back frame recorded at install, the freed segment's displaced
   owning slot, and the node's namespace fill. A node that reacts to
   control returning into it says so with KOS_DYN_CAP_CONTROL_HOOK and
   the transition runs its hook; the rest take the value and nothing
   more. Any missing fact falls through to the general path, mirroring
   the cache-or-walk split in kos_dyn_resync_after_pop.
*/
KOS_OP_INLINE int kos_return_is_region(
    KosEvalState *es,
    KosDynNode **out_child,
    KosDynNode **out_parent,
    KosDynFrame **out_ft) {

  KosDynNode *child = (KosDynNode *) KosEvalState_FRAME(es);
  KosDynNode *parent;
  KosDynFrame *ft;

  if (! child || KosEvalState_DYN_TOP(es) != child)
    return 0;

  parent = KosDynNode_PARENT(child);
  if (! parent
      || KosDynNode_IS_FRAME(parent)
      || KosDynNode_IS_GEN(parent)
      || KosDynNode_HAS_CAP(parent, KOS_DYN_CAP_ITER_RECEIVER))
    return 0;

  /* Same two conjuncts as kos_return_parent_is_plain, for the frame
     eval_ng_region_return will dispatch on. */
  ft = (KosDynFrame *) KosDynFrame_BACK_FRAME(child);
  if (! ft
      || ! KosDynNode_IS_FRAME((KosDynNode *) ft)
      || ! KosDynFrame_CODE(ft)
      || KosDynFrame_RESUME_FN(ft))
    return 0;

  if (! KosDynNode_STACK(child)
      || ! KosSegment_BACK(KosDynNode_STACK(child))
      || ! KosDynNode_NS(parent))
    return 0;

  *out_child = child;
  *out_parent = parent;
  *out_ft = ft;
  return 1;
}


/**
   Region-parent return transition, once kos_return_is_region holds:
   retarget straight from the cached facts, free the callee, push the
   value on the region's segment, then let a node that hooks control
   returns react (the hook ends in kos_eval_delivery_done); a node
   without the hook dispatches directly. val consumed.
*/
KOS_OP_INLINE KosOpResult eval_ng_region_return(
    KosEvalState *es,
    KosDynNode *child,
    KosDynNode *parent,
    KosDynFrame *ft,
    PyObject *val) {

  KosSegment **bl = KosSegment_BACK(KosDynNode_STACK(child));

  KosEvalState_DYN_TOP(es) = parent;
  KosEvalState_FRAME(es) = ft;
  KosEvalState_BLOCK(es) = bl;
  KosEvalState_NAMESPACE(es) = KosDynNode_NS(parent);
  KosEvalState_CODE(es) = KosDynFrame_CODE(ft);
  es->pc = KosDynFrame_PC(ft);
  es->acting_pc = KosDynFrame_ACTING_PC(ft);
  kos_dynframe_free((KosDynFrame *) child);

  if (KosEvalState_StackPUSH(es, val) < 0)
    return EVAL_FATAL();

  if (KosDynNode_HAS_CAP(parent, KOS_DYN_CAP_CONTROL_HOOK))
    return kos_eval_post_opcode_control_returned_after_hook(es);
  return kos_eval_delivery_done(es);
}


/**
   Resume-parent return predicate, for op_RETURN: the returning frame
   is dyn_top, and its parent is a frame waiting on a resume with no
   WITH beneath it to run a control-returned hook. Sets out_parent and
   returns 1 when it holds.
*/
KOS_OP_INLINE int kos_return_is_resume(
    KosEvalState *es,
    KosDynNode **out_parent) {

  KosDynNode *child = (KosDynNode *) KosEvalState_FRAME(es);
  KosDynNode *parent;
  KosDynNode *pp;

  if (! child || KosEvalState_DYN_TOP(es) != child)
    return 0;

  parent = KosDynNode_PARENT(child);
  if (! parent
      || ! KosDynNode_IS_FRAME(parent)
      || ! KosDynFrame_RESUME_FN((KosDynFrame *) parent))
    return 0;

  pp = KosDynNode_PARENT(parent);
  if (pp && KosDynNode_IS_WITH(pp))
    return 0;

  *out_parent = parent;
  return 1;
}


/**
   Resume-parent return transition, once kos_return_is_resume holds:
   with val already settled, pop the returning frame, take the resume
   pair off the parent (cleared, as the delivery step clears it), and
   return 1: the caller tail-calls the resume with val, which is
   exactly what the prologue's delivery step would have done after a
   stage-and-bounce. Returns 0 when val still needs settling and
   nothing was touched, so the general return path applies; returns
   -1 with PyErr set and val released when the classification itself
   fails or the pop does.
*/
static int kos_return_take_resume(
    KosEvalState *es,
    KosDynNode *parent,
    PyObject *val,
    KosResumeFn *out_resume,
    void **out_state) {

  switch (KosEval_ResultIsValue(val)) {
    case 1:
      break;
    case 0:
      return 0;  /* needs settling: the delivery step owns that */
    default:
      Py_DECREF(val);
      return -1;
  }

  if (kos_dyn_pop_frame(es) < 0) {
    Py_DECREF(val);
    return -1;
  }

  *out_resume = (KosResumeFn) KosDynFrame_RESUME_FN((KosDynFrame *) parent);
  *out_state = KosDynFrame_RESUME_STATE((KosDynFrame *) parent);
  KosDynFrame_RESUME_FN((KosDynFrame *) parent) = NULL;
  KosDynFrame_RESUME_STATE((KosDynFrame *) parent) = NULL;
  return 1;
}


/**
   Unified RETURN dispatch.  Called when DYN_TOP == FRAME — no try/with/wind
   nodes above it (either naturally, or after the unwind walk completes).

   Reads PARENT(FRAME) and dispatches to the correct terminus.  GEN is
   transparent: kos_dyn_teardown_gen_exhausted pops the frame and GEN together,
   then the consumer node above the GEN is the real terminus.  val consumed.
*/
static KosOpResult eval_event_return_at_frame(
    KosEvalState *es,
    PyObject *val) {

  KosDynNode *child = (KosDynNode *) KosEvalState_FRAME(es);
  KosDynNode *parent = KosDynNode_PARENT(child);

  /* Case 1: plain frame-to-frame — the dominant hot path.  Normally taken
     by the dispatch-loop fast arm before reaching here; this remains the
     terminus for producers that arrive by other routes (the unwind walk,
     the python adapters). */
  if (likely(kos_return_parent_is_plain(parent))) {
    KosOpResult r;
    KOS_TRACE_POST_OP_WRAP(parent_return,
        r = eval_ng_plain_return(es, child, parent, val));
    return r;
  }

  /* Case 2: gen parent — deliver to the receiving iteration node above the GEN.
     The receiver is the true terminus; GEN is unwound on the way out. */
  if (parent && KosDynNode_IS_GEN(parent)) {
    KosDynNode *consumer = KosDynNode_PARENT(parent);

    if (! consumer) {
      KosEvalState_MarkDeadAfterReturn(es);
      return EVAL_RETURN(val);
    }

    if (KosDynNode_HAS_CAP(consumer, KOS_DYN_CAP_ITER_RECEIVER)) {
      KosOpResult dr = KosIterRequest_Deliver(es, consumer,
                                              KOS_IWAKE_END, val);
      if (dr.kind == KOS_RES_NORMAL)
        return EVAL_REPROLOGUE();
      if (dr.kind == KOS_RES_RAISE) {
        KosOpResult rs;
        KOS_TRACE_POST_OP_WRAP(event_raise, rs = process_raise(es));
        return rs;
      }
      return dr;
    }

    PyErr_Format(PyExc_RuntimeError,
                 "eval_event_return: unexpected gen consumer kind %u",
                 (unsigned) KosDynNode_KIND(consumer));
    Py_DECREF(val);
    return EVAL_FATAL();
  }

  /* Case 2b: value-graft parent — an iteration node armed in value
     mode: the returning frame is a provider's grafted invoke, and its
     return value is the step element, delivered through the iteration node
     (push, disarm, ki_finish). */
  if (parent
      && KosDynNode_HAS_CAP(parent, KOS_DYN_CAP_ITER_RECEIVER)
      && kos_dyn_iter_node_value_mode(parent)) {

    if (unlikely(kos_dyn_pop_frame(es) < 0)) {
      Py_DECREF(val);
      return EVAL_FATAL();
    }

    {
      KosOpResult dr = KosIterRequest_Deliver(es, parent,
                                              KOS_IWAKE_VALUE, val);
      if (dr.kind == KOS_RES_NORMAL)
        return EVAL_REPROLOGUE();
      if (dr.kind == KOS_RES_RAISE) {
        KosOpResult rs;
        KOS_TRACE_POST_OP_WRAP(event_raise, rs = process_raise(es));
        return rs;
      }
      return dr;
    }
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
   Resume a return that was parked on a node whose leave went
   asynchronous, now that the node is gone. See eval.h.
*/
KosOpResult KosEval_ResumeReturn(KosEvalState *es, KosObligation *ob) {

  PyObject *val = ob->u.ret.value;
  KosPendingReturnKind kind = (KosPendingReturnKind) ob->u.ret.path;
  KosDynDelim *delim = (KosDynDelim *) ob->u.ret.delim;

  ob->kind = KOS_OB_NONE;
  ob->u.ret.value = NULL;
  ob->u.ret.delim = NULL;

  /* The node is gone, so the walk from the returning frame continues
     past it: further nodes may suspend again and park the value once
     more, or the walk reaches the frame and delivers. */
  switch (kind) {
  case KOS_PENDING_RETURN_FRAME:
    return eval_event_return_ng(es, val);

  case KOS_PENDING_RETURN_NATIVE:
    return kos_eval_walk_outcome(es, KosNativeFrame_Return(es, val));

  case KOS_PENDING_RETURN_DELIM:
    if (delim && kos_dyn_reaches(es, (KosDynNode *) delim))
      return kos_eval_walk_outcome(
          es, kos_eval_route_delim_return(es, delim, val));
    Py_DECREF(val);
    PyErr_SetString(PyExc_SystemError,
                    "pending return lost its delimited-invoke bookmark");
    return kos_eval_walk_outcome(es, EVAL_RAISE());

  default:
    Py_DECREF(val);
    PyErr_SetString(PyExc_SystemError, "pending return with no return path");
    return kos_eval_walk_outcome(es, EVAL_RAISE());
  }
}


/**
   Run the WIND/WITH settle hook after a CONTROL_RETURNED delivery.

   Inline early exit: a frame-topped chain has no hook work. The hook
   belongs to the WITH or WIND node that is current once its installed
   callee (an __enter__, an __exit__, a thunk) has popped; a frame whose
   parent is a pending WITH is that callee itself, and a return into it
   from its own callees must leave the pending phase alone.
*/
static KosOpResult kos_eval_post_opcode_control_returned_after_hook(
    KosEvalState *es) {

  KosDynNode *node = KosEvalState_CurrentNode(es);
  if (likely(! node || KosDynNode_IS_FRAME(node)))
    return kos_eval_delivery_done(es);

  KosOpResult r;
  KosObligation resume_return;

  resume_return.kind = KOS_OB_NONE;
  KOS_TRACE_POST_OP_WRAP(control_returned,
      r = kos_dyn_settle_top(es, &resume_return));
  if (r.kind == KOS_RES_RAISE) {
    KosOpResult rs;
    KOS_TRACE_POST_OP_WRAP(event_raise, rs = process_raise(es));
    return rs;
  }

  /* The unlink handed back a return that was parked on the node it
     settled: the node is gone, so the return continues from there. */
  if (resume_return.kind == KOS_OB_RETURN)
    return KosEval_ResumeReturn(es, &resume_return);

  return kos_eval_delivery_done(es);
}


/**
   After RETURN opcode pop: push onto the parent's stack, then
   kos_dyn_settle_top (WIND/WITH). A parent waiting on a
   resume picks the value up from that stack on the next prologue pass.
   val is a new ref; ownership is consumed by this function.
*/
static inline KosOpResult eval_parent_return_deliver_impl(
    KosEvalState *es, PyObject *val) {

  KosDynFrame *parent = KosEvalState_FRAME(es);

  /* Destination by one field: a parent waiting on a resume takes the
     value through the pending slot; a bytecode parent takes it on its
     stack, where its next instruction finds it. */
  if (parent && KosDynFrame_RESUME_FN(parent)) {
    KosEvalState_StageDelivery(es, val);
  } else if (KosEvalState_StackPUSH(es, val) < 0) {
    return EVAL_FATAL();
  }

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
static KosOpResult eval_step_drain_goal(KosEvalState *es) {
  if (! KosEvalState_GOAL(es))
    return EVAL_NORMAL();
  return kos_eval_drain_goal(es);
}


/**
   One CPS resume delivery: pop the value staged on the current frame's
   own stack, clear the frame's resume, and invoke it. Caller must only
   invoke when the eval frame has RESUME_FN set.

   This is the single point every staged delivery converges on -- a
   callee's frame return, an iteration node's element, and an awaited
   resume all reach a resume through here -- so it is also where the
   ambient await interposes. A staged value that still needs awaiting
   never reaches the resume: the same resume is set again and the value
   goes to KosEvalState_PushOrAwait, which either settles it eagerly
   (the prologue re-enters here and re-classifies) or suspends (the
   revive re-enters here with the settled value). A resume is therefore
   only ever invoked with a settled value, and a value that settles to
   a further awaitable simply bounces again. Consumers do not classify;
   that duplication is what this API exists to remove.

   A resume-carrying frame's own stack holds nothing else, so an empty
   stack unambiguously means no delivery was ever staged -- a consumer
   set the resume directly (bypassing KosEvalState_CallResult /
   NextResume) to defer its own first step to this trampoline, the way
   a cold KOS_ITER_INSTALLED install must (koskari_eval runs as a
   separate step after install, never nested inside it). That resume is
   invoked with NULL and no exception set, carries no value to settle,
   and its own contract says what NULL means. Errors are never
   delivered through this channel -- see the raise walk's resume call
   in dyn_walk.c.
*/
static KosOpResult eval_step_resume_delivery(KosEvalState *es) {
  KosDynFrame *frame = KosEvalState_FRAME(es);
  KosResumeFn resume = (KosResumeFn) KosDynFrame_RESUME_FN(frame);
  void *state = KosDynFrame_RESUME_STATE(frame);
  PyObject *val;
  KosOpResult res = EVAL_NORMAL();
  int have_res = 0;

  KosDynFrame_RESUME_FN(frame) = NULL;
  KosDynFrame_RESUME_STATE(frame) = NULL;

  /* The pending slot is the live transit. The frame stack carries a
     delivery only as storage: across a suspension (freeze moved the
     slot there), or from a landing site that still pushes -- an
     iteration node's element, a native frame's return. */
  val = KosEvalState_PENDING_VALUE(es);
  KosEvalState_PENDING_VALUE(es) = NULL;

  /* Only a code-less step frame keeps its delivery on its stack; a
     bytecode frame's stack holds its operands, and its delivery is
     always the slot. */
  if (! val && ! KosDynFrame_CODE(frame) && KosEvalState_StackLen(es) > 0) {
    val = KosEvalState_StackPopRef(es);
    if (! val) {
      res = EVAL_RAISE();
      have_res = 1;

#if KOS_EVAL_SAFETYNET
    } else if (KosEvalState_StackLen(es) > 0) {
      /* The empty-stack test above is the cold-start discriminator;
         a second staged value would make it lie. */
      Py_DECREF(val);
      val = NULL;
      PyErr_SetString(PyExc_SystemError,
                      "resume frame staged more than one value");
      res = EVAL_RAISE();
      have_res = 1;
#endif
    }
  }

  /* A node resume (NULL state: a yield-from phase) takes its delivery
     as it is: a delegate's yielded value is data, never awaited here. */
  if (val && ! have_res && state) {
    switch (KosEval_ResultIsValue(val)) {
      case 1:
        break;  /* settled: deliver it below */

      case 0:
        /* Not settled yet. Put the resume back and settle one step;
           the staged outcome is delivered on a later pass. */
        KosDynFrame_RESUME_FN(frame) = (void *) resume;
        KosDynFrame_RESUME_STATE(frame) = state;
        res = KosEvalState_SettleStage(es, val);
        have_res = 1;
        break;

      default:
        Py_DECREF(val);
        res = EVAL_RAISE();
        have_res = 1;
        break;
    }
  }

  if (! have_res) {
    KOS_TRACE_RESUME_STEP();
    res = resume(es, state, val);
  }

  /* Every outcome routes through the one hub: a RAISE from here is an
     ordinary in-eval raise and must run the unwind walk (try/finally
     extents, node conversion), and an AWAIT must reach
     eval_event_await, which is where a demand extent denies it and
     where the suspension event is recorded. Returning either raw would
     escape koskari_eval unprocessed. */
  if (res.kind == KOS_RES_NORMAL || res.kind == KOS_RES_REPROLOGUE)
    return EVAL_REPROLOGUE();

  {
    KosOpResult ar = apply_kos_op_result(es, res);
    if (ar.kind == KOS_RES_DISPATCH)
      return EVAL_REPROLOGUE();
    return ar;
  }
}


/**
   Route a non-NORMAL opcode result through the appropriate event handler.
   NORMAL and REPROLOGUE both map to EVAL_REPROLOGUE (re-enter through the prologue).
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
    KosDynGen *gen = kos_dyn_find_gen(es);

    if (unlikely(! gen)) {
      Py_DECREF(res.val);
      PyErr_SetString(PyExc_RuntimeError,
                      "yield outside generator: no generator node on chain");
      return EVAL_FATAL();
    }
    KOS_TRACE_POST_OP_WRAP(event_yield,
        r = eval_event_yield(es, gen, res.val));
    return r;
  }

  case KOS_RES_AWAIT: {
    KosOpResult r;
    KOS_TRACE_POST_OP_WRAP(event_await, r = eval_event_await(es, res.val));
    return r;
  }

  case KOS_RES_RETURN: {
    KosOpResult r;
    KOS_TRACE_POST_OP_WRAP(return_event, r = eval_event_return_ng(es, res.val));
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



/* Internal-invariant safety net. See KOS_EVAL_SAFETYNET in hints.h.
   Build with -DKOS_EVAL_SAFETYNET=1 to turn a would-be segfault from such an
   internal misfire into a clean diagnostic while tracking down the cause. */
#if KOS_EVAL_SAFETYNET
/* es->code can be transiently NULL only through an internal bug (e.g. a
   tail reset that failed to retarget it); guard the fetch against
   dereferencing it. */
# define KOS_EVAL_SAFETYNET_NULLCODE(code, on_bad)                         \
    do { if (unlikely(! (code))) goto on_bad; } while (0)
#else
# define KOS_EVAL_SAFETYNET_NULLCODE(code, on_bad)  ((void) 0)
#endif

/* No per-opcode PC bounds check: every code object ends in a THEEND barrier
   (appended in KosCode_New), and jump targets are validated in-bounds, so
   straight-line dispatch always hits a terminal RETURN or THEEND before pc
   could reach code_len -- arr[pc] is provably valid. THEEND faults exactly as
   an off-the-end fetch would. The es->code null check is an
   internal-invariant guard, gated behind KOS_EVAL_SAFETYNET (off by
   default). */
#define KOS_EVAL_PREPARE_NEXT_INSN(es, insn_lvalue, on_bad)                \
  do {                                                                     \
    KosCode *_kos_code = KosEvalState_CODE(es);                            \
    uint32_t _kos_pc = KosEvalState_PC(es);                                \
    KOS_EVAL_SAFETYNET_NULLCODE(_kos_code, on_bad);                        \
    KosOp *_kos_insn = &KosCode_CODE(_kos_code)[_kos_pc];                  \
    SAYF("[eval] pc=%u op=%s arg=%u\n",                                    \
         (unsigned) _kos_pc, kos_opcode_name(_kos_insn->op),               \
         (unsigned) _kos_insn->arg);                                       \
    (es)->acting_pc = _kos_pc;                                             \
    (es)->pc = _kos_pc + 1u;                                               \
    (insn_lvalue) = _kos_insn;                                             \
  } while (0)


/* A re-entry step that asks for another pass of the loop below. A
   pass is a cooperation point: a resume hop that found the breaker up
   staged its delivery and answered REPROLOGUE expecting a poll, and a
   loop that simply went round again would deliver, hop, and stage
   forever with the word still up. So when the breaker is up the
   re-entry hands REPROLOGUE back to the trampoline, whose prologue
   polls and re-enters. Not a do-while: the continue must reach the
   enclosing for. */
#define KOS_REENTER_AGAIN(es)                                           \
  {                                                                     \
    if (KOS_EVAL_BREAKER_UP(es))                                        \
      return EVAL_REPROLOGUE();                                         \
    continue;                                                           \
  }


KosOpResult kos_prologue_reenter(KosEvalState *es) {

  /* es live fields via Init, kos_dyn_* retarget after chain surgery,
     freeze at cleanup; chain errors propagate without rebuild. */
  for (;;) {

    #if KOS_STACK_TRACE
    {
      KosSegment **_kos_tr = KosEvalState_BLOCK(es);
      trace_stack("  after ",
                  (_kos_tr && *_kos_tr) ? *_kos_tr : NULL);
    }
    #endif

    /* Slim bounce: a staged delivery with no pending exception and no
       plan armed goes straight to its resume. The two checks below
       exist for chain surgery, which a staged handoff performs none
       of. */
    if (KosEvalState_PENDING_VALUE(es)
        && ! KosEvalState_GOAL(es)
        && ! PyErr_Occurred()) {
      KosOpResult rs = eval_step_resume_delivery(es);
      if (rs.kind == KOS_RES_REPROLOGUE)
        KOS_REENTER_AGAIN(es);
      return rs;
    }

    /* Pending PyErr from a suspended transfer thunk (e.g. RAISE in a
       wind before-thunk) must run abandon + try_local before draining
       the plan; otherwise the wind EXTENT_ACTION can complete while
       PyErr is still set. */
    {
      KosOpResult pend = eval_phase_pending_exception(es);
      if (pend.kind != KOS_RES_NORMAL) {
        if (pend.kind == KOS_RES_REPROLOGUE)
        KOS_REENTER_AGAIN(es);
        return pend;
      }
    }

    /* A transfer plan replaces the control path a staged delivery was
       waiting on: the frame it was staged for may be gone once the
       plan lands, and the plan's own INJECT is the delivery the
       landing frame expects. Drop the stale slot ahead of the drain so
       it can neither shadow that injection nor reach a resume it was
       never meant for. */
    if (KosEvalState_GOAL(es))
      Py_CLEAR(KosEvalState_PENDING_VALUE(es));

    {
      KosOpResult xfer = eval_step_drain_goal(es);
      if (xfer.kind != KOS_RES_NORMAL) {
        if (xfer.kind == KOS_RES_REPROLOGUE)
        KOS_REENTER_AGAIN(es);
        return xfer;
      }
    }

    if (KosEvalState_FRAME(es)
        && KosDynFrame_RESUME_FN(KosEvalState_FRAME(es)) != NULL) {
      KosOpResult rs = eval_step_resume_delivery(es);
      if (rs.kind == KOS_RES_REPROLOGUE)
        KOS_REENTER_AGAIN(es);
      return rs;
    }

#if KOS_EVAL_SAFETYNET
    /* Instruction dispatch never runs over a staged delivery: the slot
       is consumed in the trip that staged it, so a capture made by an
       opcode can never observe one. */
    if (unlikely(KosEvalState_PENDING_VALUE(es))) {
      Py_CLEAR(KosEvalState_PENDING_VALUE(es));
      PyErr_SetString(PyExc_SystemError,
                      "CPS delivery staged with no resume to take it");
      return EVAL_FATAL();
    }
#endif

    #if KOS_STACK_TRACE
    {
      KosSegment **_kos_tr = KosEvalState_BLOCK(es);
      trace_stack("  before",
                  (_kos_tr && *_kos_tr) ? *_kos_tr : NULL);
    }
    #endif

    return EVAL_NORMAL();
  }
}


/**
   The evaluator entry: entry and revive normalization, transfer-plan
   synchronization, then the trampoline that threaded dispatch returns
   to.  Threading enters from es.pc; a chain then runs handler to
   handler by tail calls and comes back here only with a signal.
   NORMAL means the chain stopped with the next instruction at es.pc --
   a cooperation point that found the eval breaker up, or a step with
   no way to continue -- so poll if the breaker is up and thread again.  Everything else is applied through
   apply_kos_op_result, and whatever that yields either threads
   (DISPATCH), re-enters through the prologue (REPROLOGUE), or leaves
   through the cleanup below.  The eval state is this function's own
   local; handlers and helpers take its address.
*/
KosOpResult koskari_eval(
    KosContext *ctx,
    PyObject *send_val,
    PyObject *throw_typ,
    PyObject *throw_exc) {

  KosOpResult eval_ret = EVAL_RAISE();
  KosExcState host_exc;
  KosEvalState es;
  KosOp *insn;
  KosOpResult r;

  if (! ctx) {
    PyErr_SetString(PyExc_RuntimeError, "NULL context for koskari_eval");
    return EVAL_RAISE();
  }

  /* FAST-slot code needs a namespace with fast slots. Checked here,
     before anything below is set up, so a refusal returns with nothing
     to unwind: the cleanup label pops the thread-local eval, context,
     and token entries that only the pushes below create, and reaching
     it from an earlier point would pop an enclosing eval's entries
     instead. A resume was checked on its first entry, and a native
     frame carries no code. */
  {
    KosDynFrame *top = KosContext_FRAME_TOP(ctx);
    KosCode *code = top ? KosDynFrame_CODE(top) : NULL;
    if (code && KosCode_USES_FAST(code)) {
      PyObject *ns = KosContext_NAMESPACE(ctx);
      if (! ns)
        ns = KosDynFrame_NS(top);
      if (! KosNamespace_SupportsFast(ns)) {
        PyErr_Format(PyExc_RuntimeError,
                     "code uses fast slots but namespace does not support"
                     " them: %R", ns ? ns : Py_None);
        return EVAL_RAISE();
      }
    }
  }

  /* Swap in this context's handler-visible exception state (sys.exc_info),
     saving the host's. Contexts interleaved on one thread must not see
     each other's handled exception; the reverse swap runs at cleanup. */
  KosExc_Init(&host_exc);
  KosExc_Fetch(&host_exc);
  if (KosExc_Held(&host_exc) || KosExc_Held(KosContext_EXC_INFO(ctx)))
    KosExc_Install(KosContext_EXC_INFO(ctx));

  KosEvalState_Init(&es, ctx);
  KosEvalState_ThawFromContext(&es);
  if (KosEvalState_DYN_TOP(&es))
    kos_dyn_resync_chain(&es);

  KosContext_FLAGS(ctx) |= KOS_CTX_RUNNING;

  KosEvalState_EnterDomainScope(&es);
  kos_tls_push_active_eval(&es);
  KOS_TRACE_EVAL_PUSH();
  KOS_TRACE_TRAMPOLINE_STEP();

  /* Phase 1: entry/revive normalization */
  {
    KosEvalMode mode = KOS_MODE_NORMAL;
    KosOpResult pe = eval_phase_entry(&es, send_val, throw_typ, throw_exc,
                                      &mode);
    if (pe.kind != KOS_RES_NORMAL && pe.kind != KOS_RES_REPROLOGUE) {
      eval_ret = pe;
      goto koskari_eval_cleanup;
    }

    /* Phase 2: transfer goal drive (only when mode warrants it) */
    if (mode == KOS_MODE_NORMAL) {
      if (unlikely(KosEvalState_GOAL(&es))) {
        KosOpResult pt = eval_phase_transfer(&es);
        if (pt.kind != KOS_RES_NORMAL && pt.kind != KOS_RES_REPROLOGUE) {
          eval_ret = pt;
          goto koskari_eval_cleanup;
        }
      }
    }
  }

  /* The Python resume slot is armed by a driver before an await revive
     and taken by the revive; an entry that skipped the revive (an await
     iterator still pending) clears it here, so the trampoline and the
     chain never touch it. */
  KosEvalState_PythonResumeSet(&es, NULL, KOS_RESUME_NONE);

  /* === the trampoline === */

prologue:
  /* A re-entry is a cooperation point: a raise walk, a plan transfer,
     a continuation replay, or a call install that found the breaker
     up all arrive here.  The poll runs before the re-entry delivers: a
     resume hop that found the breaker up staged its delivery so that
     a pending signal is seen ahead of it. */
  if (KOS_EVAL_BREAKER_UP(&es))
    goto poll;

reenter:
  {
    KosOpResult pr = kos_prologue_reenter(&es);
    if (pr.kind == KOS_RES_REPROLOGUE)
      goto prologue;  /* a pass found the breaker up: poll first */
    if (pr.kind != KOS_RES_NORMAL) {
      eval_ret = pr;
      goto koskari_eval_cleanup;
    }
  }
  goto thread;

poll:
  KOS_TRACE_TRAMPOLINE_STEP();
  /* The breaker is up.  Another thread waiting on the GIL has already
     outlasted the interpreter's switch interval, so yield it now, as
     Python's own loop would; then take any pending signal.
     Py_MakePendingCalls is the public entry that runs the
     interpreter's own signal handling, which is what clears the
     breaker's signal input: PyErr_CheckSignals alone would leave it up
     and every later cooperation point polling.  A signal raised here
     is taken by the re-entry's pending-exception step, which drops
     any staged delivery before the raise walk. */
  if (koskari_gil_drop_requested()) {
    Py_BEGIN_ALLOW_THREADS
    Py_END_ALLOW_THREADS
  }
  Py_MakePendingCalls();
  goto reenter;

thread:
  KOS_EVAL_PREPARE_NEXT_INSN(&es, insn, pc_bounds_error);
  KOS_TRACE_THREAD_STEP(insn->op);
  r = insn->fn(&es, insn, NULL);

  if (likely(r.kind == KOS_RES_NORMAL)) {
    /* A chain hands back NORMAL only from a cooperation point that
       found the breaker up (or a handler with no chain to run). */
    if (KOS_EVAL_BREAKER_UP(&es))
      goto poll;
    goto thread;
  }
  goto handle_result;

#if KOS_EVAL_SAFETYNET
pc_bounds_error:
  kos_eval_report_pc_bounds_error(&es);
  eval_ret = EVAL_FATAL();
  goto koskari_eval_cleanup;
#endif

handle_result:
  KOS_TRACE_TRAMPOLINE_STEP();
  {
    /* The plain-return fast arms live in op_RETURN, which threads
       straight into the parent when they hold and signals RETURN only
       when they do not; a RETURN arriving here has either already
       failed them or comes from another producer (POP_PROMPT, the
       values.c and eval_utils.c termini, a resume's completion), and
       takes the general path, whose own plain case is the same
       transition. */
    KosOpResult ar;

    ar = apply_kos_op_result(&es, r);

    if (ar.kind == KOS_RES_DISPATCH) {
      /* Plain frame-to-frame return: live regs already retargeted, no
         pending PyErr / plan / native handler.  Skip the prologue and
         go straight back to threading. */
      goto thread;
    }
    if (ar.kind == KOS_RES_REPROLOGUE) goto prologue;
    eval_ret = ar;
  }

koskari_eval_cleanup:
  KOS_TRACE_OP_EXIT();
  KOS_TRACE_EVAL_POP();
  kos_tls_pop_active_eval();
  KosEvalState_FlushLiveRegsToFrame(&es);
  KosEvalState_FreezeToContext(&es);
  KosContext_FLAGS(ctx) &= ~KOS_CTX_RUNNING;
  KosEvalState_LeaveDomainScope(&es);

  /* Save this context's handler-visible exception state and restore the
     host's (the reverse of the entry swap). */
  {
    KosExcState current;
    KosExc_Init(&current);
    KosExc_Fetch(&current);
    if (KosExc_Held(&current) || KosExc_Held(&host_exc)) {
      KosExc_Clear(KosContext_EXC_INFO(ctx));
      KosExc_Move(KosContext_EXC_INFO(ctx), &current);
      KosExc_Install(&host_exc);
    } else {
      KosExc_Clear(&current);
      KosExc_Clear(&host_exc);
    }
  }
  return eval_ret;
}


#undef KOS_EVAL_PREPARE_NEXT_INSN


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
