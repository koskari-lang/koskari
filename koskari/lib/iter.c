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
   koskari/lib/iter.c

   KosPyAsyncGen: the generator-backed KosIter provider. Its Python
   surface (anext/asend/athrow/aclose, tp_iter KosPyIter) is
   filled by the generic KosIterAdapt_* adapters (iterate.c).

   :author: Christopher O'Brien <obriencj@preoccupied.net>
   :license: GNU General Public License v3
*/


#include "lib.h"


static int asyncgen_close_drive(KosPyAsyncGen *agen);


/* === KosPyAsyncGen type === */


/**
   Free the resting span without running the extent protocol, the
   unstarted frame or the chain a yield left suspended, and the resting
   GEN node with it. Safe at arbitrary points, including dealloc
   mid-unwind with a live exception: no Python calls, no evaluator
   re-entry, and the pending PyErr is never touched.
*/
static void asyncgen_free_span(KosPyAsyncGen *agen) {

  kos_gen_state_clear(&agen->state);
}


/**
   Free the chain on a context the wrapper stepped its generator on, if
   one is there: a span left installed after a close the eval could
   not honor. The same two steps the context's clear takes; nothing
   refuses them.
*/
static void asyncgen_free_chain_on(KosContext *ctx) {

  if (! ctx || ! KosContext_DYN_TOP(ctx))
    return;
  kos_dynchain_free(KosContext_DYN_TOP(ctx), NULL);
  KosContext_ClearLiveRegs(ctx);
}


/**
   Teardown a suspended async generator: install its span on ctx, throw
   GeneratorExit, run to completion, or fall back to freeing the span
   if the body attempts to suspend (yield/await).

   Assumes a started span resting in the state and ctx's chain empty.

   This is the demand-path close: the GeneratorExit drive runs
   synchronously to completion, and a suspend attempt during close takes
   the fallback (cannot honor). All koskari generators are async, so
   dealloc must still run their teardown; a future explicit aclose is
   where a genuinely suspending close could be honored.

   Safe at arbitrary points, including dealloc mid-unwind: the caller's
   pending exception is fenced (fetched before the drive, restored
   after), so the drive never observes it and the internal PyErr_Clear
   calls cannot destroy it.

   On success (primary path): chain unwound, GEN node freed with it,
   ctx ready for free. On fallback: chain and span freed, ctx ready for
   free. Does not free ctx; caller does that.
*/
static void asyncgen_teardown_chain_on_ctx(KosPyAsyncGen *agen, KosContext *ctx) {

  PyObject *throw_exc = NULL;
  PyObject *fence_typ = NULL;
  PyObject *fence_val = NULL;
  PyObject *fence_tb = NULL;

  PyErr_Fetch(&fence_typ, &fence_val, &fence_tb);

  throw_exc = PyObject_CallNoArgs(PyExc_GeneratorExit);
  if (! throw_exc) {
    PyErr_Clear();
    goto fallback;
  }

  /* Install the span onto ctx so the chain is restored before eval; in
     the flatten path the chain rests in the state and agen->ctx was
     never the running ctx. */
  KosContext_EvalEnter(ctx, NULL);
  if (kos_dyn_gen_install(ctx, (PyObject *) agen, &agen->state) < 0) {
    KosContext_EvalExit(ctx);
    PyErr_Clear();
    Py_DECREF(throw_exc);
    goto fallback;
  }
  /* the eval below begins and ends a drive of its own */
  KosContext_EvalExit(ctx);

  {
    KosOpResult res = koskari_eval(
        ctx, NULL, NULL, PyExc_GeneratorExit, throw_exc);
    Py_DECREF(throw_exc);

    switch (res.kind) {
      case KOS_RES_RAISE:
      case KOS_RES_FATAL:
        /* GeneratorExit or StopIteration coming back is the close
           completing; anything else is what the cleanup raised, and
           is reported as a finalizer's error is. */
        if (PyErr_ExceptionMatches(PyExc_GeneratorExit)
            || PyErr_ExceptionMatches(PyExc_StopIteration)
            || PyErr_ExceptionMatches(PyExc_StopAsyncIteration))
          PyErr_Clear();
        else
          PyErr_WriteUnraisable((PyObject *) agen);
        goto primary_success;
      case KOS_RES_RETURN:
        Py_XDECREF(res.val);
        goto primary_success;
      case KOS_RES_AWAIT:
      case KOS_RES_YIELD:
        /* Suspend attempt during close; cannot honor */
        Py_XDECREF(res.val);
        goto fallback;
      default:
        goto fallback;
    }
  }

primary_success:
  KosContext_EvalEnter(ctx, NULL);
  if (kos_dyn_teardown_gen_exhausted(ctx) < 0) {
    KosContext_EvalExit(ctx);
    goto fallback;
  }
  KosContext_EvalExit(ctx);
  PyErr_Restore(fence_typ, fence_val, fence_tb);
  return;

fallback:
  PyErr_Clear();
  /* an install that went through left the span on ctx, GEN node and
     all; a refused one, or a yield's freeze, left it resting */
  asyncgen_free_chain_on(ctx);
  asyncgen_free_span(agen);
  PyErr_Restore(fence_typ, fence_val, fence_tb);
}


