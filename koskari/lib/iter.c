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

  if (ctx)
    KosContext_ReleaseChain(ctx);
}


/**
   Teardown a suspended async generator: install its span on ctx, throw
   GeneratorExit, run to completion, or fall back to freeing the span
   if the body attempts to suspend (yield/await).

   Assumes a started span resting in the state and ctx's chain empty.

   This is the demand-path close: the GeneratorExit drive runs
   synchronously to completion, and a suspend attempt during close takes
   the fallback (cannot honor). What the close owes, an ignored
   GeneratorExit or an error the cleanup raised, is reported as
   unraisable once the chain and span are freed, as CPython's finalizer
   reports it (kos_close_drive, kos_close_report). All koskari
   generators are async, so dealloc must still run their teardown; a
   future explicit aclose is where a genuinely suspending close could
   be honored.

   Safe at arbitrary points, including dealloc mid-unwind: the caller's
   pending exception is fenced (fetched before the drive, restored
   after), so the drive never observes it and the internal PyErr_Clear
   calls cannot destroy it.

   On success (primary path): chain unwound, GEN node freed with it,
   ctx ready for free. On fallback: chain and span freed, ctx ready for
   free. Does not free ctx; caller does that.
*/
static void asyncgen_teardown_chain_on_ctx(KosPyAsyncGen *agen, KosContext *ctx) {

  PyObject *fence_typ = NULL;
  PyObject *fence_val = NULL;
  PyObject *fence_tb = NULL;
  PyObject *typ = NULL;
  PyObject *val = NULL;
  PyObject *tb = NULL;

  PyErr_Fetch(&fence_typ, &fence_val, &fence_tb);

  /* Install the span onto ctx so the chain is restored before eval; in
     the flatten path the chain rests in the state and agen->ctx was
     never the running ctx. */
  KosContext_EvalEnter(ctx, NULL);
  if (kos_dyn_gen_install(ctx, (PyObject *) agen, &agen->state) < 0) {
    KosContext_EvalExit(ctx);
    PyErr_Clear();
    goto fallback;
  }
  /* the close drive begins and ends a drive of its own */
  KosContext_EvalExit(ctx);

  if (kos_close_drive(ctx, &typ, &val, &tb))
    goto fallback;

  KosContext_EvalEnter(ctx, NULL);
  if (kos_dyn_teardown_gen_exhausted(ctx) < 0) {
    KosContext_EvalExit(ctx);
    goto fallback;
  }
  KosContext_EvalExit(ctx);
  kos_close_report((PyObject *) agen, typ, val, tb);
  PyErr_Restore(fence_typ, fence_val, fence_tb);
  return;

fallback:
  PyErr_Clear();
  /* an install that went through left the span on ctx, GEN node and
     all; a refused one, or a yield's freeze, left it resting */
  asyncgen_free_chain_on(ctx);
  asyncgen_free_span(agen);
  kos_close_report((PyObject *) agen, typ, val, tb);
  PyErr_Restore(fence_typ, fence_val, fence_tb);
}


/**
   Is the generator left mid-await on its own context with nothing
   driving it? A step is in flight (busy) and its chain sits on the
   wrapper's context, which no drive is running: the stepper that
   began the step was dropped, or a consumer's finalizer moved the
   span here (KosPyAsyncGen_Park). A context the collector cleared
   while the step was in flight counts too: it lost the chain without
   the cleanup, which the close reports. A step installed on a
   consumer's chain leaves the wrapper's context empty, and belongs to
   that consumer.
*/
static int asyncgen_parked(KosPyAsyncGen *agen) {

  KosContext *ctx = agen->ctx;

  if (! agen->state.busy || ! ctx
      || (KosContext_FLAGS(ctx) & KOS_CTX_RUNNING))
    return 0;
  return KosContext_DYN_TOP(ctx)
      || (KosContext_FLAGS(ctx) & KOS_CTX_CLEARED);
}


