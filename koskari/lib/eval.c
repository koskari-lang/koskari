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

   1. Context ownership: A KosContext is a Python object held by
      reference; every holder (coroutine, async generator, transform,
      iteration adapter) keeps its own reference and the last one to
      let go deallocates it. See the ownership notes in eval.h.

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
      cc-unwind goal is pushed: it leaves every extent in the current
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


/* Module-level string constants for await/send/throw/close.  Initialized
   in koskari_eval_init, cleared in koskari_eval_clear. */
static PyObject *str_asend = NULL;
static PyObject *str_athrow = NULL;
static PyObject *str_enter = NULL;
static PyObject *str_exit = NULL;
static PyObject *str_aenter = NULL;
static PyObject *str_aexit = NULL;
static PyObject *str_name = NULL;
static PyObject *str_package = NULL;
static PyObject *str_empty = NULL;   /* "" : the name of a dots-only import */
static PyObject *str_asyncio_future_blocking = NULL;

/* KoskariContinuationApply payload attributes (see cont.c raise sites) */
static PyObject *str_args = NULL;
static PyObject *str_continuation = NULL;
static PyObject *str_target = NULL;

/* The exception's frame record attribute and its entries' keys (see
   dynchain_frame_meta) */
static PyObject *str_dynchain = NULL;
static PyObject *str_rec_name = NULL;
static PyObject *str_rec_filename = NULL;
static PyObject *str_rec_line = NULL;
static PyObject *str_rec_col = NULL;
static PyObject *str_rec_pc = NULL;
static PyObject *str_rec_crossed = NULL;
static PyObject *str_rec_frame = NULL;


/*
  Sentinel fromlist for the import opcodes (see kos_import_module).
  Passing ("*",) as fromlist to PyImport_ImportModuleLevelObject causes it
  to return the deepest (leaf) submodule rather than the topmost package,
  matching importlib.import_module semantics.  Initialized in
  koskari_eval_init.
*/
static PyObject *_import_fromlist = NULL;

PyObject *KosIterEnd = NULL;


/* === Static forward declarations === */

static void inject_eval_frame_traceback(
    KosContext *ctx,
    PyObject **exc_type,
    PyObject **exc_val,
    PyObject **exc_tb);

static void kos_eval_mark_context_dead(KosContext *ctx);

static KosOpResult process_raise(KosContext *ctx);

static KosOpResult apply_kos_op_result(KosContext *ctx, KosOpResult res);

static KosOpResult kos_eval_walk_outcome(KosContext *ctx, KosOpResult r);

static KosOpResult eval_parent_return_deliver(KosContext *ctx, PyObject *val);

static inline int kos_return_parent_is_plain(KosDynNode *parent);

static KosOpResult eval_event_yield(
    KosContext *ctx,
    KosDynGen *gen,
    PyObject *val);

static KosOpResult eval_event_await(KosContext *ctx, PyObject *val);

static KosOpResult eval_event_return_unwind(KosContext *ctx, PyObject *val);

static KosOpResult eval_event_return_at_frame(KosContext *ctx, PyObject *val);

static KosOpResult eval_event_return(KosContext *ctx, PyObject *val);

static KosOpResult kos_eval_post_opcode_control_returned_after_hook(
        KosContext *ctx);



#include "eval/eval_chain.c"
#include "eval/eval_drive.c"
#include "eval/eval_await.c"
#include "eval/eval_call.c"
#include "eval/eval_iter.c"
#include "eval/eval_obj.c"


int KosEval_IsFuture(PyObject *val) {
  return kos_isfuture(val);
}
#include "eval/eval_cache.c"
#include "eval/eval_match.c"


/* === KosIter provider step drive === */


/**
   One step of a provider through its suite, for every site that
   steps one: the next request, the for-each step, yield-from and the
   Python adapters. A sync iterator steps through tp_iternext; a
   python async iterator through its eager-stepped __anext__. send_val
   is borrowed (protocol send); *fast_out receives a new reference on
   DELIVERED/EXHAUSTED, and the awaitable payload on EXIT.
*/
KosIterStepResult kos_iter_step_provider(
    KosContext *ctx,
    PyObject *iter,
    KosIterSlots *islots,
    PyObject *send_val,
    PyObject **out) {

  KosIterStepResult r = islots->ki_step(ctx, iter, send_val, out);

  if (r != KOS_ITER_ERROR)
    return r;

  /* A StopIteration ending a provider step is that provider's
     exhaustion -- builtins.map truncation parity for a mapped
     function's StopIteration included -- and its value is the end's
     payload. */
  if (PyErr_ExceptionMatches(PyExc_StopIteration)) {
    PyObject *payload = NULL;

    if (PyGen_FetchStopIterationValue(&payload) < 0)
      return KOS_ITER_ERROR;
    islots->ki_finish(iter, ! islots->ki_pyctx_recyclable);
    *out = payload;
    return KOS_ITER_EXHAUSTED;
  }
  return KOS_ITER_ERROR;
}


KosIterRequestStatus KosIterRequest_Next(
    KosContext *ctx,
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

    switch (kos_iter_step_provider(ctx, iter, islots, send_val, &out)) {
      case KOS_ITER_VALUE:
        *fast_out = out;
        return KOS_IREQ_DELIVERED;

      case KOS_ITER_EXHAUSTED:
        *fast_out = out;
        return KOS_IREQ_EXHAUSTED;

      case KOS_ITER_INSTALLED:
      case KOS_ITER_INSTALLED_VALUE:
        return KOS_IREQ_CONTINUES;

      case KOS_ITER_ERROR:
      default:
        return KOS_IREQ_ERROR;
    }
  }

  {
    PyObject *value = NULL;
    PyObject *_await = NULL;

    switch (op_get_next_from_bound(ctx, req->kind, &_await, iter, &value)) {
      case GET_NEXT_VALUE:
        *fast_out = value;
        return KOS_IREQ_DELIVERED;

      case GET_NEXT_EXHAUSTED:
        *fast_out = NULL;
        return KOS_IREQ_EXHAUSTED;

      case GET_NEXT_SUSPEND:
        /* Suspended python async step: the awaited completion IS the
           element, staged for the step resume at revive. */
        *fast_out = _await;
        return KOS_IREQ_EXIT;

      case GET_NEXT_ERROR:
      default:
        return KOS_IREQ_ERROR;
    }
  }
}


/* Whether the live frame carries a resume: the receiver of a
   producer's element or end. */
static inline int iter_frame_has_resume(KosContext *ctx) {
  KosDynFrame *frame = KosContext_FRAME(ctx);

  return frame && KosDynFrame_RESUME_KIND(frame) != NULL;
}