void KosPyAsyncGen_Teardown(KosPyAsyncGen *agen) {

  KosGenState *state;

  if (! agen)
    return;
  state = &agen->state;

  if (agen->ctx && KosContext_DYN_TOP(agen->ctx) && ! state->busy) {
    /* Live chain: the generator was abandoned mid-await, where the
       resting-span teardown below cannot reach it. Demand-drive the
       close through the suspended context, exception-fenced --
       teardown runs from dealloc, so every outcome of the drive is
       swallowed and cleared. */
    PyObject *fence_typ, *fence_val, *fence_tb;
    PyErr_Fetch(&fence_typ, &fence_val, &fence_tb);
    (void) asyncgen_close_drive(agen);
    PyErr_Clear();
    PyErr_Restore(fence_typ, fence_val, fence_tb);
  }

  if (state->active && ! state->started) {
    /* never driven: nothing entered, nothing to unwind */
    asyncgen_free_span(agen);

  } else if (state->active) {
    /* suspended: the GeneratorExit drive needs a context; one spliced
       only from Koskari has none, so it gets one here, and falls back
       to the pure cleanup when even that cannot be had */
    KosContext *ctx = agen->ctx;
    if (! ctx) {
      PyObject *fence_typ, *fence_val, *fence_tb;
      PyErr_Fetch(&fence_typ, &fence_val, &fence_tb);
      ctx = KosContext_New(NULL, kos_tls_current());
      if (! ctx)
        PyErr_Clear();
      PyErr_Restore(fence_typ, fence_val, fence_tb);
    }
    if (ctx) {
      asyncgen_teardown_chain_on_ctx(agen, ctx);
      if (ctx != agen->ctx)
        Py_DECREF(ctx);
    } else {
      asyncgen_free_span(agen);
    }
  }

  /* a chain still on the wrapper's context, with its GEN node, goes
     with the context, before the wrapper's memory does */
  Py_CLEAR(agen->ctx);
}


/**
   Drop the generator's resting span and context without driving the
   body. The close itself belongs to the finalizer, which has already
   run by the time clear or dealloc reaches here. A span installed on
   a consumer's chain is held there by the record stepping it, which
   holds the wrapper too, so it is never met here; one on the
   wrapper's own context goes with the context.
*/
static void asyncgen_drop_state(KosPyAsyncGen *agen) {

  asyncgen_free_span(agen);
  Py_CLEAR(agen->ctx);
}


/**
   Finalizer: a generator dropped while suspended is closed before its
   state is torn down, the way CPython closes an abandoned async
   generator, so its finally blocks, with exits and wind after thunks
   run against its frame as it was. GeneratorExit is thrown at the
   suspension point through the same drive aclose uses, and whatever
   comes back is swallowed there. A generator never started has
   entered nothing, a finished or closed one has nothing left to
   unwind, and one mid-drive belongs to that drive.
*/
static void asyncgen_finalize(PyObject *self) {

  KosPyAsyncGen *agen = (KosPyAsyncGen *) self;
  KosGenState *state = &agen->state;

  if (state->busy || agen->closed)
    return;
  if (state->active && ! state->started)
    return;
  if (! (agen->ctx && KosContext_DYN_TOP(agen->ctx)) && ! state->active)
    return;

  KosPyAsyncGen_Teardown(agen);
  agen->closed = 1;
}