void KosPyAsyncGen_Teardown(KosPyAsyncGen *agen) {

  KosGenState *state;

  if (! agen)
    return;
  state = &agen->state;

  /* a live chain on the wrapper's context with no step marked, or a
     step left there mid-await */
  if (state->busy ? asyncgen_parked(agen)
      : (agen->ctx && KosContext_DYN_TOP(agen->ctx))) {
    /* Live chain: the generator was left mid-await, where the
       resting-span teardown below cannot reach it. Close it through
       the suspended context, exception-fenced; whatever the close
       left behind, the live chain or a span a yield in its cleanup
       froze, is freed, and what the close owes is then reported.
       The step is over either way: a close that wound the chain down
       finished it through the GEN, and one that could not leaves
       nothing to resume, so the busy mark goes. */
    PyObject *fence_typ, *fence_val, *fence_tb;
    PyObject *typ, *val, *tb;

    PyErr_Fetch(&fence_typ, &fence_val, &fence_tb);
    (void) kos_close_drive(agen->ctx, &typ, &val, &tb);
    asyncgen_free_chain_on(agen->ctx);
    asyncgen_free_span(agen);
    state->busy = 0;
    kos_close_report((PyObject *) agen, typ, val, tb);
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
   suspension point through the same drive aclose uses, and nothing
   comes back out of it: an error the cleanup raises, or a suspend the
   close cannot honor, is reported as unraisable. A generator never
   started has entered nothing, a finished or closed one has nothing
   left to unwind, and one mid-drive belongs to that drive.

   A step left mid-await on the wrapper's context (asyncgen_parked) is
   the generator's own to close, as CPython's finalizer closes a
   generator whose asend awaitable was dropped mid-await; a step
   installed on a consumer's chain is the consumer's.
*/
static void asyncgen_finalize(PyObject *self) {

  KosPyAsyncGen *agen = (KosPyAsyncGen *) self;
  KosGenState *state = &agen->state;

  if (agen->closed)
    return;
  if (state->busy) {
    if (asyncgen_parked(agen)) {
      KosPyAsyncGen_Teardown(agen);
      agen->closed = 1;
    }
    return;
  }
  if (state->active && ! state->started) {
    /* CPython's close marks a never-started frame completed */
    agen->closed = 1;
    return;
  }
  if (! (agen->ctx && KosContext_DYN_TOP(agen->ctx)) && ! state->active)
    return;

  KosPyAsyncGen_Teardown(agen);
  agen->closed = 1;
}


static void asyncgen_dealloc(PyObject *self) {

  KosPyAsyncGen *agen = (KosPyAsyncGen *) self;
  PyObject *fn;

  /* A chain of generators, each bound frame holding the previous one,
     releases one generator per link: a frame returning to its
     function's cache drops its locals in place, and a suspended link's
     close frees the next link from inside the finalizer. The trashcan
     wraps the finalizer too, as CPython's subtype_dealloc does, so
     that recursion is deferred off the C stack. It needs the
     generator untracked. */
  PyObject_GC_UnTrack(self);
  Py_TRASHCAN_BEGIN(self, asyncgen_dealloc);

  /* Run the finalizer first. It may resurrect the generator, in which
     case the dealloc is abandoned here and happens on the next drop;
     a resurrected object must be tracked, so the finalizer runs
     tracked. */
  PyObject_GC_Track(self);
  if (PyObject_CallFinalizerFromDealloc(self) < 0)
    goto done;
  PyObject_GC_UnTrack(self);

  asyncgen_drop_state(agen);
  Py_CLEAR(agen->code_name);

  /* A function's generator is stored on the function when its slot is
     free: untracked, every field clear, refcount zero, the next call
     revives it. The function's own release frees a stored one, so the
     reference dropped here may free both at once. */
  fn = agen->fn;
  agen->fn = NULL;
  if (fn) {
    if (! KosFunction_DEAD_RECORD(fn)) {
      KosFunction_DEAD_RECORD(fn) = self;
      KOS_TRACE_FL_RETURN("asyncgen", 0);
      Py_DECREF(fn);
      goto done;
    }
    KOS_TRACE_FL_RETURN("asyncgen", 1);
    Py_DECREF(fn);
  }

  Py_TYPE(self)->tp_free(self);

done:
  Py_TRASHCAN_END;
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


/* === KosIter suite === */


/**
   The suite's ki_busy: 1 with the RuntimeError set while another site
   is driving the generator, else 0. A Python method asking gets
   CPython's wording for it, "anext(): asynchronous generator is
   already running" and the like; a site with no method behind it
   gets the generic text.
*/
static int agen_iter_busy(PyObject *self, const char *method) {

  KosPyAsyncGen *agen = (KosPyAsyncGen *) self;

  if (! agen->state.busy)
    return 0;

  if (method)
    PyErr_Format(PyExc_RuntimeError,
                 "%s(): asynchronous generator is already running",
                 method);
  else
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

  if (agen_iter_busy((PyObject *) agen, NULL))
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

  /* The installed frame sits at a yield, whichever context it last
     yielded on. A context that is not running enters the evaluator
     next and dispatches its send or throw on the suspension event, so
     the event says yield here: a span frozen by a flattened consumer
     never left an evaluator, and the context it lands on may never
     have run. A running consumer threads into the frame directly. */
  if (! (KosContext_FLAGS(ctx) & KOS_CTX_RUNNING))
    KosContext_SetSuspensionEvent(ctx, KOS_SUSP_YIELD);

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
        goto install_error;
      Py_DECREF(popped);
      if (KosContext_StackPushRef(ctx, send_val) < 0)
        goto install_error;
    }
  }

  return KOS_ITER_INSTALLED;

install_error:
  /* The span is spliced and the frame has not run: put it back to
     rest, and take back the yield event set above, so the generator
     stays suspended where it was and the next drive installs afresh. */
  kos_dyn_gen_unsplice(ctx, state);
  if (! (KosContext_FLAGS(ctx) & KOS_CTX_RUNNING))
    KosContext_SetSuspensionEvent(ctx, KOS_SUSP_NONE);

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

  if (agen_iter_busy(self, NULL))
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

   A step left mid-await on the wrapper's context (asyncgen_parked) is
   not the finishing consumer's: a consumer letting go of it, its
   request finished as it unwinds, ends nothing, and the generator
   stays running until it is closed, as a CPython async generator
   whose asend awaitable was dropped does.
*/
static void agen_iter_finish(PyObject *self, int exhausted) {

  KosPyAsyncGen *agen = (KosPyAsyncGen *) self;

  if (! exhausted) {
    /* the chain test first: a step that ended at a yield, or ran on a
       consumer's chain, left the wrapper's context empty */
    if (! (agen->ctx && KosContext_DYN_TOP(agen->ctx)
           && asyncgen_parked(agen)))
      agen->state.busy = 0;
    return;
  }

  agen->state.busy = 0;
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


/* === A step left on a consumer's chain === */


int KosPyAsyncGen_Park(
    KosPyAsyncGen *agen,
    KosContext *from,
    KosDynGen *gen) {

  KosGenState *state = &agen->state;
  KosContext *own = agen->ctx;
  int failed;

  /* the wrapper's context takes the span only when it holds nothing
     of its own: a step on a consumer's chain left it empty */
  if (own && (KosContext_DYN_TOP(own) || KosContext_PY_AWAIT_ITER(own)
              || KosContext_PY_RESUME_INJECT(own)
              || (KosContext_FLAGS(own)
                  & (KOS_CTX_RUNNING | KOS_CTX_CLEARED)))) {
    PyErr_SetString(PyExc_SystemError,
                    "async generator context is not free to take its"
                    " suspended step");
    return -1;
  }

  if (! own) {
    /* the parameter cells the step ran under go with it */
    own = KosContext_NewFromSnapshot(
        NULL, Py_XNewRef(KosContext_PARAM_REFS(from)));
    if (! own)
      return -1;
    agen->ctx = own;
  }

  /* the span leaves the consumer's chain into the state, and the
     consumer's registers come back */
  if (kos_dyn_gen_freeze(from, gen) < 0)
    return -1;

  /* and goes onto the wrapper's context, renumbered for its empty
     base. The install's room check reads the drive's limit. */
  KosContext_EvalEnter(own, NULL);
  failed = kos_dyn_gen_install_awaiting(own, (PyObject *) agen, state);
  KosContext_EvalExit(own);
  if (failed < 0) {
    /* the span goes back where it was, at the same depth, so the
       consumer's close still reaches it; the drive state never left */
    PyObject *typ, *val, *tb;

    PyErr_Fetch(&typ, &val, &tb);
    KosContext_EvalEnter(from, NULL);
    if (kos_dyn_gen_install_awaiting(from, (PyObject *) agen, state) < 0)
      PyErr_Clear();
    KosContext_EvalExit(from);
    PyErr_Restore(typ, val, tb);
    return -1;
  }

  /* What the suspension holds outside the chain goes with the span:
     the await in flight belongs to the innermost frame, which is the
     innermost generator's, parked first; a generator further out was
     waiting on that one's element, and takes the suspension mark
     alone. The handler-visible exception state is the innermost
     frame's view, and goes with it too. */
  KosContext_PY_AWAIT_ITER(own) = KosContext_PY_AWAIT_ITER(from);
  KosContext_PY_AWAIT_ITER(from) = NULL;
  KosContext_PY_RESUME_INJECT(own) = KosContext_PY_RESUME_INJECT(from);
  KosContext_PY_RESUME_INJECT(from) = NULL;
  KosExc_Clear(KosContext_EXC_INFO(own));
  *KosContext_EXC_INFO(own) = *KosContext_EXC_INFO(from);
  KosExc_Init(KosContext_EXC_INFO(from));
  KosContext_SetSuspensionEvent(own, KOS_SUSP_AWAIT);
  KosContext_MarkSuspendedNotRunning(own);

  /* A generator whose finalizer already ran, while its span sat on
     the consumer's chain out of its reach (both unreachable in one
     collection, its finalizer first), is owed the close that finalizer
     could not make; a finalizer runs once, so it is made here. */
  if (PyObject_GC_IsFinalized((PyObject *) agen)) {
    KosPyAsyncGen_Teardown(agen);
    agen->closed = 1;
  }

  return 0;
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