/**
   Hand an element, or the end of the iteration, to the resume the
   live frame carries. The resume's own record of what it stepped is
   the resume's to finish. An end arrives as the protocol states it,
   NULL with StopIteration carrying the producer's return value. The
   answer is RESUME with the kind left on the frame: the event's
   thread function tails into the relay, whose RESUME arm clears the
   resume and enters it by tail call, staging first for a kind that
   settles its delivery. Never a call into the resume from here: a
   resume threads the consumer on and does not return while the loop
   runs, so a call would nest a C frame per element. value is
   consumed.
*/
static KosOpResult iter_deliver_to_resume(
    KosContext *ctx,
    PyObject *value,
    int exhausted) {

  if (exhausted) {
    kos_raise_stop_iteration(value);
    value = NULL;
  }
  return EVAL_RESUME(value);
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
        KosContext *ctx,
        PyObject *send_val,
        PyObject *throw_typ,
        PyObject *throw_exc,
        KosObligation *out_return) {

  if (throw_exc != NULL) {
    PyObject *typ_eff = throw_typ ? throw_typ
        : (PyObject *) Py_TYPE(throw_exc);

    SAYF("[await] revive_await throw typ=%p exc=%p\n",
         (void *) throw_typ, (void *) throw_exc);

    if (! KosContext_PythonAwaitIter(ctx)) {
      PyErr_SetObject(typ_eff, throw_exc);
      return EVAL_RAISE();
    }
    {
      PyObject *typ = typ_eff;
      PyObject *val = throw_exc;
      PyObject *tb = Py_None;
      PyObject *res = NULL;
      PySendResult step;
      Py_INCREF(typ);
      Py_INCREF(val);
      Py_INCREF(tb);
      PyErr_NormalizeException(&typ, &val, &tb);
      step = kos_iter_throw(KosContext_PythonAwaitIter(ctx), val, &res);
      Py_DECREF(typ);
      Py_DECREF(val);
      Py_XDECREF(tb);
      if (step == PYGEN_NEXT)
        return EVAL_AWAIT(res);
      if (step == PYGEN_RETURN) {
        PyObject *await_result = res;
        KosContext_PythonAwaitClearIter(ctx);
        KosContext_PythonResumeSet(ctx, await_result, KOS_RESUME_AWAIT);
        PyErr_Clear();
        /* Fall through: push inject like a normal successful resume */
      }
      else {
        KosContext_PythonAwaitClearIter(ctx);
        return EVAL_RAISE();
      }
    }
  }
  PyObject *to_push = NULL;
  uint8_t resume_kind_unused;
  /* Take transfers the stored inject's reference to us (owned == 1);
     the send_val fallback is borrowed from the caller. */
  int owned = KosContext_PythonResumeTake(
      ctx, &to_push, &resume_kind_unused);
  (void) resume_kind_unused;
  if (to_push == NULL && send_val != NULL) {
    to_push = send_val;
  }
  if (to_push != NULL && KosContext_FRAME(ctx)) {
    if (KosDynFrame_RESUME_KIND(KosContext_FRAME(ctx))) {
      /* the frame is waiting on a resume: the awaited value is its
         delivery, staged for the entry's delivery step. A for-each
         step's awaited element arrives this way, and the step resume
         finishes the region's request. A revive runs ahead of the
         trampoline, so the resume cannot run from here. */
      if (! owned)
        Py_INCREF(to_push);
      KosContext_StageDelivery(ctx, to_push);
      owned = 0;
    } else if (KosContext_StackPushRef(ctx, to_push) < 0) {
      if (owned)
        Py_DECREF(to_push);
      return EVAL_RAISE();
    }
  }
  if (owned)
    Py_DECREF(to_push);
  {
    /* the awaited value may complete a node's pending leave; a return
       stored on that node is handed back for the entry phase to resume */
    KosOpResult sr = kos_dyn_settle_top(ctx, out_return);
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
    KosContext *ctx,
    PyObject *send_val,
    PyObject *throw_typ,
    PyObject *throw_exc) {

  PyObject *to_push;

  SAYF("[gen] revive_yield ENTER suspend=%d send_val=%p "
       "throw_typ=%p throw_exc=%p dyn_top=%p frame_top=%p\n",
       KosContext_SuspensionEvent(ctx),
       (void *) send_val, (void *) throw_typ, (void *) throw_exc,
       (void *) KosContext_DYN_TOP(ctx),
       (void *) KosContext_FRAME(ctx));

  /* A frame waiting on a resume (a yield-from delegation in flight)
     took no placeholder at install and takes its delivery through the
     pending slot. A throw goes to the resume the same way, which
     throws it into the delegate; GeneratorExit raises at the frame
     instead, and the raise walk closes the delegate and clears the
     resume when it unwinds the delegation's node. */
  if (KosContext_FRAME(ctx)
      && KosDynFrame_RESUME_KIND(KosContext_FRAME(ctx))) {
    if (throw_exc != NULL) {
      PyErr_SetObject(throw_typ, throw_exc);
      return KosContext_ThrowIntoResume(ctx)
          ? KOS_MODE_NORMAL : KOS_MODE_EXCEPTION;
    }
    to_push = (send_val != NULL) ? send_val : Py_None;
    Py_INCREF(to_push);
    KosContext_StageDelivery(ctx, to_push);
    return KOS_MODE_NORMAL;
  }
  if (throw_exc != NULL) {
    if (KosContext_FRAME(ctx))
      Py_XDECREF(KosContext_StackPopRef(ctx));
    PyErr_SetObject(throw_typ, throw_exc);
    return KOS_MODE_EXCEPTION;
  }
  if (KosContext_FRAME(ctx)) {
    /* pop_ref hands back an owned reference: releasing it here is
       what pays off the placeholder push -- a drive that staged the
       sent value ahead of us leaves that value on top, and dropping
       the reference silently leaked one per send. */
    Py_XDECREF(KosContext_StackPopRef(ctx));
    to_push = (send_val != NULL) ? send_val : Py_None;
    if (KosContext_StackPushRef(ctx, to_push) < 0)
      return KOS_MODE_EXCEPTION;
  }
  SAYF("[gen] revive_yield DONE frame_top=%p dyn_top=%p\n",
       (void *) KosContext_FRAME(ctx),
       (void *) KosContext_DYN_TOP(ctx));
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

  PyDict_SetItem(meta, str_rec_name, name ? name : Py_None);
  PyDict_SetItem(meta, str_rec_filename, filename ? filename : Py_None);
  PyObject *line_o = PyLong_FromUnsignedLong((unsigned long) line);
  PyObject *col_o = PyLong_FromUnsignedLong((unsigned long) col);
  PyObject *pc_o = PyLong_FromUnsignedLong((unsigned long) pc);
  if (line_o && col_o && pc_o) {
    PyDict_SetItem(meta, str_rec_line, line_o);
    PyDict_SetItem(meta, str_rec_col, col_o);
    PyDict_SetItem(meta, str_rec_pc, pc_o);
  }
  Py_XDECREF(line_o);
  Py_XDECREF(col_o);
  Py_XDECREF(pc_o);
  PyDict_SetItem(meta, str_rec_crossed, crossed ? Py_True : Py_False);
  {
    PyObject *id_o = PyLong_FromUnsignedLong(
        (unsigned long) kos_dynnode_id((KosDynNode *) frame));
    if (id_o) {
      PyDict_SetItem(meta, str_rec_frame, id_o);
      Py_DECREF(id_o);
    }
  }
  return meta;
}


/**
   1 when record already holds an entry for frame: a handler's entry
   recorded the frame when it caught the exception, and a re-raise
   out of that handler crosses the same frame. Python keeps the
   original entry for a re-raised exception too.
*/
static int dynchain_record_has_frame(PyObject *record, KosDynFrame *frame) {

  uint32_t id = kos_dynnode_id((KosDynNode *) frame);

  for (Py_ssize_t i = 0; i < PyList_GET_SIZE(record); i++) {
    PyObject *entry = PyList_GET_ITEM(record, i);
    PyObject *id_o;

    if (! PyDict_Check(entry))
      continue;
    id_o = PyDict_GetItemWithError(entry, str_rec_frame);
    if (id_o && PyLong_AsUnsignedLong(id_o) == id)
      return 1;
    if (! id_o && PyErr_Occurred())
      PyErr_Clear();
  }
  return 0;
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
    PyObject *prior = NULL;

    if (PyObject_GetOptionalAttr(exc_val, str_dynchain, &prior) < 0) {
      PyErr_Clear();
    } else if (prior) {
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
    for (KosDynNode *node = KosContext_DYN_TOP(ctx); node;
         node = KosDynNode_PARENT(node)) {
      if (! KosDynNode_IS_FRAME(node)) {
        continue;
      }
      if (dynchain_record_has_frame(frames, (KosDynFrame *) node))
        continue;
      PyObject *meta = dynchain_frame_meta((KosDynFrame *) node, 0);
      if (! meta) {
        PyErr_Clear();
        continue;
      }
      PyList_Append(frames, meta);
      Py_DECREF(meta);
    }
  }

  if (PyObject_SetAttr(exc_val, str_dynchain, frames) < 0) {
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
void kos_eval_note_crossed_frame(KosContext *ctx) {

  PyObject *typ = NULL, *val = NULL, *tb = NULL;

  if (! ctx || ! KosContext_FRAME(ctx) || ! PyErr_Occurred())
    return;

  KosContext_FlushLiveRegsToFrame(ctx);
  PyErr_Fetch(&typ, &val, &tb);
  PyErr_NormalizeException(&typ, &val, &tb);

  if (val && PyExceptionInstance_Check(val)) {
    PyObject *frames = NULL;
    int fresh = 0;

    if (PyObject_GetOptionalAttr(val, str_dynchain, &frames) < 0)
      PyErr_Clear();
    if (frames && ! PyList_Check(frames))
      Py_CLEAR(frames);
    if (! frames) {
      frames = PyList_New(0);
      fresh = 1;
    }

    if (! frames) {
      PyErr_Clear();
    } else if (dynchain_record_has_frame(frames, KosContext_FRAME(ctx))) {
      /* a handler in this frame recorded it when it caught the
         exception; the re-raise crossing it adds nothing */
      Py_DECREF(frames);
    } else {
      PyObject *meta = dynchain_frame_meta(KosContext_FRAME(ctx), 1);
      if (meta) {
        PyList_Append(frames, meta);
        Py_DECREF(meta);
        /* a list the exception already held was appended in place */
        if (fresh && PyObject_SetAttr(val, str_dynchain, frames) < 0)
          PyErr_Clear();
      } else {
        PyErr_Clear();
      }
      Py_DECREF(frames);
    }
  }

  PyErr_Restore(typ, val, tb);
}


/**
   One traceback entry per recorded frame: an empty code object naming
   the file, the function and the line the frame was executing, a
   frame over it, and PyTraceBack_Here. Entries go in innermost first,
   and each goes in ahead of the ones before, so the printed order is
   the outermost frame first, as for Python frames. A record entry
   with no file (the code-less root of a demand drive) adds nothing.
   The frame's globals carry the whole record as __koskari_trace__.
   The extended chain is attached to the value as its __traceback__.
*/
static PyFrameObject *frame_for_record(PyObject *entry, PyObject *record) {

  PyObject *filename = PyDict_GetItemString(entry, "filename");
  PyObject *name = PyDict_GetItemString(entry, "name");
  PyObject *line_o = PyDict_GetItemString(entry, "line");
  if (! filename || ! PyUnicode_Check(filename) || ! line_o)
    return NULL;
  if (! name || ! PyUnicode_Check(name))
    name = Py_None;

  PyCodeObject *code = PyCode_NewEmpty(
      PyUnicode_AsUTF8(filename),
      name == Py_None ? "<module>" : PyUnicode_AsUTF8(name),
      (int) PyLong_AsLong(line_o));
  if (! code)
    return NULL;

  PyObject *globals = PyDict_New();
  if (! globals) {
    Py_DECREF(code);
    return NULL;
  }
  PyObject *modname = PyUnicode_FromString("koskari.native");
  if (modname) {
    PyDict_SetItemString(globals, "__name__", modname);
    Py_DECREF(modname);
  }
  PyDict_SetItemString(globals, "__koskari_trace__", record);

  PyFrameObject *pyframe = PyFrame_New(PyThreadState_Get(), code, globals,
                                       NULL);
  Py_DECREF(globals);
  Py_DECREF(code);
  return pyframe;
}


void kos_eval_inject_recorded_frames(
    PyObject **exc_type,
    PyObject **exc_val,
    PyObject **exc_tb) {

  PyObject *record;

  if (! exc_val || ! *exc_val)
    return;
  record = PyObject_GetAttrString(*exc_val, "__koskari_dynchain__");
  if (! record) {
    PyErr_Clear();
    return;
  }
  if (! PyList_Check(record)) {
    Py_DECREF(record);
    return;
  }

  for (Py_ssize_t i = 0; i < PyList_GET_SIZE(record); i++) {
    PyObject *entry = PyList_GET_ITEM(record, i);
    PyFrameObject *pyframe;

    if (! PyDict_Check(entry)
        || PyDict_GetItemString(entry, "traced") == Py_True)
      continue;
    PyDict_SetItemString(entry, "traced", Py_True);

    /* the triple stays in hand while the frame is built, so a failure
       there cannot displace the exception being decorated */
    pyframe = frame_for_record(entry, record);
    if (! pyframe) {
      PyErr_Clear();
      continue;
    }

    PyErr_Restore(*exc_type, *exc_val, *exc_tb);
    if (PyTraceBack_Here(pyframe) < 0)
      PyErr_Clear();
    Py_DECREF(pyframe);
    PyErr_Fetch(exc_type, exc_val, exc_tb);
  }
  Py_DECREF(record);

  /* PyTraceBack_Here extends the thread-state traceback only. The
     value's __traceback__, which a handler and coro.c's PyErr_SetObject
     read, is attached here; before 3.12 PyErr_Restore leaves it alone. */
  if (*exc_tb && PyExceptionInstance_Check(*exc_val)
      && PyObject_SetAttrString(*exc_val, "__traceback__", *exc_tb) < 0)
    PyErr_Clear();
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
  kos_eval_inject_recorded_frames(exc_type, exc_val, exc_tb);
}


static void kos_eval_mark_context_dead(KosContext *ctx) {

  KosContext_FLAGS(ctx) &= ~KOS_CTX_RUNNING;
  KosContext_FLAGS(ctx) |= KOS_CTX_DEAD;
}


/**
   Inject traceback into the current PyErr in-place, leave PyErr set, and
   mark the context dead.  Call only when PyErr is set.  Flushes live
   pc/acting_pc onto the top frame first so traceback sees the current
   instruction.

   An exact GeneratorExit is not decorated. It is the close protocol's
   signal: every close drive throws one in and clears whatever comes
   back, so the dynchain record, the faux frame, and the traceback entry
   would be built for an exception discarded on the next line, once per
   abandoned generator. A subclass, or any exception a finally raises in
   its place, is a different type and is decorated as usual.
*/
void kos_eval_prepare_boundary_raise(KosContext *ctx) {

  PyObject *typ = NULL, *val = NULL, *tb = NULL;

  KosContext_FlushLiveRegsToFrame(ctx);
  PyErr_Fetch(&typ, &val, &tb);
  if (typ != PyExc_GeneratorExit)
    inject_eval_frame_traceback(ctx, &typ, &val, &tb);
  PyErr_Restore(typ, val, tb);
  kos_eval_mark_context_dead(ctx);
  ctx->terminal = 1;
}


/**
   Intercept a KoskariContinuationApply exception and install the appropriate
   transfer plan.  PyErr must be set to KoskariContinuationApply on entry.

   Own-token path: the continuation targets this eval context.  Extract the
   continuation and resume value, clear PyErr, and call
   kos_invoke_continuation to push a transfer goal.

   Foreign-token path: the continuation targets a different eval context.
   Fetch and clear PyErr and push a cc-unwind goal that leaves every local
   extent down to the context's root and then delivers the exception as
   a boundary raise, so the eval loop exits with KOS_RES_RAISE and the
   exception propagates upward to the owning context.

   :return: EVAL_REPROLOGUE on success, EVAL_RAISE on error
*/
static KosOpResult handle_continuation_apply(KosContext *ctx) {

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

    if (kos_invoke_continuation(ctx, k, resume_val, cont_obj) == 0) {
      Py_DECREF(resume_val);
      Py_DECREF(args_obj);
      Py_DECREF(typ);
      Py_DECREF(val);
      Py_DECREF(cont_obj);
      return EVAL_REPROLOGUE();
    }
    kos_xfer_goal_abandon_all(ctx);
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
    KosDynNode *seam = KosContext_DYN_TOP(ctx);
    KosXferGoal *goal;

    while (seam && KosDynNode_PARENT(seam))
      seam = KosDynNode_PARENT(seam);
    if (seam && ! KosDynNode_IS_GEN(seam))
      seam = NULL;

    goal = kos_xfer_goal_new(NULL, seam, val, (uint32_t) -1, NULL, 1);
    Py_DECREF(typ);
    Py_DECREF(val);
    if (! goal)
      return EVAL_RAISE();
    kos_xfer_goal_push(ctx, goal);
  }
  return EVAL_REPROLOGUE();
}


/**
   Unified exception ingress (the RAISE hub).  All PyErr / KOS_RES_RAISE
   ingress converges here: post-opcode results (apply_kos_op_result),
   the prologue's pending PyErr (kos_eval_prologue),
   revive-throw (eval_phase_entry KOS_MODE_EXCEPTION), and transfer goal
   step failures (kos_eval_drain_goal).  One deliberate non-ingress
   carve-out lives in kos_eval_drain_goal: a foreign cc-unwind's boundary
   raise (signal delivery, not a failed transfer; must not see try_local).

   Ordering contract:
     1. guard        - no PyErr: SystemError + prepare_boundary_raise,
                       EVAL_RAISE
     2. cc test      - PyErr_ExceptionMatches(KoskariContinuationApply),
                       exactly once
     3. nest abandon - kos_xfer_goal_abandon_all when a goal is active
                       AND the exception is not a continuation signal
     4. cc dispatch  - continuation signal: handle_continuation_apply
                       (own token: invoke; foreign: cc-unwind goal);
                       REPROLOGUE re-enters the loop with a goal active
     5. try_local    - kos_dyn_unwind_raise walks TRY / FINALLY /
                       WITH / unwind extents
     6. outcome map  - REPROLOGUE: handler applied, restart prologue.
                       AWAIT: async unwind started; tag KOS_SUSP_AWAIT,
                       mark suspended, propagate awaitable (caller owns
                       tl.val).
                       RAISE: propagate; kos_eval_prepare_boundary_raise
                       already ran inside try_local - never call it again
                       (archive boundary-control-reemit.md invariant 7.4).
                       FATAL: mark context dead, EVAL_FATAL.

   :return: EVAL_REPROLOGUE to continue, EVAL_AWAIT on async unwind,
            EVAL_RAISE to propagate (PyErr set), EVAL_FATAL on internal
            fault, or the applied return's own result (EVAL_RETURN at
            the drive root)
*/
static KosOpResult process_raise(KosContext *ctx) {

  if (! PyErr_Occurred()) {
    PyErr_SetString(PyExc_SystemError,
                    "process_raise: no exception set");
    kos_eval_prepare_boundary_raise(ctx);
    return EVAL_RAISE();
  }

  /* The raise supersedes any staged delivery: the frame it was staged
     for may be popped or replaced by a transfer. Every ingress into
     the raise machinery passes here, so this is the one place the slot
     drops. */
  Py_CLEAR(KosContext_PENDING_VALUE(ctx));

  int is_cc_apply = (KoskariContinuationApply
                     && PyErr_ExceptionMatches(KoskariContinuationApply));

  if (! is_cc_apply && KosContext_GOAL(ctx))
    kos_xfer_goal_abandon_all(ctx);

  if (is_cc_apply)
    return handle_continuation_apply(ctx);

  /* The live frame's resume gets the failure before the walk does,
     whatever the frame: a resume is a halted instruction awaiting its
     delivery, and the failure is that delivery. It is delivered
     through the prologue rather than called from inside the unwind,
     against a settled chain where the resume may thread on. Every
     pre-walk step above has already run -- the goal stack is
     abandoned, so a transfer in flight cannot outlive the failure the
     resume is about to handle. Bouncing rather than delivering here is
     what puts every raise ingress on that one path: this entry is
     reached straight from apply_kos_op_result, never through the
     prologue. It terminates because the delivery clears the resume;
     the walk bounces back here the same way each time a frame pop
     reveals a frame with a resume. */
  if (KosContext_FRAME(ctx)
      && KosDynFrame_RESUME_KIND(KosContext_FRAME(ctx)))
    return EVAL_REPROLOGUE();

  {
    KosOpResult tl = kos_dyn_unwind_raise(ctx);
    switch (tl.kind) {
    case KOS_RES_REPROLOGUE:
      return EVAL_REPROLOGUE();
    case KOS_RES_AWAIT:
      KosContext_SetSuspensionEvent(ctx, KOS_SUSP_AWAIT);
      KosContext_MarkSuspendedNotRunning(ctx);
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
    KosContext *ctx,
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


#include "eval/eval_context.c"


/* === Frame namespace helpers (for VAR_* opcodes) === */


/**
   Deref name in the current frame namespace. New reference or NULL.
*/
static PyObject *frame_ns_deref(KosContext *ctx, PyObject *name) {
  PyObject *ns = KosContext_CurrentNamespace(ctx);

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
static int frame_ns_store(KosContext *ctx, PyObject *name, PyObject *val) {
  PyObject *ns = KosContext_CurrentNamespace(ctx);

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
  PyObject *ns = KosContext_CurrentNamespace(ctx);

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
  PyObject *ns = KosContext_CurrentNamespace(ctx);
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
static PyObject *frame_ns_take(KosContext *ctx, PyObject *name) {
  PyObject *ns = KosContext_CurrentNamespace(ctx);
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
static PyObject *frame_ns_ref(KosContext *ctx, PyObject *name) {
  PyObject *ns = KosContext_CurrentNamespace(ctx);

  if (! KosNamespace_Check(ns)) {
    PyErr_Format(PyExc_TypeError,
                 "frame namespace must be Namespace for VAR_REF, got %R", ns);
    return NULL;
  }

  return KosNamespace_ClaimRef(ns, name);
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

   A bare relative import (dots alone) names the enclosing package at
   that level: the compiler emits None for it, and the import machinery
   takes the empty string, as Python's own "from .. import x" passes.

   :param ctx: context (supplies frame globals for relative anchors)
   :param name: module name str, or Py_None for a bare relative import
   :param level: number of leading dots; 0 = absolute
   :param leaf: non-zero to return the leaf module; zero for the root
   :return: new module reference, or NULL with error set
*/
static PyObject *kos_import_module(
    KosContext *ctx,
    PyObject *name,
    long level,
    int leaf) {

  PyObject *fromlist = leaf ? _import_fromlist : NULL;
  PyObject *globals_dict = NULL;

  if (name == Py_None)
    name = level > 0 ? str_empty : NULL;

  if (level > 0) {
    PyObject *globals_ns = KosContext_GlobalsNamespace(ctx);

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


static KosOpResult kos_await_dispatch(KosContext *ctx, PyObject *value) {
  return push_or_await(ctx, value, 1, 0);
}


/* === NEXT, ITER_NEXT === */


/**
   Route a walk's answer into the evaluator: a RAISE enters the raise
   ingress so a local handler can take it, an AWAIT is tagged for the
   driver, and everything else passes. Applied once, at the site that
   consumes a walk's raw result; a result that has already been through
   here must not come back.
*/
static KosOpResult kos_eval_walk_outcome(KosContext *ctx, KosOpResult r) {
  if (r.kind == KOS_RES_RAISE) {
    KosOpResult pr;
    KOS_TRACE_POST_OP_WRAP(event_raise, pr = process_raise(ctx));
    return pr;
  }
  if (r.kind == KOS_RES_AWAIT)
    return eval_event_await(ctx, r.val);
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
            staged); EVAL_REPROLOGUE when the invoke site is a
            code-less resume frame; EVAL_RAISE on error
*/
static KosOpResult kos_eval_route_delim_return(
    KosContext *ctx,
    KosDynDelim *ret_delim,
    PyObject *val) {

  SAYF("[eval] route_delim_return: unwinding to return frame=%p node=%p",
       (void *) KosDynDelim_RETURN_FRAME(ret_delim),
       (void *) KosDynDelim_RETURN_NODE(ret_delim));

  {
    KosOpResult wr;
    KosDynNode *pending = NULL;
    wr = kos_dyn_unwind_to_node(ctx, KosDynDelim_RETURN_NODE(ret_delim),
                                KOS_XFER_EVENT_SHIFT, &pending);
    if (wr.kind == KOS_RES_REPROLOGUE || wr.kind == KOS_RES_AWAIT) {
      /* a leave went into a thunk or onto an awaitable; val is stored on
         that node and re-enters this route once the node has finished */
      if (kos_dyn_ob_store_return(pending, val, KOS_PENDING_RETURN_DELIM,
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
       (void *) KosContext_CurrentNode(ctx),
       KosContext_CurrentNode(ctx)
           ? kos_dynnode_kind_name(
                 KosDynNode_KIND(KosContext_CurrentNode(ctx)))
           : "NULL");

  if (KosDynDelim_MODE(ret_delim) == KOS_DYN_DELIM_MODE_INPLACE) {
    if (kos_dyn_finish_inplace_pop_return(ctx, ret_delim, val) < 0)
      return EVAL_RAISE();
  } else if (kos_dyn_finish_delimited_pop_return(ctx, ret_delim, val) < 0) {
    return EVAL_RAISE();
  }

  /* Both finishes leave val on the invoking frame's stack. A code-less
     invoker (a resume frame that called the continuation) has no pc to
     continue at: the prologue delivers the value on its stack to the
     resume set on it, the way a callee's return reaches it. */
  if (! KosContext_CODE(ctx))
    return EVAL_REPROLOGUE();
  return EVAL_NORMAL();
}


/*
  Includes the implementation of all opcodes from the opcode/ directory.
*/
/* The THEEND barrier handler reports the same fault as an off-the-end fetch;
   the reporter is defined below (with the dispatch loop). */
static void kos_eval_report_pc_bounds_error(KosContext *ctx);
/* Every handler has the KosThreadFn prototype: insn is the handler's
   own KosOp (read through KosOp_ARG, KosOp_OBJ, KosOp_ROW, KosOp_SLOT),
   and the delivery
   argument is unused by an opcode.  The one shape lets an instruction's
   fn pointer reach any handler, and lets a handler be a musttail
   target.  Each handler is its own function, reached only through the
   instructions that name it. */
#define KOS_OP_HANDLER(name)                                            \
  static KOS_PRESERVE_NONE KosOpResult op_##name(                       \
      KosContext *ctx, void *insn, PyObject *kos_op_delivery_)

#define CURRENT_NS() KosContext_NAMESPACE(ctx)

/* Namespace-presence guard for opcode handlers.  ctx->namespace is a live
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
   plain return landing on its caller). The delivery
   left a plain bytecode frame current with nothing staged, no error,
   and no goal active: DISPATCH, so the trampoline threads from ctx->pc
   without a prologue pass, and a handler that ran the delivery itself
   threads on directly. Anything else (a value staged for a resume, a
   codeless frame, a plan) is the prologue's to deliver.
*/
static inline KosOpResult kos_eval_delivery_done(KosContext *ctx) {
  KosDynFrame *frame = KosContext_FRAME(ctx);

  if (likely(frame
             && KosDynFrame_CODE(frame)
             && ! KosDynFrame_RESUME_KIND(frame)
             && ! KosContext_PENDING_VALUE(ctx)
             && ! KosContext_GOAL(ctx)
             && ! PyErr_Occurred()))
    return EVAL_DISPATCH();
  return EVAL_REPROLOGUE();
}


#include "eval/eval_electors.c"
#include "eval/eval_item.c"

/* The return arms op_RETURN threads through directly (defined with the
   return machinery below; the trampoline's own fast arm uses the same
   functions for every other producer of a RETURN signal). */
static inline int kos_return_is_plain(
    KosContext *ctx,
    KosDynNode **out_child,
    KosDynNode **out_parent);
KOS_OP_INLINE int kos_return_is_region(
    KosContext *ctx,
    KosDynNode **out_child,
    KosDynNode **out_parent,
    KosDynFrame **out_ft);
KOS_OP_INLINE KosOpResult eval_plain_return(
    KosContext *ctx,
    KosDynNode *child,
    KosDynNode *parent,
    PyObject *val);
KOS_OP_INLINE KosOpResult eval_region_return(
    KosContext *ctx,
    KosDynNode *child,
    KosDynNode *parent,
    KosDynFrame *ft,
    PyObject *val);
KOS_OP_INLINE int kos_return_is_resume(
    KosContext *ctx,
    KosDynNode **out_parent);
static int kos_return_take_resume(
    KosContext *ctx,
    KosDynNode *parent,
    PyObject *val,
    const KosResumeKind **out_resume,
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
    KosContext *ctx,
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
    kwd = KosContext_StackPopRef(ctx);
    if (! kwd)
      goto done;
  }

  if (opa & 1) {
    pos = KosContext_StackPopRef(ctx);
    if (! pos)
      goto done;
  }

  if (! subject) {
    subject = KosContext_StackPopRef(ctx);
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

  /* The keywords are copied into a fresh plain dict, as Python's
     f(**d) does: the callee reads it raw, and the copy holds the keys
     and values for the call whatever the caller's mapping does
     meanwhile -- binding a keyword by name compares the keys, and a
     key's __eq__ may mutate the mapping it came from. Keys of any
     type pass, as the dict literal's splat is this call. */
  if (kwd) {
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

  res = kos_call_push_args(ctx, callable, args, kwd, tail);

 done:
  Py_XDECREF(kwd);
  Py_XDECREF(pos);
  Py_XDECREF(subject);
  Py_XDECREF(callable);
  Py_XDECREF(args);
  return res;
}


/**
   The delivery of RETURN: val is the owned return value.  The three
   shapes that need no walk thread straight on: a plain bytecode parent
   takes the value on its stack and continues at its saved pc (the
   bytecode frame's own resume); a region over such a parent takes the
   value on its segment and reacts through its control-returned hook
   if it has one; a parent waiting on a resume takes the value by a
   tail call into that resume.  Anything else -- an extent above the
   returning frame, a generator or a receiving iteration-node parent,
   a missing retarget cache, an unsettled value -- signals RETURN and
   the trampoline's return machinery walks it.  Reached by tail call
   from every RETURN row, so it carries the KosThreadFn prototype; insn
   is unused.
*/
KOS_PRESERVE_NONE KosOpResult kos_return_deliver(
    KosContext *ctx,
    void *insn,
    PyObject *val) {

  KosDynNode *child, *parent;
  KosDynFrame *ft;
  KosOpResult r;

  (void) insn;

  if (likely(kos_return_is_plain(ctx, &child, &parent))) {
    KOS_TRACE_POST_OP_WRAP(plain_return,
        r = eval_plain_return(ctx, child, parent, val));
    if (likely(r.kind == KOS_RES_DISPATCH))
      RESULT_CONTINUE(ctx);
    if (r.kind == KOS_RES_REPROLOGUE)
      TAILCALL(kos_eval_prologue, ctx, NULL, NULL);
    return r;
  }

  if (kos_return_is_region(ctx, &child, &parent, &ft)) {
    KOS_TRACE_POST_OP_WRAP(plain_return,
        r = eval_region_return(ctx, child, parent, ft, val));
    if (likely(r.kind == KOS_RES_DISPATCH))
      RESULT_CONTINUE(ctx);
    if (r.kind == KOS_RES_REPROLOGUE)
      TAILCALL(kos_eval_prologue, ctx, NULL, NULL);
    return r;
  }

  if (kos_return_is_resume(ctx, &parent)) {
    const KosResumeKind *resume;
    void *state;
    int took = kos_return_take_resume(ctx, parent, val, &resume, &state);

    if (took > 0)
      RESUME_HOP(resume, ctx, state, val);
    if (unlikely(took < 0))
      RESULT_RAISE();
  }

  /* The general path: the walk over the extents above the frame, the
     generator and iteration-node parents, the delimited return, the
     root. Its own plain case is the transition above. The arms above
     are the RETURN row's own work; from here on is the signal's. */
  KOS_TRACE_TRAMPOLINE_STEP();
  KOS_TRACE_POST_OP_WRAP(return_event, r = eval_event_return(ctx, val));
  if (likely(r.kind == KOS_RES_DISPATCH))
    RESULT_CONTINUE(ctx);
  if (r.kind == KOS_RES_RESUME)
    TAILCALL(kos_eval_relay, ctx, (void *) (uintptr_t) KOS_RES_RESUME, r.val);
  if (r.kind == KOS_RES_REPROLOGUE)
    TAILCALL(kos_eval_prologue, ctx, NULL, NULL);
  return r;
}


/**
   The delivery of YIELD: val is the owned yielded value.  A flattened
   generator delivers to its consumer here and threads into it: the
   same freeze and delivery the trampoline would run for the YIELD
   signal, minus the trip.  A generator an adapter drives surfaces the
   value as the terminal YIELD.  Reached by tail call from every YIELD
   row; insn is unused.
*/
KOS_PRESERVE_NONE KosOpResult kos_yield_deliver(
    KosContext *ctx,
    void *insn,
    PyObject *val) {

  KosDynGen *gen = kos_dyn_find_gen(ctx);
  KosOpResult r;
  int flattened;

  (void) insn;

  if (unlikely(! gen)) {
    Py_DECREF(val);
    PyErr_SetString(PyExc_RuntimeError,
                    "yield outside generator: no generator node on chain");
    return EVAL_FATAL();
  }

  /* The freeze, then the delivery to a flattened consumer, or the
     terminal YIELD for a generator an adapter runs. The flattened
     delivery is the YIELD row's own work; the terminal yield is the
     signal's. Read before the freeze, which detaches the GEN from its
     consumer. */
  flattened = KosDynGen_IS_FLATTENED(gen);
  if (! flattened)
    KOS_TRACE_TRAMPOLINE_STEP();
  KOS_TRACE_POST_OP_WRAP(event_yield, r = eval_event_yield(ctx, gen, val));
  if (likely(r.kind == KOS_RES_RESUME))
    TAILCALL(kos_eval_relay, ctx, (void *) (uintptr_t) KOS_RES_RESUME, r.val);
  if (unlikely(r.kind == KOS_RES_YIELD && flattened)) {
    /* The freeze found no consumer for a generator that had one:
       never hand the trampoline a YIELD it would deliver again. */
    Py_DECREF(r.val);
    ctx->terminal = 0;
    PyErr_SetString(PyExc_SystemError,
                    "flattened generator yield found no consumer");
    RESULT_RAISE();
  }
  if (r.kind == KOS_RES_REPROLOGUE)
    TAILCALL(kos_eval_prologue, ctx, NULL, NULL);
  return r;
}


/* The poll itself: what Python's own loop does at a cooperation point,
   in the order it does it. Another thread waiting on the GIL has
   already outlasted the interpreter's switch interval, so yield it
   now; then take any pending signal and pending call.
   Py_MakePendingCalls is the public entry that runs the interpreter's
   own signal handling, which is what clears the breaker's signal
   input: PyErr_CheckSignals alone would leave it up and every later
   cooperation point polling. A signal it raises is left set for the
   prologue's pending-exception step, which drops any staged delivery
   before the raise walk.

   The cyclic collector differs by version. Through 3.11 the allocator
   runs a scheduled collection itself. From 3.12 the interpreter only
   schedules it, on the breaker, and runs it from its own loop or from
   PyErr_CheckSignals, whose first act is to run a scheduled collection
   on this thread (and, on 3.12, to recompute the aggregate word, which
   otherwise stays up and keeps every cooperation point polling). The
   signals were taken just above, so that call finds none; it is
   skipped when a handler already raised, so nothing runs on top of
   the pending exception. */
static void kos_eval_poll_now(void) {
  if (koskari_gil_drop_requested()) {
    Py_BEGIN_ALLOW_THREADS
    Py_END_ALLOW_THREADS
  }
  Py_MakePendingCalls();

#if PY_MINOR_VERSION >= 12
  if (! PyErr_Occurred())
    PyErr_CheckSignals();
#endif
}


KOS_PRESERVE_NONE KosOpResult kos_eval_poll(
    KosContext *ctx,
    void *insn,
    PyObject *val) {

  (void) insn;
  (void) val;

  KOS_TRACE_TRAMPOLINE_STEP();
  kos_eval_poll_now();
  if (unlikely(PyErr_Occurred()))
    TAILCALL(kos_eval_prologue, ctx, NULL, NULL);
  KOS_THREAD_NEXT(ctx);
}


KOS_PRESERVE_NONE KosOpResult kos_eval_raise(
    KosContext *ctx,
    void *insn,
    PyObject *val) {

  KosOpResult r;

  (void) insn;
  (void) val;

  KOS_TRACE_TRAMPOLINE_STEP();
  KOS_TRACE_POST_OP_WRAP(event_raise, r = process_raise(ctx));
  if (r.kind == KOS_RES_REPROLOGUE)
    TAILCALL(kos_eval_prologue, ctx, NULL, NULL);
  return r;
}


KOS_PRESERVE_NONE KosOpResult kos_eval_await(
    KosContext *ctx,
    void *insn,
    PyObject *val) {

  KosOpResult r;

  (void) insn;

  KOS_TRACE_TRAMPOLINE_STEP();
  KOS_TRACE_POST_OP_WRAP(event_await, r = eval_event_await(ctx, val));
  if (r.kind == KOS_RES_REPROLOGUE)
    TAILCALL(kos_eval_prologue, ctx, NULL, NULL);
  return r;
}


KOS_PRESERVE_NONE KosOpResult kos_eval_control_returned(
    KosContext *ctx,
    void *insn,
    PyObject *val) {

  KosOpResult r;

  (void) insn;
  (void) val;

  KOS_TRACE_TRAMPOLINE_STEP();
  r = kos_eval_post_opcode_control_returned_after_hook(ctx);
  if (likely(r.kind == KOS_RES_DISPATCH))
    RESULT_CONTINUE(ctx);
  if (r.kind == KOS_RES_REPROLOGUE)
    TAILCALL(kos_eval_prologue, ctx, NULL, NULL);
  return r;
}


KOS_PRESERVE_NONE KosOpResult kos_eval_relay(
    KosContext *ctx,
    void *kind,
    PyObject *val) {

  switch ((KosOpResultKind) (uintptr_t) kind) {
  case KOS_RES_POLL:
    TAILCALL(kos_eval_poll, ctx, NULL, NULL);
  case KOS_RES_RAISE:
    TAILCALL(kos_eval_raise, ctx, NULL, NULL);
  case KOS_RES_AWAIT:
    TAILCALL(kos_eval_await, ctx, NULL, val);
  case KOS_RES_RETURN:
    TAILCALL(kos_return_deliver, ctx, NULL, val);
  case KOS_RES_YIELD:
    TAILCALL(kos_yield_deliver, ctx, NULL, val);
  case KOS_RES_CONTROL_RETURNED:
    TAILCALL(kos_eval_control_returned, ctx, NULL, NULL);
  case KOS_RES_RESUME: {
    /* The live frame's resume takes the delivery by tail call: the
       prologue's slim bounce without the pass. A delivery already
       staged for the frame is the value; a kind that settles its
       delivery goes through the prologue, which settles it. */
    KosDynFrame *frame = KosContext_FRAME(ctx);
    const KosResumeKind *resume = frame ? KosDynFrame_RESUME_KIND(frame) : NULL;
    void *state;

    if (unlikely(! resume)) {
      Py_XDECREF(val);
      PyErr_SetString(PyExc_SystemError,
                      "RESUME relay with no resume on the live frame");
      return EVAL_FATAL();
    }
    if (! val && KosContext_PENDING_VALUE(ctx)) {
      val = KosContext_PENDING_VALUE(ctx);
      KosContext_PENDING_VALUE(ctx) = NULL;
    }
    if (resume->settle && val) {
      KosContext_PENDING_VALUE(ctx) = val;
      TAILCALL(kos_eval_prologue, ctx, NULL, NULL);
    }
    state = KosDynFrame_RESUME_STATE(frame);
    KosDynFrame_RESUME_KIND(frame) = NULL;
    KosDynFrame_RESUME_STATE(frame) = NULL;
    RESUME_HOP(resume, ctx, state, val);
  }
  case KOS_RES_DISPATCH:
  case KOS_RES_NORMAL:
    KOS_THREAD_NEXT(ctx);
  case KOS_RES_REPROLOGUE:
    TAILCALL(kos_eval_prologue, ctx, NULL, NULL);
  case KOS_RES_FATAL:
  default:
    return EVAL_FATAL();
  }
}


#include "handlers.incl"
#undef CURRENT_NS
#undef KOS_OP_REQUIRE_NS
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
   Complete a native frame by handing value to the eval loop's return
   machinery, which owns every delivery terminus (parent frame,
   step resume, drive root). Clears the program slot and, when cache
   is given, flags the frame to be cached there when the return path
   frees it (KOS_FRAME_RETURN_TO_CACHE; native_state becomes the cache
   slot). The caller returns this result from its handler. value is
   consumed.
*/
KosOpResult KosNativeFrame_ReturnToCache(
    KosContext *ctx,
    PyObject *value,
    KosDynFrame **cache) {

  KosDynFrame *frame = KosContext_FRAME(ctx);

  if (cache) {
    KosDynFrame_CACHE_SLOT(frame) = (void *) cache;
    KosDynFrame_FLAGS(frame) |= KOS_FRAME_RETURN_TO_CACHE;
  } else {
    KosDynFrame_CACHE_SLOT(frame) = NULL;
  }

  return EVAL_RETURN(value);
}


/**
   Push a code-less frame, reusing the cached frame when one
   is available. Either way the frame gets an owned fill of the live
   namespace (kos_dyn_push_frame_native's contract): the cached frame
   was stripped of its fill when cached, so the reinstall attaches a fresh
   one before the push publishes the frame's namespace to the live
   register. KosResumeFrame_Install sets the resume kind on it.
*/
static KosDynFrame *kos_push_or_reuse_codeless_frame(
    KosContext *ctx,
    KosDynFrame **cache) {

  if (cache && *cache) {
    PyObject *empty = KosNamespace_Empty();
    KosDynFrame *frame = *cache;

    if (! empty)
      return NULL;
    if (kos_eval_check_frame_room(ctx, KosContext_FRAME(ctx), 1) < 0)
      return NULL;
    *cache = NULL;
    Py_INCREF(empty);
    KosDynFrame_NS(frame) = empty;
    kos_dyn_push_ready_frame(ctx, frame);
    return frame;
  }

  return kos_dyn_push_frame_native(ctx);
}


KosDynFrame *KosResumeFrame_Install(
    KosContext *ctx,
    const KosResumeKind *kind,
    void *state,
    KosDynFrame **cache) {

  KosDynFrame *frame = kos_push_or_reuse_codeless_frame(ctx, cache);
  if (! frame)
    return NULL;

  KosDynFrame_RESUME_KIND(frame) = kind;
  KosDynFrame_RESUME_STATE(frame) = state;
  return frame;
}


/**
   One next-element request against iter from inside a native frame
   handler: a plain iterator answers through tp_iternext alone; a
   provider steps through its suite, a python async iterator through
   its __anext__, with no node. A step that defers (the provider's
   frames installed, the async step awaiting) records nothing: the
   caller's own record holds iter, and the resume that takes the
   delivery finishes it. See eval.h for the per-status contract.
*/
KosIterRequestStatus KosNativeFrame_RequestNext(
    KosContext *ctx,
    PyObject *iter,
    PyObject **out) {

  KosIterRequestStatus status;
  KosIterSlots *islots;

  *out = NULL;

  if (kos_iter_next_plain(iter, out, &status))
    return status;

  if (KosIter_CheckSlots(iter, &islots)) {
    switch (kos_iter_step_provider(ctx, iter, islots, NULL, out)) {
      case KOS_ITER_VALUE:
        return KOS_IREQ_DELIVERED;
      case KOS_ITER_EXHAUSTED:
        Py_CLEAR(*out);
        return KOS_IREQ_EXHAUSTED;
      case KOS_ITER_INSTALLED:
      case KOS_ITER_INSTALLED_VALUE:
        return KOS_IREQ_CONTINUES;
      case KOS_ITER_ERROR:
      default:
        return KOS_IREQ_ERROR;
    }
  }

  {
    PyObject *value = NULL;
    PyObject *_await = NULL;

    switch (op_get_next_from_iter(ctx, &_await, iter, &value)) {
      case GET_NEXT_VALUE:
        *out = value;
        return KOS_IREQ_DELIVERED;
      case GET_NEXT_EXHAUSTED:
        return KOS_IREQ_EXHAUSTED;
      case GET_NEXT_SUSPEND:
        *out = _await;
        return KOS_IREQ_EXIT;
      case GET_NEXT_ERROR:
      default:
        return KOS_IREQ_ERROR;
    }
  }
}


KosOpResult KosContext_NextResume(
    KosContext *ctx,
    PyObject *iter,
    PyObject *send_val,
    const KosResumeKind *resume,
    void *state) {

  KosDynFrame *frame = KosContext_FRAME(ctx);
  PyObject *out = NULL;

  (void) send_val;  /* no caller sends into Next yet; matches the
                        design's future Send entry point */

  switch (KosNativeFrame_RequestNext(ctx, iter, &out)) {
    case KOS_IREQ_DELIVERED:
      return kos_entry_value(ctx, frame, out, resume, state);

    case KOS_IREQ_CONTINUES:
      /* The resume is set on the calling frame for the delivery; a
         frame the install left current is threaded into only when it
         can be: one suspended inside a yield-from carries that phase's
         resume, and the prologue has to run it before anything else. */
      KosDynFrame_RESUME_KIND(frame) = resume;
      KosDynFrame_RESUME_STATE(frame) = state;
      return kos_iter_installed(ctx);

    case KOS_IREQ_EXIT:
      KosDynFrame_RESUME_KIND(frame) = resume;
      KosDynFrame_RESUME_STATE(frame) = state;
      return EVAL_AWAIT(out);

    case KOS_IREQ_EXHAUSTED:
      /* Exhaustion reaches the resume the way an in-flight source's
         does and the way any failed delivery does: set on the frame
         with StopIteration pending, for the prologue to deliver as a
         NULL. */
      PyErr_SetNone(PyExc_StopIteration);
      KosDynFrame_RESUME_KIND(frame) = resume;
      KosDynFrame_RESUME_STATE(frame) = state;
      return EVAL_REPROLOGUE();

    case KOS_IREQ_ERROR:
    default:
      /* A request that failed before it deferred raises at the caller
         with no resume set: nothing is in flight for the resume to
         finish, and a provider refused as busy belongs to the step
         that holds it. */
      return EVAL_RAISE();
  }
}


/**
   Run revive_await and normalize its outcome for eval_phase_entry: on
   KOS_RES_AWAIT tag the suspension and return the terminal result;
   otherwise set *mode (EXCEPTION on RAISE, NORMAL otherwise) and return
   EVAL_NORMAL.
*/
static KosOpResult entry_revive_await(
    KosContext *ctx,
    PyObject *send_val,
    PyObject *throw_typ,
    PyObject *throw_exc,
    KosEvalMode *mode,
    KosObligation *out_return) {

  KosOpResult ra = revive_await(ctx, send_val, throw_typ, throw_exc,
                                out_return);

  if (ra.kind == KOS_RES_AWAIT) {
    KosContext_SetSuspensionEvent(ctx, KOS_SUSP_AWAIT);
    KosContext_MarkSuspendedNotRunning(ctx);
    return ra;
  }
  *mode = (ra.kind == KOS_RES_RAISE) ? KOS_MODE_EXCEPTION : KOS_MODE_NORMAL;
  return EVAL_NORMAL();
}


/**
   Phase 0: the entry call a Python-boundary entry recorded on a fresh
   context. Takes it off the context and installs it on this context.
   A call of a non-yielding exact KosFunction whose self-bind is
   resolved installs the callee as the root frame, carrying
   frame_flags, and its return ends the drive; the install binds any
   argument shape, keywords included. Any other call (another
   callable, a function's first call, a generator) is made through
   KosContext_Call from a code-less root frame carrying resume and
   frame_flags, taken from frame_cache when the slot holds one; the
   slot is also the resume's state, so the resume returns the frame to
   it. A value back in hand is staged for the resume, which the entry
   phase delivers. An outcome other than an install is applied as the
   trampoline would apply it. Out of line so koskari_eval's own frame
   carries none of it.

   :return: EVAL_NORMAL when eval should proceed to the entry phase;
            terminal KosOpResult when eval should stop.
*/
static KOS_NOINLINE KosOpResult eval_phase_entry_call(KosContext *ctx) {

  const KosEntryCall *call = KosContext_ENTRY_CALL(ctx);
  PyObject *callable = call->callable;
  KosOpResult r;

  KosContext_ENTRY_CALL(ctx) = NULL;

  if (Py_TYPE(callable) == KOS_EVALUATED_PYTYPE(KosFunctionType)
      && KosFunction_BIND_SELF_STATE(callable) != 0
      && KosFunction_CODE(callable)
      && ! KosCode_YIELDS((KosCode *) KosFunction_CODE(callable))) {

    /* The chain is empty, so the install inherits no demand bit; the
       recorded flags go onto the root frame it pushed. */
    r = KosFunction_Install(ctx, callable, call->args, call->nargs,
                            call->kwnames, 0);
    if (r.kind == KOS_RES_NORMAL || r.kind == KOS_RES_POLL) {
      KOS_TRACE_EVENTS_INC(KOS_TE_ENTRY_ROOT_INSTALL);
      KosDynFrame_FLAGS(KosContext_FRAME(ctx)) |= call->frame_flags;
      return EVAL_NORMAL();
    }

  } else {
    KosDynFrame *frame;
    int cached = call->frame_cache && *call->frame_cache;

    /* The root frame comes from the wrapper's slot when it holds one;
       the slot is the resume's state, so the resume returns the frame
       to it. The inner call is never a tail call: its return must
       reach this frame's resume. */
    frame = KosResumeFrame_Install(ctx, call->resume, call->frame_cache,
                                   call->frame_cache);
    if (! frame) {
      r = EVAL_RAISE();

    } else {
      if (cached)
        KOS_TRACE_EVENTS_INC(KOS_TE_ENTRY_ROOT_CACHED);
      KosDynFrame_FLAGS(frame) |= call->frame_flags;
      r = KosContext_Call(ctx, callable, call->args, call->nargs,
                          call->kwnames, 0, call->resume, call->frame_cache);
      if (r.kind == KOS_RES_RETURN) {
        KosContext_StageDelivery(ctx, r.val);
        r = EVAL_REPROLOGUE();
      }
      if (r.kind == KOS_RES_NORMAL || r.kind == KOS_RES_REPROLOGUE)
        return EVAL_NORMAL();
    }
  }

  r = apply_kos_op_result(ctx, r);
  if (r.kind == KOS_RES_DISPATCH || r.kind == KOS_RES_REPROLOGUE)
    return EVAL_NORMAL();
  return r;
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
    KosContext *ctx,
    PyObject *send_val,
    PyObject *throw_typ,
    PyObject *throw_exc,
    KosEvalMode *mode_out) {

  KosEvalMode mode = KOS_MODE_NORMAL;
  KosObligation resume_return;

  resume_return.kind = KOS_OB_NONE;
  if (send_val == NULL && throw_typ == NULL && throw_exc == NULL) {
    SAYF("[eval] phase_entry branch: send/throw all NULL, suspend=%d\n",
         KosContext_SuspensionEvent(ctx));
    switch (KosContext_SuspensionEvent(ctx)) {
      case KOS_SUSP_NONE:
        mode = KOS_MODE_NORMAL;
        break;
      case KOS_SUSP_AWAIT:
        if (KosContext_PythonAwaitIter(ctx)) {
          mode = KOS_MODE_NORMAL;
        } else {
          KosOpResult _ra = entry_revive_await(ctx, Py_None, NULL, NULL,
                                               &mode, &resume_return);
          if (_ra.kind == KOS_RES_AWAIT)
            return _ra;
        }
        break;
      case KOS_SUSP_YIELD:
        /* anext() passes NULL for send; treat as Py_None per async gen protocol */
        SAY("[eval] phase_entry -> revive_yield(Py_None)");
        mode = revive_yield(ctx, Py_None, throw_typ, throw_exc);
        break;
      default:
        mode = KOS_MODE_NORMAL;
        break;
    }

  } else {
    SAYF("[eval] phase_entry branch: send_val=%p throw_typ=%p throw_exc=%p "
         "suspend=%d\n",
         (void *) send_val, (void *) throw_typ, (void *) throw_exc,
         KosContext_SuspensionEvent(ctx));
    switch (KosContext_SuspensionEvent(ctx)) {
      case KOS_SUSP_YIELD:
        SAY("[eval] phase_entry -> revive_yield(send_val)");
        mode = revive_yield(ctx, send_val, throw_typ, throw_exc);
        break;
      case KOS_SUSP_AWAIT:
      default: {
        KosOpResult _ra = entry_revive_await(ctx, send_val, throw_typ,
                                             throw_exc, &mode,
                                             &resume_return);
        if (_ra.kind == KOS_RES_AWAIT)
          return _ra;
        break;
      }
    }
  }
  KosContext_ClearSuspensionEvent(ctx);

  if (mode == KOS_MODE_EXCEPTION) {
    KosOpResult pr = process_raise(ctx);
    SAYF("[eval] phase_entry process_raise kind=%d\n", (int) pr.kind);

    if (pr.kind == KOS_RES_REPROLOGUE) {
      *mode_out = KOS_MODE_NORMAL;
      return EVAL_NORMAL();
    }
    /* AWAIT (tagged by the hub) / RAISE / FATAL are terminal here */
    return pr;
  }

  /* The revive completed a node whose leave had a return, break, or
     continue stored on it (a wind whose after thunk tail-called the
     awaitable has no frame to return through). With that node gone,
     the transfer continues instead of the frame's bytecode. The answer has been through the ingress:
     a RAISE or an AWAIT is terminal, anything else goes on to dispatch. */
  if (mode == KOS_MODE_NORMAL && resume_return.kind == KOS_OB_RETURN) {
    KosOpResult rr = KosEval_ResumeReturn(ctx, &resume_return);
    switch (rr.kind) {
    case KOS_RES_NORMAL:
    case KOS_RES_REPROLOGUE:
    case KOS_RES_DISPATCH:
    case KOS_RES_POLL:
      *mode_out = KOS_MODE_NORMAL;
      return EVAL_NORMAL();
    default:
      return rr;
    }
  }

  /* A revive that found the top frame waiting on a resume staged the
     sent value in the pending slot; the trampoline's first prologue
     pass delivers it. */
  *mode_out = mode;
  return EVAL_NORMAL();
}


/**
   Phase 2: transfer goal drive.

   Drives the active transfer goal as far as the chain allows.  When no
   frame and no hook remain after draining, emits a terminal RETURN(None)
   and marks the context dead.  Abandonment outcomes from the unified
   RAISE ingress (AWAIT / RAISE / FATAL) are terminal here.

   :return: EVAL_NORMAL when the dispatch loop should start;
            terminal KosOpResult when eval should stop.
*/
static KosOpResult eval_phase_transfer(KosContext *ctx) {

  KosOpResult xfer = kos_eval_drain_goal(ctx);

  if (xfer.kind == KOS_RES_REPROLOGUE)
    return EVAL_NORMAL();
  if (xfer.kind != KOS_RES_NORMAL)
    return xfer;  /* AWAIT / RAISE / FATAL */

  if (! KosContext_FRAME(ctx)) {
    kos_eval_mark_context_dead(ctx);
    Py_INCREF(Py_None);
    ctx->terminal = 1;
    return EVAL_RETURN(Py_None);
  }

  return EVAL_NORMAL();
}


/**
   Centralized YIELD handler.  Flushes live registers to the frame, marks the
   generator dead-after-yield, freezes gen (the running frame's GEN, from
   kos_dyn_find_gen), delivers to the live frame's resume when there is
   one, and otherwise tags the suspension event.  val is a new
   reference; ownership transfers to the caller or consumer.

   A yield delivered to a flattened consumer answers RESUME: the
   consumer frame's resume takes the element through the relay, by
   tail call from the YIELD row's thread function. A yield with no
   consumer on the chain is the terminal EVAL_YIELD the adapters run.
*/
static KosOpResult eval_event_yield(
    KosContext *ctx,
    KosDynGen *gen,
    PyObject *val) {

  /* No flush here: the freeze's retarget flushes the outgoing frame's
     pcs when it switches to the consumer's. */
  SAYF("[eval] post_opcode KOS_RES_YIELD -> freeze gen=%p dyn_top=%p\n",
       (void *) gen, (void *) KosContext_CurrentNode(ctx));
  if (kos_dyn_gen_freeze(ctx, gen) < 0) {
    Py_DECREF(val);
    return EVAL_FATAL();
  }
  {
    /* The consumer is the live frame once the freeze has detached the
       GEN: its resume takes the element (a for-each step, a next
       request, a yield-from over a provider), else nobody, and the
       yield leaves the evaluator. Never a search outward: a frame
       waiting on its own step sits between the producer and any
       enclosing region. */
    if (iter_frame_has_resume(ctx))
      return iter_deliver_to_resume(ctx, val, 0);
  }
  /* Only a yield that stops the evaluator leaves the context not
     running; a delivered yield keeps threading, and the context stays
     RUNNING so the context's GC traverse keeps visiting the live state. */
  KosContext_MarkDeadAfterYield(ctx);
  KosContext_SetSuspensionEvent(ctx, KOS_SUSP_YIELD);
  ctx->terminal = 1;
  return EVAL_YIELD(val);
}


/**
   Centralized AWAIT handler.  Demand-checks, marks suspended, and tags the
   suspension event.  val ownership transfers on success, or is released on
   demand denial.
*/
static KosOpResult eval_event_await(KosContext *ctx, PyObject *val) {
  if (kos_eval_in_demand(ctx)) {
    Py_DECREF(val);
    KosContext_FLAGS(ctx) &= ~KOS_CTX_DEAD;
    KosContext_FLAGS(ctx) |= KOS_CTX_RUNNING;

    /* The eager step stored the __await__ iterator for a resumption
       that is now refused; nothing will drive it again. */
    KosContext_PythonAwaitClearIter(ctx);
    kos_demand_set_sync_violation(
        "async suspension is not allowed in demand_sync");
    KosOpResult rs;
    KOS_TRACE_POST_OP_WRAP(event_raise, rs = process_raise(ctx));
    return rs;
  }

  KosContext_MarkSuspendedNotRunning(ctx);
  KOS_TRACE_POST_OP_WRAP(event_await,
      KosContext_SetSuspensionEvent(ctx, KOS_SUSP_AWAIT));
  ctx->terminal = 1;
  return EVAL_AWAIT(val);
}


/**
   Unwind handler.  Called from eval_event_return when try/with/wind nodes
   sit above the frame (DYN_TOP != FRAME).  Runs the transition walk to clear
   them, then delegates to eval_event_return_at_frame.  val consumed.
*/
static inline KosOpResult eval_event_return_unwind(
    KosContext *ctx,
    PyObject *val) {

  KosDynDelim *took = NULL;
  KosOpResult unw = kos_dyn_unwind_return(ctx, KosContext_FRAME(ctx), val,
                                          KOS_PENDING_RETURN_FRAME, &took);

  /* a FINALLY took the return, a leave is in flight with val stored, or
     a leave raised; the walk consumed val on each */
  if (unw.kind != KOS_RES_NORMAL)
    return kos_eval_walk_outcome(ctx, unw);
  if (took) {
    /* The return crossed a delimiting reset: the walk stopped at a DELIM
       with a live invoke bookmark (at dyn_top).  Deliver val to the
       k-invocation site instead of the frame's parent. */
    KosOpResult rr = kos_eval_route_delim_return(ctx, took, val);
    if (rr.kind == KOS_RES_NORMAL)
      return EVAL_REPROLOGUE();
    return kos_eval_walk_outcome(ctx, rr);
  }

  return eval_event_return_at_frame(ctx, val);
}


/**
   Case-1 return predicate: parent is a plain Koskari frame, so the return
   is a straight frame-to-frame pop with no node deliveries.  Assumes
   DYN_TOP == FRAME is already established.  The single definition, shared
   by eval_event_return_at_frame and kos_return_is_plain.

   eval_plain_return sets ctx->code from the parent, pushes the value
   and returns EVAL_DISPATCH(), bypassing the prologue entirely. Both
   conjuncts below guard that, for separate reasons: without code there
   is nothing to dispatch, and with a resume pending the prologue is
   what would have delivered the value to it. A bytecode frame can
   carry a resume (the for-each store resume sits on one), so neither
   check implies the other.
*/
static inline int kos_return_parent_is_plain(KosDynNode *parent) {
  return parent
      && KosDynNode_IS_FRAME(parent)
      && KosDynFrame_CODE((KosDynFrame *) parent)
      && ! KosDynFrame_RESUME_KIND((KosDynFrame *) parent);
}


/**
   Whole plain-return predicate: no dynamic extent above the returning
   frame, and its parent is another plain Koskari frame.  Sets out_child
   and out_parent and returns 1 when it holds.

   Used by the dispatch-loop fast arm to route a KOS_RES_RETURN straight
   into eval_plain_return, skipping apply_kos_op_result and the guard
   chain it would otherwise walk to reach the same place.  Stricter than
   the chain it short-circuits only in requiring a non-NULL frame, so a
   miss can never do more than fall through to the general path.
*/
static inline int kos_return_is_plain(
    KosContext *ctx,
    KosDynNode **out_child,
    KosDynNode **out_parent) {

  KosDynNode *child = (KosDynNode *) KosContext_FRAME(ctx);
  KosDynNode *parent;

  if (unlikely(! child || KosContext_DYN_TOP(ctx) != child))
    return 0;

  parent = KosDynNode_PARENT(child);
  if (unlikely(! kos_return_parent_is_plain(parent)))
    return 0;

  *out_child = child;
  *out_parent = parent;
  return 1;
}


/**
   kos_dynframe_free with the frame-cache offer inline: the returning
   frame's fn tag is taken here and the frame offered to its function's
   cache without leaving the unit. A refused offer, a frame flagged for
   a native holder's cache, or an untagged frame falls to
   kos_dynframe_free, which skips the offer since the tag is gone.
*/
KOS_OP_INLINE void kos_dynframe_free_fast(KosDynFrame *frame) {

  /* a plain frame-to-frame return carries no resume; the clear stays
     out of line for the frames that do */
  if (unlikely(KosDynFrame_RESUME_KIND(frame)))
    kos_dynframe_clear_resume(frame);

  {
    PyObject *fn = KosDynFrame_FN(frame);
    if (likely(fn)
        && ! (KosDynFrame_FLAGS(frame) & KOS_FRAME_RETURN_TO_CACHE)) {
      int cached;

      KosDynFrame_FN(frame) = NULL;
      cached = KosFunction_CacheFrame(fn, frame);
      Py_DECREF(fn);
      if (likely(cached))
        return;
    }
  }
  kos_dynframe_free(frame);
}


/**
   NG fast path: plain Koskari frame-to-frame return.

   Inline retarget from child (being freed) to parent.  Skips the chain
   walk, the flush (frame is freed), and the agen iteration-node check (not
   reachable from a frame whose parent is itself a frame).  val consumed.
*/
KOS_OP_INLINE KosOpResult eval_plain_return(
    KosContext *ctx,
    KosDynNode *child,
    KosDynNode *parent,
    PyObject *val) {

  /* Inline retarget: equivalent to commit_dyn_top + apply_live(flush=0),
     but with no chain walk — parent is a known FRAME node, so every live
     register is read directly from it. */
  KosContext_DYN_TOP(ctx) = parent;
  KosContext_FRAME(ctx) = (KosDynFrame *) parent;
  KosContext_BLOCK(ctx) = &KosDynNode_STACK(parent);
  KosContext_NAMESPACE(ctx) = KosDynNode_NS(parent);
  KosContext_CODE(ctx) = KosDynFrame_CODE((KosDynFrame *) parent);
  ctx->pc = KosDynFrame_PC((KosDynFrame *) parent);
  ctx->acting_pc = KosDynFrame_ACTING_PC((KosDynFrame *) parent);
  kos_dynframe_free_fast((KosDynFrame *) child);

  if (KosContext_StackPUSH(ctx, val) < 0)
    return EVAL_FATAL();

  /* No control-returned hook: parent (now dyn_top) is a frame, and a
     WITH beneath it is parent's own extent, pending or not, whose hook
     runs when parent itself returns. No PyErr is set, no goal is active,
     and parent has no resume (the case-1 guard), so the trampoline can
     thread directly -- no prologue re-entry. */
  return EVAL_DISPATCH();
}


/**
   Region-parent return predicate: the frame returns into a chain node
   that passes a callee's value through to the frame beneath it -- any
   node that is not itself a frame or a generator, beneath a frame
   with no resume waiting (a stepping loop's frame takes the value as
   its step's element instead). Every retarget fact is cache-available:
   the back frame recorded at install, the freed segment's displaced
   owning slot, and the node's namespace fill. A node that reacts to
   control returning into it says so with KOS_DYN_CAP_CONTROL_HOOK and
   the transition runs its hook; the rest take the value and nothing
   more. Any missing fact falls through to the general path, mirroring
   the cache-or-walk split in kos_dyn_resync_after_pop.
*/
KOS_OP_INLINE int kos_return_is_region(
    KosContext *ctx,
    KosDynNode **out_child,
    KosDynNode **out_parent,
    KosDynFrame **out_ft) {

  KosDynNode *child = (KosDynNode *) KosContext_FRAME(ctx);
  KosDynNode *parent;
  KosDynFrame *ft;

  if (! child || KosContext_DYN_TOP(ctx) != child)
    return 0;

  parent = KosDynNode_PARENT(child);
  if (! parent
      || KosDynNode_IS_FRAME(parent)
      || KosDynNode_IS_GEN(parent))
    return 0;

  /* Same two conjuncts as kos_return_parent_is_plain, for the frame
     eval_region_return will dispatch on. */
  ft = (KosDynFrame *) KosDynFrame_BACK_FRAME(child);
  if (! ft
      || ! KosDynNode_IS_FRAME((KosDynNode *) ft)
      || ! KosDynFrame_CODE(ft)
      || KosDynFrame_RESUME_KIND(ft))
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
KOS_OP_INLINE KosOpResult eval_region_return(
    KosContext *ctx,
    KosDynNode *child,
    KosDynNode *parent,
    KosDynFrame *ft,
    PyObject *val) {

  KosSegment **bl = KosSegment_BACK(KosDynNode_STACK(child));

  KosContext_DYN_TOP(ctx) = parent;
  KosContext_FRAME(ctx) = ft;
  KosContext_BLOCK(ctx) = bl;
  KosContext_NAMESPACE(ctx) = KosDynNode_NS(parent);
  KosContext_CODE(ctx) = KosDynFrame_CODE(ft);
  ctx->pc = KosDynFrame_PC(ft);
  ctx->acting_pc = KosDynFrame_ACTING_PC(ft);
  kos_dynframe_free((KosDynFrame *) child);

  if (KosContext_StackPUSH(ctx, val) < 0)
    return EVAL_FATAL();

  if (KosDynNode_HAS_CAP(parent, KOS_DYN_CAP_CONTROL_HOOK))
    return kos_eval_post_opcode_control_returned_after_hook(ctx);
  return kos_eval_delivery_done(ctx);
}


/**
   Resume-parent return predicate, for op_RETURN: the returning frame
   is dyn_top, and its parent is a frame waiting on a resume with no
   WITH beneath it to run a control-returned hook. Sets out_parent and
   returns 1 when it holds.
*/
KOS_OP_INLINE int kos_return_is_resume(
    KosContext *ctx,
    KosDynNode **out_parent) {

  KosDynNode *child = (KosDynNode *) KosContext_FRAME(ctx);
  KosDynNode *parent;
  KosDynNode *pp;

  if (! child || KosContext_DYN_TOP(ctx) != child)
    return 0;

  parent = KosDynNode_PARENT(child);
  if (! parent
      || ! KosDynNode_IS_FRAME(parent)
      || ! KosDynFrame_RESUME_KIND((KosDynFrame *) parent))
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
    KosContext *ctx,
    KosDynNode *parent,
    PyObject *val,
    const KosResumeKind **out_resume,
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

  if (kos_dyn_pop_frame(ctx) < 0) {
    Py_DECREF(val);
    return -1;
  }

  *out_resume = KosDynFrame_RESUME_KIND((KosDynFrame *) parent);
  *out_state = KosDynFrame_RESUME_STATE((KosDynFrame *) parent);
  KosDynFrame_RESUME_KIND((KosDynFrame *) parent) = NULL;
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
    KosContext *ctx,
    PyObject *val) {

  KosDynNode *child = (KosDynNode *) KosContext_FRAME(ctx);
  KosDynNode *parent = KosDynNode_PARENT(child);

  /* Case 1: plain frame-to-frame — the dominant hot path.  Normally taken
     by the dispatch-loop fast arm before reaching here; this remains the
     terminus for producers that arrive by other routes (the unwind walk,
     the python adapters). */
  if (likely(kos_return_parent_is_plain(parent))) {
    KosOpResult r;
    KOS_TRACE_POST_OP_WRAP(parent_return,
        r = eval_plain_return(ctx, child, parent, val));
    return r;
  }

  /* Case 2: gen parent -- the flattened generator returned. The GEN
     sits on whatever the consumer frame had at dyn_top (the frame
     itself, a for-each region, an extent such as a try), and the
     frame's resume takes the end once the generator's frame and GEN
     are gone. */
  if (parent && KosDynNode_IS_GEN(parent)) {
    if (! KosDynNode_PARENT(parent)) {
      KosContext_MarkDeadAfterReturn(ctx);
      ctx->terminal = 1;
      return EVAL_RETURN(val);
    }

    if (kos_dyn_teardown_gen_exhausted(ctx) < 0) {
      Py_DECREF(val);
      return EVAL_FATAL();
    }
    if (iter_frame_has_resume(ctx))
      return iter_deliver_to_resume(ctx, val, 1);
    PyErr_SetString(PyExc_RuntimeError,
                    "generator returned into a frame with no resume"
                    " awaiting it");
    Py_DECREF(val);
    return EVAL_FATAL();
  }

  /* Case 3: any other non-NULL, non-GEN parent (WIND, TRY, WITH, a
     for-each region, native frame, etc.) -- pop the callee frame and
     let eval_parent_return_deliver handle the context transition and
     any control-return hooks. A provider's grafted invoke returning
     under a stepping region lands here: the loop frame's step resume
     takes the value as the element. */
  if (parent) {
    if (unlikely(kos_dyn_pop_frame(ctx) < 0)) {
      Py_DECREF(val);
      return EVAL_FATAL();
    }
    return eval_parent_return_deliver(ctx, val);
  }

  /* Case 4: no parent — root frame, eval done. */
  KosContext_MarkDeadAfterReturn(ctx);
  ctx->terminal = 1;
  return EVAL_RETURN(val);
}


/**
   The RETURN handler.  Guards on DYN_TOP == FRAME (no try/with/wind
   unwind needed), then dispatches via eval_event_return_at_frame.  When
   the guard fails, the unwind walk in eval_event_return_unwind handles
   it and converges on the same dispatch on completion.  val consumed.
*/
static inline KosOpResult eval_event_return(
    KosContext *ctx,
    PyObject *val) {

  if (unlikely(KosContext_DYN_TOP(ctx)
               != (KosDynNode *) KosContext_FRAME(ctx)))
    return eval_event_return_unwind(ctx, val);

  return eval_event_return_at_frame(ctx, val);
}


/**
   Resume a return that was stored on a node whose leave went
   asynchronous, now that the node is gone. See eval.h.
*/
KosOpResult KosEval_ResumeReturn(KosContext *ctx, KosObligation *ob) {

  PyObject *val = ob->u.ret.value;
  KosPendingReturnKind kind = (KosPendingReturnKind) ob->u.ret.path;
  KosDynDelim *delim = (KosDynDelim *) ob->u.ret.delim;

  ob->kind = KOS_OB_NONE;
  ob->u.ret.value = NULL;
  ob->u.ret.delim = NULL;

  /* The node is gone, so the walk from the returning frame continues
     past it: further nodes may suspend again and store the value once
     more, or the walk reaches the frame and delivers. */
  switch (kind) {
  case KOS_PENDING_RETURN_FRAME:
    return eval_event_return(ctx, val);

  case KOS_PENDING_RETURN_DELIM:
    if (delim && kos_dyn_reaches(ctx, (KosDynNode *) delim)) {
      /* The route lands val at the invoke site inside the evaluator;
         its NORMAL is a re-entry there, as eval_event_return_unwind maps it,
         never a boundary result. */
      KosOpResult rr = kos_eval_walk_outcome(
          ctx, kos_eval_route_delim_return(ctx, delim, val));
      if (rr.kind == KOS_RES_NORMAL)
        return EVAL_REPROLOGUE();
      return rr;
    }
    Py_DECREF(val);
    PyErr_SetString(PyExc_SystemError,
                    "pending return lost its delimited-invoke bookmark");
    return kos_eval_walk_outcome(ctx, EVAL_RAISE());

  case KOS_PENDING_RETURN_BREAK:
  case KOS_PENDING_RETURN_CONTINUE: {
    /* The loop walk finishes in the loop's own frame with the pc set,
       so its NORMAL is a re-entry of that frame, not a boundary. */
    KosOpResult lr = kos_eval_walk_outcome(
        ctx, kos_dyn_unwind_to_loop(
            ctx,
            (kind == KOS_PENDING_RETURN_BREAK)
                ? KOS_XFER_EVENT_BREAK : KOS_XFER_EVENT_CONTINUE,
            val));
    if (lr.kind == KOS_RES_NORMAL)
      return EVAL_REPROLOGUE();
    return lr;
  }

  default:
    Py_DECREF(val);
    PyErr_SetString(PyExc_SystemError, "pending return with no return path");
    return kos_eval_walk_outcome(ctx, EVAL_RAISE());
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
    KosContext *ctx) {

  KosDynNode *node = KosContext_CurrentNode(ctx);
  if (likely(! node || KosDynNode_IS_FRAME(node)))
    return kos_eval_delivery_done(ctx);

  KosOpResult r;
  KosObligation resume_return;

  resume_return.kind = KOS_OB_NONE;
  KOS_TRACE_POST_OP_WRAP(control_returned,
      r = kos_dyn_settle_top(ctx, &resume_return));
  if (r.kind == KOS_RES_RAISE) {
    KosOpResult rs;
    KOS_TRACE_POST_OP_WRAP(event_raise, rs = process_raise(ctx));
    return rs;
  }

  /* The unlink handed back a return that was stored on the node it
     settled: the node is gone, so the return continues from there. */
  if (resume_return.kind == KOS_OB_RETURN)
    return KosEval_ResumeReturn(ctx, &resume_return);

  return kos_eval_delivery_done(ctx);
}


/**
   After RETURN opcode pop: a parent waiting on a resume takes the
   value through the pending slot, delivered on the next prologue
   pass; a bytecode parent takes it on its stack. Then
   kos_dyn_settle_top (WIND/WITH). val is a new ref; ownership is
   consumed by this function.
*/
static inline KosOpResult eval_parent_return_deliver_impl(
    KosContext *ctx, PyObject *val) {

  KosDynFrame *parent = KosContext_FRAME(ctx);

  /* Destination by one field: a parent waiting on a resume takes the
     value through the pending slot; a bytecode parent takes it on its
     stack, where its next instruction finds it. */
  if (parent && KosDynFrame_RESUME_KIND(parent)) {
    KosContext_StageDelivery(ctx, val);
  } else if (KosContext_StackPUSH(ctx, val) < 0) {
    return EVAL_FATAL();
  }

  return kos_eval_post_opcode_control_returned_after_hook(ctx);
}


static KosOpResult eval_parent_return_deliver(KosContext *ctx, PyObject *val) {
  KosOpResult ret;
  KOS_TRACE_POST_OP_WRAP(parent_return,
      ret = eval_parent_return_deliver_impl(ctx, val));
  return ret;
}


/**
   Top-of-loop transfer drain.
*/
static KosOpResult eval_step_drain_goal(KosContext *ctx) {
  if (! KosContext_GOAL(ctx))
    return EVAL_NORMAL();
  return kos_eval_drain_goal(ctx);
}


/**
   The prologue's delivery step, first half: take the delivery staged
   for the live frame's resume and clear the resume. Caller must only
   invoke when the live frame has a resume kind set. The pending slot
   is the live transit; a code-less step frame's own stack carries a
   delivery only as storage (a drive's exit moved the slot there, or a
   landing site that still pushes). An empty slot and stack is a cold
   start: a consumer set the resume directly to defer its first step
   to this trampoline, and the resume is entered with NULL and no
   exception set. A failed delivery arrives the same way, NULL with
   PyErr set, routed here by the prologue's resume offer; the resume
   handles it or returns EVAL_RAISE to decline, which re-raises with
   the resume already cleared. The failure supersedes any delivery
   staged for the frame, in the slot or on a code-less frame's stack:
   it is dropped here, as process_raise drops the slot, so a signal
   taken by the prologue's poll after a hop staged its element never
   hands that element to the resume as a success.

   This is also where the ambient await interposes. A kind that
   settles its delivery never sees an unsettled value: the resume is
   set again and the value goes through KosContext_SettleStage, which
   either settles it eagerly (the prologue re-enters here) or suspends
   (the revive re-enters here with the settled value). A kind that
   takes its delivery as it is (a yield-from phase, the for-each store)
   gets it unsettled: that value is data, never awaited here.

   The second half is the prologue's tail call into the resume
   (KOS_PROLOGUE_DELIVER): a resume threads the consumer on and does
   not return while the loop runs, so it is never called from a plain
   function.

   :return: 1 when the resume is to be entered now, with the kind,
     its state and the value in the out parameters; 0 when the pass
     has a result of its own in out_res (a settle step, a failure)
*/
static int eval_step_resume_take(
    KosContext *ctx,
    const KosResumeKind **out_resume,
    void **out_state,
    PyObject **out_val,
    KosOpResult *out_res) {

  KosDynFrame *frame = KosContext_FRAME(ctx);
  const KosResumeKind *resume = KosDynFrame_RESUME_KIND(frame);
  void *state = KosDynFrame_RESUME_STATE(frame);
  PyObject *val;

  KosDynFrame_RESUME_KIND(frame) = NULL;
  KosDynFrame_RESUME_STATE(frame) = NULL;

  if (unlikely(PyErr_Occurred())) {
    Py_CLEAR(KosContext_PENDING_VALUE(ctx));
    if (! KosDynFrame_CODE(frame) && KosContext_StackLen(ctx) > 0)
      Py_XDECREF(KosContext_StackPopRef(ctx));
    *out_resume = resume;
    *out_state = state;
    *out_val = NULL;
    return 1;
  }

  val = KosContext_PENDING_VALUE(ctx);
  KosContext_PENDING_VALUE(ctx) = NULL;

  /* Only a code-less step frame keeps its delivery on its stack; a
     bytecode frame's stack holds its operands, and its delivery is
     always the slot. */
  if (! val && ! KosDynFrame_CODE(frame) && KosContext_StackLen(ctx) > 0) {
    val = KosContext_StackPopRef(ctx);
    if (! val) {
      *out_res = EVAL_RAISE();
      return 0;
    }

#if KOS_EVAL_SAFETYNET
    if (KosContext_StackLen(ctx) > 0) {
      /* The empty-stack test above is the cold-start discriminator;
         a second staged value would make it lie. */
      Py_DECREF(val);
      PyErr_SetString(PyExc_SystemError,
                      "resume frame staged more than one value");
      *out_res = EVAL_RAISE();
      return 0;
    }
#endif
  }

  if (val && resume->settle) {
    switch (KosEval_ResultIsValue(val)) {
      case 1:
        break;  /* settled: deliver it below */

      case 0:
        /* Not settled yet. Put the resume back and settle one step;
           the staged outcome is delivered on a later pass. */
        KosDynFrame_RESUME_KIND(frame) = resume;
        KosDynFrame_RESUME_STATE(frame) = state;
        *out_res = KosContext_SettleStage(ctx, val);
        return 0;

      default:
        Py_DECREF(val);
        *out_res = EVAL_RAISE();
        return 0;
    }
  }

  *out_resume = resume;
  *out_state = state;
  *out_val = val;
  return 1;
}


/* A RESUME answer applied rather than relayed: the delivery is staged
   for the prologue's delivery step, which enters the live frame's
   resume. Any other answer passes through. */
static inline KosOpResult apply_resume_result(KosContext *ctx, KosOpResult r) {
  if (r.kind != KOS_RES_RESUME)
    return r;
  if (r.val)
    KosContext_StageDelivery(ctx, r.val);
  return EVAL_REPROLOGUE();
}


/**
   Route a non-NORMAL opcode result through the appropriate event handler.
   NORMAL and REPROLOGUE both map to EVAL_REPROLOGUE (re-enter through the prologue).
*/
static KosOpResult apply_kos_op_result(KosContext *ctx, KosOpResult res) {
  switch (res.kind) {
  case KOS_RES_NORMAL:
  case KOS_RES_REPROLOGUE:
  case KOS_RES_POLL:
    return EVAL_REPROLOGUE();

  case KOS_RES_RAISE: {
    KosOpResult r;
    KOS_TRACE_POST_OP_WRAP(event_raise, r = process_raise(ctx));
    return r;
  }

  case KOS_RES_YIELD: {
    KosOpResult r;
    KosDynGen *gen = kos_dyn_find_gen(ctx);

    if (unlikely(! gen)) {
      Py_DECREF(res.val);
      PyErr_SetString(PyExc_RuntimeError,
                      "yield outside generator: no generator node on chain");
      return EVAL_FATAL();
    }
    KOS_TRACE_POST_OP_WRAP(event_yield,
        r = eval_event_yield(ctx, gen, res.val));
    return apply_resume_result(ctx, r);
  }

  case KOS_RES_AWAIT: {
    KosOpResult r;
    KOS_TRACE_POST_OP_WRAP(event_await, r = eval_event_await(ctx, res.val));
    return r;
  }

  case KOS_RES_RETURN: {
    KosOpResult r;
    KOS_TRACE_POST_OP_WRAP(return_event, r = eval_event_return(ctx, res.val));
    return apply_resume_result(ctx, r);
  }

  case KOS_RES_CONTROL_RETURNED:
    return kos_eval_post_opcode_control_returned_after_hook(ctx);

  case KOS_RES_RESUME:
    return apply_resume_result(ctx, res);

  case KOS_RES_FATAL:
  default:
    SAY("[eval] FATAL: apply_kos_op_result: unknown or fatal KosOpResultKind\n");
    return EVAL_FATAL();
  }
}


static void kos_eval_report_pc_bounds_error(KosContext *ctx) {

  KosDynFrame *rep_pf = KosContext_FRAME(ctx);

  if (! rep_pf) {
    PyErr_SetString(PyExc_SystemError,
                    "koskari eval: no interpreter frame during "
                    "instruction fetch");
    return;
  }

  KosCode *rep_code = KosContext_CODE(ctx);
  if (! rep_code)
    rep_code = KosDynFrame_CODE(rep_pf);
  uint32_t rep_pc = KosContext_PC(ctx);

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


/* Internal-invariant safety net. See KOS_EVAL_SAFETYNET in hints.h.
   Build with -DKOS_EVAL_SAFETYNET=1 to turn a would-be segfault from such an
   internal misfire into a clean diagnostic while tracking down the cause. */
#if KOS_EVAL_SAFETYNET
/* ctx->code can be transiently NULL only through an internal bug (e.g. a
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
   an off-the-end fetch would. The ctx->code null check is an
   internal-invariant guard, gated behind KOS_EVAL_SAFETYNET (off by
   default). */
#define KOS_EVAL_PREPARE_NEXT_INSN(ctx, insn_lvalue, on_bad)               \
  do {                                                                     \
    KosCode *_kos_code = KosContext_CODE(ctx);                             \
    uint32_t _kos_pc = KosContext_PC(ctx);                                 \
    KOS_EVAL_SAFETYNET_NULLCODE(_kos_code, on_bad);                        \
    KosOp *_kos_insn = &KosCode_CODE(_kos_code)[_kos_pc];                  \
    SAYF("[eval] pc=%u op=%s arg=%u\n",                                    \
         (unsigned) _kos_pc, kos_opcode_name(_kos_insn->op),               \
         (unsigned) _kos_insn->arg);                                       \
    (ctx)->acting_pc = _kos_pc;                                            \
    (ctx)->pc = _kos_pc + 1u;                                              \
    (insn_lvalue) = _kos_insn;                                             \
  } while (0)


/* A re-entry step that asks for another pass of the loop below. A
   pass is a cooperation point: a resume hop that found the breaker up
   staged its delivery and answered REPROLOGUE expecting a poll, and a
   loop that simply went round again would deliver, hop, and stage
   forever with the word still up. So a pass polls first when the
   breaker is up. Not a do-while: the continue must reach the
   enclosing for. */
#define KOS_REENTER_AGAIN(ctx)                                          \
  {                                                                     \
    if (KOS_EVAL_BREAKER_UP(ctx))                                       \
      kos_eval_poll_now();                                              \
    continue;                                                           \
  }

/* The prologue's delivery step: take the delivery and enter the
   resume by tail call. The resume threads the consumer on; a result
   that comes back is the chain's own and reaches the trampoline,
   which applies it. A pass result instead (a settle step, a failure)
   re-enters or returns as any other phase's does. Not a do-while: the
   re-entry must reach the enclosing for. */
#define KOS_PROLOGUE_DELIVER(ctx)                                       \
  {                                                                     \
    const KosResumeKind *kos_pd_resume_;                                \
    void *kos_pd_state_;                                                \
    PyObject *kos_pd_val_;                                              \
    KosOpResult kos_pd_res_;                                            \
                                                                        \
    if (eval_step_resume_take((ctx), &kos_pd_resume_, &kos_pd_state_,   \
                              &kos_pd_val_, &kos_pd_res_)) {            \
      KOS_TRACE_RESUME_STEP();                                          \
      TAILCALL(kos_pd_resume_->fn, (ctx), kos_pd_state_, kos_pd_val_);  \
    }                                                                   \
    if (kos_pd_res_.kind == KOS_RES_REPROLOGUE)                         \
      KOS_REENTER_AGAIN(ctx);                                           \
    return kos_pd_res_;                                                 \
  }


KOS_PRESERVE_NONE KosOpResult kos_eval_prologue(
    KosContext *ctx,
    void *insn,
    PyObject *val) {

  (void) insn;
  (void) val;

  /* A re-entry is a cooperation point: a raise walk, a plan transfer,
     a continuation replay, or a staged delivery arrives here, and the
     poll runs before the pass delivers, so a pending signal is seen
     ahead of a staged delivery. */
  if (KOS_EVAL_BREAKER_UP(ctx))
    kos_eval_poll_now();

  /* the live registers are retargeted by kos_dyn_* after chain
     surgery; chain errors propagate without rebuild. */
  for (;;) {

    #if KOS_STACK_TRACE
    {
      KosSegment **_kos_tr = KosContext_BLOCK(ctx);
      trace_stack("  after ",
                  (_kos_tr && *_kos_tr) ? *_kos_tr : NULL);
    }
    #endif

    /* The pass reads the pending exception and the goal's intent once:
       nothing below changes either without leaving the pass, since a
       pending exception takes the raise phase, which returns or
       restarts the loop. A pass with no goal skips the probe. */
    PyObject *const err = PyErr_Occurred();
    const int will_drive =
        KosContext_GOAL(ctx) && kos_xfer_goal_will_drive(ctx);

    /* Slim bounce: a staged delivery with no pending exception and no
       goal about to drive goes straight to its resume. The two checks
       below exist for chain surgery, which a staged handoff performs
       none of. A goal waiting on a running thunk is dormant: the thunk's
       own deliveries go through here like anyone else's. */
    if (KosContext_PENDING_VALUE(ctx) && ! will_drive && ! err) {
      KOS_PROLOGUE_DELIVER(ctx);
    }

    /* A failed delivery reaches its consumer the way a successful one
       does. A frame with a resume set, bytecode or code-less, takes
       the pending exception as a NULL delivery, on a settled chain,
       where threading on is legal; every resume tells a NULL with an
       error pending from its own meaning of a bare NULL. Declining
       re-raises with the resume already cleared by the delivery, so
       the walk below unwinds as it always did, and this cannot loop.

       A goal in play is routed past this offer and through the phase
       below, whose process_raise abandons the goal stack and then
       bounces straight back here with no goal active -- so a transfer
       still reaches abandon first, and the offer still fires. */
    if (err
        && ! KosContext_GOAL(ctx)
        && KosContext_FRAME(ctx)
        && KosDynFrame_RESUME_KIND(KosContext_FRAME(ctx))) {
      KOS_PROLOGUE_DELIVER(ctx);
    }

    /* Pending PyErr from a suspended transfer thunk (e.g. RAISE in a
       wind before-thunk) must run abandon + try_local before draining
       the plan; otherwise the wind EXTENT_ACTION can complete while
       PyErr is still set. */
    if (err) {
      KosOpResult pend = process_raise(ctx);
      if (pend.kind != KOS_RES_NORMAL) {
        if (pend.kind == KOS_RES_REPROLOGUE)
        KOS_REENTER_AGAIN(ctx);
        return pend;
      }
    }

    /* A transfer replaces the control path a staged delivery was
       waiting on: the frame it was staged for may be gone once the
       goal lands, and the goal's own delivery is the one the landing
       frame expects. Drop the stale slot ahead of a drive so it can
       neither shadow that delivery nor reach a resume it was never
       meant for. Only ahead of a drive: while the goal waits on a
       running thunk the slot belongs to the thunk's code. */
    if (will_drive)
      Py_CLEAR(KosContext_PENDING_VALUE(ctx));

    {
      KosOpResult xfer = eval_step_drain_goal(ctx);
      if (xfer.kind != KOS_RES_NORMAL) {
        if (xfer.kind == KOS_RES_REPROLOGUE)
        KOS_REENTER_AGAIN(ctx);
        return xfer;
      }
    }

    if (KosContext_FRAME(ctx)
        && KosDynFrame_RESUME_KIND(KosContext_FRAME(ctx)) != NULL) {
      KOS_PROLOGUE_DELIVER(ctx);
    }

#if KOS_EVAL_SAFETYNET
    /* Instruction dispatch never runs over a staged delivery: the slot
       is consumed in the trip that staged it, so a capture made by an
       opcode can never observe one. */
    if (unlikely(KosContext_PENDING_VALUE(ctx))) {
      Py_CLEAR(KosContext_PENDING_VALUE(ctx));
      PyErr_SetString(PyExc_SystemError,
                      "CPS delivery staged with no resume to take it");
      return EVAL_FATAL();
    }
#endif

    #if KOS_STACK_TRACE
    {
      KosSegment **_kos_tr = KosContext_BLOCK(ctx);
      trace_stack("  before",
                  (_kos_tr && *_kos_tr) ? *_kos_tr : NULL);
    }
    #endif

    {
      KosOp *next;

      KOS_EVAL_PREPARE_NEXT_INSN(ctx, next, kos_eval_prologue_bad_code);
      KOS_TRACE_THREAD_STEP(next->op);
      TAILCALL(next->fn, ctx, next, NULL);
    }
  }

#if KOS_EVAL_SAFETYNET
kos_eval_prologue_bad_code:
  kos_eval_report_pc_bounds_error(ctx);
  return EVAL_FATAL();
#endif
}


/**
   The evaluator entry: entry and revive normalization, transfer-plan
   synchronization, then the trampoline that threaded dispatch returns
   to.  Threading enters from ctx->pc; a chain then runs handler to
   handler by tail calls and comes back here only with a signal.
   NORMAL means the chain stopped with the next instruction at ctx->pc
   -- a cooperation point that found the eval breaker up, or a step
   with no way to continue -- so poll if the breaker is up and thread
   again.  Everything else is applied through apply_kos_op_result, and
   whatever that yields either threads (DISPATCH), re-enters through
   the prologue (REPROLOGUE), or leaves through the cleanup below.
   Handlers and helpers take ctx itself. s is the calling thread's
   record when the caller resolved it for its own use, else NULL and
   resolved here.
*/
KosOpResult koskari_eval(
    KosContext *ctx,
    KosTlsState *s,
    PyObject *send_val,
    PyObject *throw_typ,
    PyObject *throw_exc) {

  KosOpResult eval_ret = EVAL_RAISE();
  KosExcState host_exc;
  PyThreadState *tstate;
  int host_held;
  KosOpResult r;

  if (! ctx) {
    PyErr_SetString(PyExc_RuntimeError, "NULL context for koskari_eval");
    return EVAL_RAISE();
  }

  /* A context with the running flag set is held by an eval on a C
     stack, this thread's or another's; every suspension and the
     cleanup below clear the flag before that eval returns, so a second
     entry can only be a caller without a busy check of its own. Refused
     here, before anything below is set up, so the return has nothing to
     unwind; nothing between this read and the set below releases the
     GIL, so another thread cannot pass the check in the meantime. */
  if (unlikely(KosContext_FLAGS(ctx) & KOS_CTX_RUNNING)) {
    PyErr_SetString(PyExc_RuntimeError,
                    "eval entered on a context already being evaluated");
    return EVAL_RAISE();
  }

  /* FAST-slot code needs a namespace with fast slots. Checked here,
     before anything below is set up, so a refusal returns with nothing
     to unwind: the cleanup label pops the thread-local context entry
     that only the push below creates, and reaching it from an earlier
     point would pop an enclosing eval's entry instead. A resume was
     checked on its first entry, and a native frame carries no code. */
  {
    KosDynFrame *top = KosContext_FRAME(ctx);
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

  /* the calling thread's record, resolved after the refusals above so
     a refused entry does not pay for it; tstate is kept for the
     cleanup, which runs after kos_tls_leave_drive has released s. */
  if (! s)
    s = kos_tls_current();
  tstate = kos_tls_thread_state(s);

  if (kos_recursion_enter(tstate, " in koskari_eval"))
    return EVAL_RAISE();

  /* the context runs this eval under the domain current on the thread:
     the active context's, else the innermost `with Domain():`, else
     the thread default. Nothing is carried between evals: the exit
     clears it, and a context resumed elsewhere takes that stack's.
     Taken after the last refusal above, so a refused entry leaves no
     domain on the context. */
  {
    PyObject *domain = kos_tls_domain(s);
    if (unlikely(! domain)) {
      kos_recursion_leave(tstate);
      return EVAL_RAISE();
    }
    Py_INCREF(domain);
    Py_XSETREF(KosContext_DOMAIN(ctx), domain);
  }

  /* Swap in this context's handler-visible exception state (sys.exc_info),
     saving the host's. Contexts interleaved on one thread must not see
     each other's handled exception; the reverse swap runs at cleanup.
     The swap moves the thread state's top handled-exception item only,
     the one KosExc_Install writes: it is peeked first and fetched only
     when it holds one. A deeper item (a caller's handler, seen from
     inside a Python generator) is left where it is. */
  host_held = kos_tstate_exc_held(tstate);
  KosExc_Init(&host_exc);
  if (host_held) {
    KosExc_Fetch(&host_exc);
    KOS_TRACE_EVENTS_INC(KOS_TE_EXC_FETCH);
  }
  if (host_held || KosExc_Held(KosContext_EXC_INFO(ctx)))
    KosExc_Install(KosContext_EXC_INFO(ctx));

  /* The drive holds the context: a holder that drops its reference
     while bytecode runs (a coroutine collected from inside its own
     body, a provider cleared under its stepper) leaves the context
     alive until the drive's cleanup below. */
  Py_INCREF(ctx);

  kos_tls_enter_drive(s, ctx);
  KosContext_EvalEnter(ctx, tstate);

  /* A chain of one frame node that the registers already name needs
     no resync: the walk's result is that node's frame, stack slot and
     namespace, and every writer of the registers, the context's
     construction included, set all three from it. A root frame is
     never fully stamped, so the restamp has nothing to do either. */
  {
    KosDynNode *top = KosContext_DYN_TOP(ctx);
    if (top
        && (top != (KosDynNode *) KosContext_FRAME(ctx)
            || KosDynNode_PARENT(top)))
      kos_dyn_resync_chain(ctx);
  }

  KosContext_FLAGS(ctx) |= KOS_CTX_RUNNING;
  KOS_TRACE_EVAL_PUSH();
  KOS_TRACE_TRAMPOLINE_STEP();

  /* Phase 0: a boundary entry's call, recorded on the context */
  if (unlikely(KosContext_ENTRY_CALL(ctx))) {
    r = eval_phase_entry_call(ctx);
    if (r.kind != KOS_RES_NORMAL) {
      eval_ret = r;
      goto koskari_eval_cleanup;
    }
  }

  /* Phase 1: entry/revive normalization */
  {
    KosEvalMode mode = KOS_MODE_NORMAL;
    KosOpResult pe = eval_phase_entry(ctx, send_val, throw_typ, throw_exc,
                                      &mode);
    if (pe.kind != KOS_RES_NORMAL && pe.kind != KOS_RES_REPROLOGUE) {
      eval_ret = pe;
      goto koskari_eval_cleanup;
    }

    /* Phase 2: transfer goal drive (only when mode warrants it) */
    if (mode == KOS_MODE_NORMAL) {
      if (unlikely(KosContext_GOAL(ctx))) {
        KosOpResult pt = eval_phase_transfer(ctx);
        if (pt.kind != KOS_RES_NORMAL && pt.kind != KOS_RES_REPROLOGUE) {
          eval_ret = pt;
          goto koskari_eval_cleanup;
        }
      }
    }
  }

  /* The Python resume slot is set by a driver before an await revive
     and taken by the revive; an entry that skipped the revive (an await
     iterator still pending) clears it here, so the trampoline and the
     chain never touch it. */
  KosContext_PythonResumeSet(ctx, NULL, KOS_RESUME_NONE);

  /* === the trampoline === */

  /* One call into the prologue, which threads into the instruction
     stream; a chain comes back only with a boundary result, marked as
     such on the context by the event that made it one. A result
     that reached here unmarked came from a producer that returned it
     raw instead of tailing into its event, and is applied here as the
     loop-bodied evaluator did. */
  for (;;) {
    r = kos_eval_prologue(ctx, NULL, NULL);
    if (ctx->terminal || r.kind == KOS_RES_FATAL) {
      eval_ret = r;
      break;
    }
    if (r.kind == KOS_RES_REPROLOGUE)
      continue;

    KOS_TRACE_TRAMPOLINE_STEP();
    r = apply_kos_op_result(ctx, r);
    if (r.kind == KOS_RES_DISPATCH || r.kind == KOS_RES_REPROLOGUE)
      continue;
    eval_ret = r;
    break;
  }

koskari_eval_cleanup:
  KOS_TRACE_OP_EXIT();
  KOS_TRACE_EVAL_POP();
  KosContext_EvalExit(ctx);
  KosContext_FLAGS(ctx) &= ~KOS_CTX_RUNNING;
  kos_tls_leave_drive(s, ctx);
  Py_CLEAR(KosContext_DOMAIN(ctx));

  /* Save this context's handler-visible exception state and restore the
     host's (the reverse of the entry swap). By what the thread's top
     handled-exception item holds now and what it held at entry:

     - thread holds one, host held one: the context takes the thread's,
       the host's is installed again.
     - thread holds one, host held none: the context takes the thread's,
       the thread's is cleared.
     - thread holds none, host held one: the context's is cleared, the
       host's is installed again.
     - neither: nothing moves; the context keeps what it held.

     Only the first two fetch. */
  if (kos_tstate_exc_held(tstate)) {
    KosExcState current;

    KosExc_Init(&current);
    KosExc_Fetch(&current);
    KOS_TRACE_EVENTS_INC(KOS_TE_EXC_FETCH);
    KosExc_Clear(KosContext_EXC_INFO(ctx));
    KosExc_Move(KosContext_EXC_INFO(ctx), &current);
    KosExc_Install(&host_exc);
  } else if (KosExc_Held(&host_exc)) {
    KosExc_Clear(KosContext_EXC_INFO(ctx));
    KosExc_Install(&host_exc);
  }

  /* the drive's hold on the context, taken at entry; a holder that let
     go during the drive deallocates it here */
  kos_recursion_leave(tstate);
  Py_DECREF(ctx);
  return eval_ret;
}


#undef KOS_EVAL_PREPARE_NEXT_INSN
#undef KOS_PROLOGUE_DELIVER
#undef KOS_REENTER_AGAIN


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

  /* The end-of-iteration delivery, a private object compared by
     identity. */
  if (! KosIterEnd) {
    KosIterEnd = PyObject_CallNoArgs((PyObject *) &PyBaseObject_Type);
    if (! KosIterEnd)
      return -1;
  }

  /* Initialize string constants (cleared in koskari_eval_clear). */
  STR_CONST(str_aenter, "__aenter__");
  STR_CONST(str_aexit, "__aexit__");
  STR_CONST(str_args, "args");
  STR_CONST(str_asend, "asend");
  STR_CONST(str_asyncio_future_blocking, "_asyncio_future_blocking");
  STR_CONST(str_athrow, "athrow");
  STR_CONST(str_continuation, "continuation");
  STR_CONST(str_enter, "__enter__");
  STR_CONST(str_exit, "__exit__");
  STR_CONST(str_name, "__name__");
  STR_CONST(str_package, "__package__");
  STR_CONST(str_empty, "");
  STR_CONST(str_target, "target");
  STR_CONST(str_dynchain, "__koskari_dynchain__");
  STR_CONST(str_rec_name, "name");
  STR_CONST(str_rec_filename, "filename");
  STR_CONST(str_rec_line, "line");
  STR_CONST(str_rec_col, "col");
  STR_CONST(str_rec_pc, "pc");
  STR_CONST(str_rec_crossed, "crossed");
  STR_CONST(str_rec_frame, "frame");

  if (PyType_Ready(&KosContextType) < 0)
    return -1;
  Py_INCREF(&KosContextType);
  if (PyModule_AddObject(mod, "Context", (PyObject *) &KosContextType) < 0) {
    Py_DECREF(&KosContextType);
    return -1;
  }

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
  Py_CLEAR(KosIterEnd);

  Py_CLEAR(str_aenter);
  Py_CLEAR(str_aexit);
  Py_CLEAR(str_args);
  Py_CLEAR(str_asend);
  Py_CLEAR(str_asyncio_future_blocking);
  Py_CLEAR(str_athrow);
  Py_CLEAR(str_continuation);
  Py_CLEAR(str_enter);
  Py_CLEAR(str_exit);
  Py_CLEAR(str_name);
  Py_CLEAR(str_package);
  Py_CLEAR(str_empty);
  Py_CLEAR(str_target);
  Py_CLEAR(str_dynchain);
  Py_CLEAR(str_rec_name);
  Py_CLEAR(str_rec_filename);
  Py_CLEAR(str_rec_line);
  Py_CLEAR(str_rec_col);
  Py_CLEAR(str_rec_pc);
  Py_CLEAR(str_rec_crossed);
  Py_CLEAR(str_rec_frame);

  /* Null out exception type pointers (module dict owns the refs) */
  KoskariContinuationDead = NULL;
  KoskariContinuationApply = NULL;
  KoskariContinuationError = NULL;

  context_freelist_drain();
  koskari_dynchain_clear(mod);

  return 0;
}


// The end.