static void asyncgen_dealloc(PyObject *self) {

  /* run the finalizer first; it may resurrect the generator, in which
     case the dealloc is abandoned here and happens on the next drop */
  if (PyObject_CallFinalizerFromDealloc(self) < 0)
    return;

  PyObject_GC_UnTrack(self);

  KosPyAsyncGen *agen = (KosPyAsyncGen *) self;

  asyncgen_drop_state(agen);
  Py_CLEAR(agen->code_name);

  /* A function's generator is stored on the function when its slot is
     free: untracked, every field clear, refcount zero, the next call
     revives it. The function's own release frees a stored one, so the
     reference dropped here may free both at once. */
  {
    PyObject *fn = agen->fn;

    agen->fn = NULL;
    if (fn) {
      if (! KosFunction_DEAD_RECORD(fn)) {
        KosFunction_DEAD_RECORD(fn) = self;
        KOS_TRACE_FL_RETURN("asyncgen", 0);
        Py_DECREF(fn);
        return;
      }
      KOS_TRACE_FL_RETURN("asyncgen", 1);
      Py_DECREF(fn);
    }
  }

  Py_TYPE(self)->tp_free(self);
}


static int asyncgen_traverse(PyObject *self, visitproc visit, void *arg) {

  KosPyAsyncGen *agen = (KosPyAsyncGen *) self;
  int res;

  Py_VISIT(agen->code_name);
  Py_VISIT(agen->fn);

  if (agen->state.active) {
    res = kos_dynchain_traverse(agen->state.active, NULL, visit, arg);
    if (res)
      return res;
  }

  Py_VISIT(agen->ctx);

  return 0;
}


static int asyncgen_clear(PyObject *self) {

  KosPyAsyncGen *agen = (KosPyAsyncGen *) self;

  Py_CLEAR(agen->code_name);
  Py_CLEAR(agen->fn);
  asyncgen_drop_state(agen);

  return 0;
}


static PyObject *asyncgen_aiter(PyObject *self) {

  Py_INCREF(self);
  return self;
}


static PyObject *asyncgen_repr(PyObject *self) {

  KosPyAsyncGen *agen = (KosPyAsyncGen *) self;

  if (agen->code_name && PyUnicode_Check(agen->code_name))
    return PyUnicode_FromFormat(
        "<async_generator object %U at %p>", agen->code_name, (void *) self);
  return PyUnicode_FromFormat("<async_generator object at %p>", (void *) self);
}


static PyObject *asyncgen_get_name(PyObject *self, void *closure) {

  KosPyAsyncGen *agen = (KosPyAsyncGen *) self;

  (void) closure;
  if (agen->code_name) {
    Py_INCREF(agen->code_name);
    return agen->code_name;
  }
  Py_RETURN_NONE;
}


static PyGetSetDef asyncgen_getsets[] = {
  { "__name__", asyncgen_get_name, NULL,
    "code name of the generator body", NULL },
  { NULL, NULL, NULL, NULL, NULL },
};


static PyMethodDef asyncgen_methods[] = {
  { "asend", (PyCFunction) KosIterAdapt_Asend, METH_O,
    "Send a value into the async generator; returns an awaitable" },

  { "athrow", (PyCFunction) KosIterAdapt_Athrow, METH_VARARGS,
    "Throw an exception into the async generator; returns an awaitable" },

  { "aclose", (PyCFunction) KosIterAdapt_Aclose, METH_NOARGS,
    "Close the async generator; returns an awaitable" },

  { NULL, NULL, 0, NULL },
};


static PyAsyncMethods asyncgen_async_methods = {
  .am_aiter = asyncgen_aiter,
  .am_anext = KosIterAdapt_Anext,
};



