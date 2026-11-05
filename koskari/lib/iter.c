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
   began the step was dropped. A context the collector cleared
   while the step was in flight counts too: it lost the chain without
   the cleanup, which the close reports. A step installed on a
   consumer's chain is marked so (KOS_GEN_BUSY_CONSUMER), and belongs
   to that consumer. The chain test stays: a drive on the wrapper's
   context that ended at a yield froze its span off the chain before
   its finish, and is not left mid-await.
*/
static int asyncgen_parked(KosPyAsyncGen *agen) {

  KosContext *ctx = agen->ctx;

  if (agen->state.busy != KOS_GEN_BUSY_OWN || ! ctx
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
    state->busy = KOS_GEN_IDLE;
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
   a consumer's chain is not in the state while installed, and goes
   with that chain, whose free touches neither the wrapper nor its
   state; one on the wrapper's own context goes with the context.
*/
static void asyncgen_drop_state(KosPyAsyncGen *agen) {

  asyncgen_free_span(agen);
  Py_CLEAR(agen->ctx);
}


/* === PEP 525 hooks === */


/**
   Read the thread's async generator hooks once, on the generator's
   first method call or drive, as CPython's async_gen_init_hooks does:
   the finalizer is kept for the generator's finalizer, and firstiter
   is called with the generator, its error failing the call that
   started it. An event loop registers the generator through firstiter
   and, through the finalizer, schedules its aclose when it is dropped.

   :return: 0, or -1 with firstiter's error set
*/
static int asyncgen_init_hooks(KosPyAsyncGen *agen) {

  PyThreadState *tstate;
  PyObject *firstiter;
  PyObject *res;

  if (agen->hooks_inited)
    return 0;
  agen->hooks_inited = 1;

  tstate = PyThreadState_Get();
  if (tstate->async_gen_finalizer)
    agen->finalizer = Py_NewRef(tstate->async_gen_finalizer);

  firstiter = tstate->async_gen_firstiter;
  if (! firstiter)
    return 0;

  Py_INCREF(firstiter);
  res = PyObject_CallOneArg(firstiter, (PyObject *) agen);
  Py_DECREF(firstiter);
  if (! res)
    return -1;
  Py_DECREF(res);
  return 0;
}


/**
   Hand the dropped generator to its finalizer hook in place of the
   synchronous close, as CPython's _PyGen_Finalize does; an event
   loop's hook schedules its aclose. The hook's error is reported as
   unraisable against the generator, and the caller's pending
   exception is kept across the call. The reference is let go once
   called: a finalizer runs once.
*/
static void asyncgen_call_finalizer(KosPyAsyncGen *agen) {

  PyObject *finalizer = agen->finalizer;
  PyObject *typ, *val, *tb;
  PyObject *res;

  agen->finalizer = NULL;

  PyErr_Fetch(&typ, &val, &tb);
  res = PyObject_CallOneArg(finalizer, (PyObject *) agen);
  if (res)
    Py_DECREF(res);
  else
    PyErr_WriteUnraisable((PyObject *) agen);
  Py_DECREF(finalizer);
  PyErr_Restore(typ, val, tb);
}


/**
   Finalizer: a generator dropped while suspended is closed before its
   state is torn down, the way CPython closes an abandoned async
   generator, so its finally blocks, with exits and wind after thunks
   run against its frame as it was. GeneratorExit is thrown at the
   suspension point through the same drive aclose uses, and nothing
   comes back out of it: an error the cleanup raises, or a suspend the
   close cannot honor, is reported as unraisable. A finished or closed
   generator has nothing left to unwind, and one mid-drive belongs to
   that drive.

   A step left mid-await on the wrapper's context (asyncgen_parked) is
   the generator's own to close, synchronously, finalizer hook or
   not: nothing can resume an abandoned step, and an aclose the hook
   scheduled would be refused as already running and lose the
   cleanup. A step installed on a consumer's chain is the consumer's,
   closed with that chain.

   A generator at rest, suspended at a yield or unstarted with its
   hooks read, that took a finalizer hook is handed to the hook
   instead, as CPython hands it: under an event loop its aclose then
   runs as a task, where a cleanup that awaits completes.
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
  if (! state->active && ! (agen->ctx && KosContext_DYN_TOP(agen->ctx)))
    return;

  if (agen->finalizer && state->active) {
    asyncgen_call_finalizer(agen);
    return;
  }

  if (state->active && ! state->started) {
    /* CPython's close marks a never-started frame completed */
    agen->closed = 1;
    return;
  }

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

  /* weak references go before the finalizer, as CPython's generators
     let them go */
  if (agen->weakreflist)
    PyObject_ClearWeakRefs(self);

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
  Py_CLEAR(agen->finalizer);

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
  Py_VISIT(agen->finalizer);

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
  Py_CLEAR(agen->finalizer);
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


/* Each protocol method reads the hooks before it makes its awaitable,
   as CPython's do, so firstiter sees the generator at the first call */


static PyObject *asyncgen_anext(PyObject *self) {

  if (asyncgen_init_hooks((KosPyAsyncGen *) self) < 0)
    return NULL;
  return KosIterAdapt_Anext(self);
}


static PyObject *asyncgen_asend(PyObject *self, PyObject *value) {

  if (asyncgen_init_hooks((KosPyAsyncGen *) self) < 0)
    return NULL;
  return KosIterAdapt_Asend(self, value);
}


static PyObject *asyncgen_athrow(PyObject *self, PyObject *args) {

  if (asyncgen_init_hooks((KosPyAsyncGen *) self) < 0)
    return NULL;
  return KosIterAdapt_Athrow(self, args);
}


static PyObject *asyncgen_aclose(PyObject *self, PyObject *ignored) {

  if (asyncgen_init_hooks((KosPyAsyncGen *) self) < 0)
    return NULL;
  return KosIterAdapt_Aclose(self, ignored);
}


static PyMethodDef asyncgen_methods[] = {
  { "asend", (PyCFunction) asyncgen_asend, METH_O,
    "Send a value into the async generator; returns an awaitable" },

  { "athrow", (PyCFunction) asyncgen_athrow, METH_VARARGS,
    "Throw an exception into the async generator; returns an awaitable" },

  { "aclose", (PyCFunction) asyncgen_aclose, METH_NOARGS,
    "Close the async generator; returns an awaitable" },

  { NULL, NULL, 0, NULL },
};


static PyAsyncMethods asyncgen_async_methods = {
  .am_aiter = asyncgen_aiter,
  .am_anext = asyncgen_anext,
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

  /* where the step runs: a Python adapter's drive on the wrapper's
     context, or a consumer's chain */
  state->busy = (ctx == agen->ctx)
      ? KOS_GEN_BUSY_OWN : KOS_GEN_BUSY_CONSUMER;

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
  state->busy = KOS_GEN_IDLE;
  return KOS_ITER_ERROR;
}


static KosIterStepResult agen_iter_step(
    KosContext *ctx,
    PyObject *self,
    PyObject *send_val,
    PyObject **out) {

  KosPyAsyncGen *agen = (KosPyAsyncGen *) self;

  /* a Koskari consumer's first step reads the hooks, as a Python
     method's does */
  if (! agen->hooks_inited && asyncgen_init_hooks(agen) < 0)
    return KOS_ITER_ERROR;
  return agen_iter_drive(ctx, agen, send_val, 0, out);
}


static KosIterStepResult agen_iter_throw(
    KosContext *ctx,
    PyObject *self,
    PyObject *throw_typ,
    PyObject *throw_exc,
    PyObject **out) {

  KosPyAsyncGen *agen = (KosPyAsyncGen *) self;

  if (! agen->hooks_inited && asyncgen_init_hooks(agen) < 0)
    return KOS_ITER_ERROR;

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
    /* a step on a consumer's chain ends here on the one compare; one
       on the wrapper's context is left mid-await only while its chain
       is still there (asyncgen_parked), a yield having frozen it off */
    if (agen->state.busy != KOS_GEN_BUSY_OWN || ! asyncgen_parked(agen))
      agen->state.busy = KOS_GEN_IDLE;
    return;
  }

  agen->state.busy = KOS_GEN_IDLE;
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
    .tp_weaklistoffset = offsetof(KosPyAsyncGen, weakreflist),
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
  agen->finalizer = NULL;
  agen->weakreflist = NULL;
  agen->closed = 0;
  agen->hooks_inited = 0;

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