/**
   Drive GeneratorExit through a context abandoned mid-await, so its
   cleanup runs during a synchronous stepper close; the frozen-chain
   teardown cannot reach a live chain. Mirrors coroutine_close: the
   pending awaitable is closed first, then the throw is delivered at
   the suspended AWAIT. Returns 0 when the generator wound down
   (including a cleanup suspension taking the cannot-honor fallback),
   -1 with an exception set when the body ignored GeneratorExit or
   raised something else.
*/
static int asyncgen_close_drive(KosPyAsyncGen *agen) {

  PyObject *it = KosContext_PY_AWAIT_ITER(agen->ctx);
  if (it) {
    kos_iter_close(it);
    Py_CLEAR(KosContext_PY_AWAIT_ITER(agen->ctx));
  }

  PyObject *exc = PyObject_CallNoArgs(PyExc_GeneratorExit);
  if (! exc)
    return -1;

  KosOpResult res = koskari_eval(
      agen->ctx, NULL, NULL, PyExc_GeneratorExit, exc);
  Py_DECREF(exc);

  switch (res.kind) {
    case KOS_RES_RETURN:
      Py_XDECREF(res.val);
      return 0;

    case KOS_RES_AWAIT:
      /* a synchronous close cannot suspend: cannot-honor fallback */
      Py_XDECREF(res.val);
      PyErr_Clear();
      return 0;

    case KOS_RES_YIELD:
      Py_XDECREF(res.val);
      PyErr_SetString(PyExc_RuntimeError,
                      "async generator ignored GeneratorExit");
      return -1;

    case KOS_RES_RAISE:
    case KOS_RES_FATAL:
    default:
      if (! PyErr_Occurred())
        return 0;
      if (PyErr_ExceptionMatches(PyExc_GeneratorExit)
          || PyErr_ExceptionMatches(PyExc_StopIteration)
          || PyErr_ExceptionMatches(PyExc_StopAsyncIteration)) {
        PyErr_Clear();
        return 0;
      }
      return -1;
  }
}




/* === KosIter suite === */


/**
   The suite's ki_busy: 1 with the RuntimeError set while another site
   is driving the generator, else 0
*/
static int agen_iter_busy(PyObject *self) {

  KosPyAsyncGen *agen = (KosPyAsyncGen *) self;

  if (! agen->state.busy)
    return 0;

  PyErr_SetString(PyExc_RuntimeError,
                  "async generator already being iterated");
  return 1;
}


/**
   Shared core of ki_step and ki_throw: guard, mark busy, splice the
   generator's frames onto the consumer context. On success the drive is
   in flight (KOS_ITER_INSTALLED); when throwing, the exception the
   throw ingress primed is delivered at resume. send_val, when given,
   replaces the install placeholder on TOS as the value of the
   suspended yield expression.

   The state's span is the unstarted frame or a suspended chain, told
   apart by the started flag; no span with a chain live on the
   wrapper's context is a drive mid-await, grafted onto a consumer
   context other than the wrapper's own. Nothing in either place is a
   spent generator.
*/
static KosIterStepResult agen_iter_drive(
    KosContext *ctx,
    KosPyAsyncGen *agen,
    PyObject *send_val,
    int throwing,
    PyObject **out) {

  KosGenState *state = &agen->state;
  KosDynNode *active;
  int started;

  if (agen_iter_busy((PyObject *) agen))
    return KOS_ITER_ERROR;

  if (agen->closed) {
    *out = NULL;
    return KOS_ITER_EXHAUSTED;
  }

  active = state->active;
  started = state->started;

  if (send_val && send_val != Py_None && ! started) {
    PyErr_SetString(PyExc_TypeError,
                    "can't send non-None value to a just-started "
                    "async generator");
    return KOS_ITER_ERROR;
  }

  if (! active && ! (agen->ctx && KosContext_DYN_TOP(agen->ctx))) {
    *out = NULL;
    return KOS_ITER_EXHAUSTED;
  }

  state->busy = 1;

  if (! active) {
    if (ctx != agen->ctx) {
      if (kos_dyn_gen_install_bootstrap(
              ctx, (PyObject *) agen, state,
              KosContext_DYN_TOP(agen->ctx)) < 0)
        goto drive_error;
      KosContext_ClearLiveRegs(agen->ctx);
    }
    return KOS_ITER_INSTALLED;
  }

  /* the wrapper's own context drops its stale chain pointers, unless
     it is the drive context, whose chain the install extends */
  if (agen->ctx && agen->ctx != ctx)
    KosContext_ClearLiveRegs(agen->ctx);

  if (! started) {
    /* the first install enters fresh code with an empty stack: no
       placeholder to swap, nothing to throw into. A refusal (no frame
       room) leaves the frame resting. */
    if (kos_dyn_gen_install_bootstrap(ctx, (PyObject *) agen, state,
                                      active) < 0)
      goto drive_error;
    state->active = NULL;
    return KOS_ITER_INSTALLED;
  }

  if (kos_dyn_gen_install(ctx, (PyObject *) agen, state) < 0)
    goto drive_error;

  /* A throw ingress primed the exception: a frame waiting on a
     resume takes it there and throws it into its delegate; any
     other frame raises it at the resume point as before. */
  if (throwing)
    (void) KosContext_ThrowIntoResume(ctx);

  /* The frozen install pushed the Py_None placeholder for the
     suspended yield expression; replace it with the sent value. The
     throw ingress sends no value. */
  if (send_val) {
    KosDynFrame *frame = KosContext_FRAME(ctx);
    if (frame && KosDynFrame_RESUME_KIND(frame)) {
      /* waiting on a resume: the sent value is its delivery */
      Py_INCREF(send_val);
      KosContext_StageDelivery(ctx, send_val);
    } else {
      PyObject *popped = KosContext_StackPopRef(ctx);
      if (! popped)
        goto drive_error;
      Py_DECREF(popped);
      if (KosContext_StackPushRef(ctx, send_val) < 0)
        goto drive_error;
    }
  }

  return KOS_ITER_INSTALLED;

drive_error:
  /* Every failed drive releases the busy mark; a wedged one would
     refuse all further iteration of a still-live generator. */
  state->busy = 0;
  return KOS_ITER_ERROR;
}


static KosIterStepResult agen_iter_step(
    KosContext *ctx,
    PyObject *self,
    PyObject *send_val,
    PyObject **out) {

  return agen_iter_drive(ctx, (KosPyAsyncGen *) self, send_val, 0, out);
}


static KosIterStepResult agen_iter_throw(
    KosContext *ctx,
    PyObject *self,
    PyObject *throw_typ,
    PyObject *throw_exc,
    PyObject **out) {

  KosPyAsyncGen *agen = (KosPyAsyncGen *) self;

  if (agen_iter_busy(self))
    return KOS_ITER_ERROR;

  if (agen->closed) {
    *out = NULL;
    return KOS_ITER_EXHAUSTED;
  }

  /* Throw ingress: prime the exception, then attach; the pending error
     is delivered at the resume point inside the flattened drive. A
     generator the drive finds spent takes no delivery: a throw into a
     finished generator completes quietly, and EXHAUSTED carries no
     exception. */
  PyErr_SetObject(throw_typ, throw_exc);
  {
    KosIterStepResult r = agen_iter_drive(ctx, agen, NULL, 1, out);
    if (r == KOS_ITER_EXHAUSTED)
      PyErr_Clear();
    return r;
  }
}


static int agen_iter_close(PyObject *self) {

  KosPyAsyncGen *agen = (KosPyAsyncGen *) self;

  /* Declined while another site is driving; that drive's completion
     owns the state. */
  if (agen->state.busy)
    return 0;

  KosPyAsyncGen_Teardown(agen);
  agen->closed = 1;
  return 0;
}


/**
   The suite's ki_finish: the step is over, and the generator is closed
   when it was the end. An exhausted finish from the Python-driven
   completion frees the completed chain on the wrapper's context, the
   GEN node with it, so the generator's locals go when it finishes
   rather than when the object dies. A Koskari consumer's completion
   left nothing on the wrapper's context: the teardown freed the span
   and the node from the consumer's chain. A context whose recycle is
   refused keeps its chain, left for the teardown.
*/
static void agen_iter_finish(PyObject *self, int exhausted) {

  KosPyAsyncGen *agen = (KosPyAsyncGen *) self;

  agen->state.busy = 0;
  if (! exhausted)
    return;

  agen->closed = 1;

  if (agen->ctx && KosContext_DYN_TOP(agen->ctx))
    (void) KosContext_Recycle(agen->ctx);
}


/**
   The wrapper's context, for a step from Python: created on the first
   such step with the parameterize cells in force then, and kept for
   the generator's life. A generator only ever spliced from
   Koskari never has one. NULL with the error set on a creation
   failure, and the step fails: the generator's span runs only on
   this context or on a Koskari consumer's chain. The Python adapter
   that borrows this context holds the wrapper, and releases the
   context first.
*/
static KosContext *agen_iter_pyctx(PyObject *self) {

  KosPyAsyncGen *agen = (KosPyAsyncGen *) self;

  if (! agen->ctx)
    agen->ctx = KosContext_New(NULL, kos_tls_current());
  return agen->ctx;
}


KosEvaluatedTypeObject KosPyAsyncGenType = {
  .type = {
    PyVarObject_HEAD_INIT(NULL, 0)

    .tp_name = "koskari.lib.AsyncGenerator",
    .tp_basicsize = sizeof(KosPyAsyncGen),
    .tp_itemsize = 0,
    .tp_dealloc = asyncgen_dealloc,
    .tp_finalize = asyncgen_finalize,
    .tp_repr = asyncgen_repr,
    .tp_as_async = &asyncgen_async_methods,
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC,
    .tp_doc = "Koskari async generator wrapper",
    .tp_traverse = asyncgen_traverse,
    .tp_clear = asyncgen_clear,
    .tp_iter = KosIterAdapt_PyIter,
    .tp_methods = asyncgen_methods,
    .tp_getset = asyncgen_getsets,
    .tp_base = KOS_EVALUATED_PYTYPE(KosEvaluatedType),
  },
  .kos_iter = {
    .ki_step = agen_iter_step,
    .ki_throw = agen_iter_throw,
    .ki_close = agen_iter_close,
    .ki_finish = agen_iter_finish,
    .ki_pyctx = agen_iter_pyctx,
    .ki_busy = agen_iter_busy,
  },
};


PyObject *KosPyAsyncGen_New(
    KosContext *ctx,
    KosDynFrame *frame,
    PyObject *fn) {

  KosPyAsyncGen *agen;

  if (fn && KosFunction_DEAD_RECORD(fn)) {
    agen = (KosPyAsyncGen *) KosFunction_DEAD_RECORD(fn);
    KosFunction_DEAD_RECORD(fn) = NULL;
    KOS_TRACE_FL_ALLOC("asyncgen", 1);
    _Py_NewReference((PyObject *) agen);
    kos_gc_clear_finalized((PyObject *) agen);
  } else {
    KOS_TRACE_FL_ALLOC("asyncgen", 0);
    agen = PyObject_GC_New(KosPyAsyncGen,
                           KOS_EVALUATED_PYTYPE(KosPyAsyncGenType));
    if (! agen) {
      Py_XDECREF(ctx);
      kos_dynframe_free(frame);
      return NULL;
    }
  }

  agen->ctx = ctx;
  kos_gen_state_init(&agen->state, frame);
  agen->code_name = NULL;
  agen->fn = Py_XNewRef(fn);
  agen->closed = 0;

  PyObject_GC_Track((PyObject *) agen);
  return (PyObject *) agen;
}


/* === Unit lifecycle === */


int koskari_iter_init(PyObject *mod) {

  PyObject *dict = PyModule_GetDict(mod);
  if (! dict)
    return -1;

  if (PyType_Ready(KOS_EVALUATED_PYTYPE(KosPyAsyncGenType)))
    return -1;

  if (PyDict_SetItemString(dict, "AsyncGenerator",
                           (PyObject *) KOS_EVALUATED_PYTYPE(KosPyAsyncGenType)))
    return -1;

  return 0;
}


int koskari_iter_clear(PyObject *mod) {

  (void) mod;
  return 0;
}


// The end.
