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

   KosPyAsyncGen and KosPyAsyncGenASend. Async iteration and tp_iter
   DemandGenerator for synchronous for-loops.

   :author: Christopher O'Brien <obriencj@preoccupied.net>
   :license: GNU General Public License v3
*/


#include "lib.h"


static int install_frozen_gen_for_resume(
    KosContext *eval_ctx,
    KosPyAsyncGen *agen) {

  KosDynGen *g;

  if (! eval_ctx || ! agen)
    return 0;
  g = (KosDynGen *) agen->gen;
  {
    KosEvalState es;
    int r;

    KosEvalState_Init(&es, eval_ctx);
    KosEvalState_ThawFromContext(&es);
    if (! g || ! kos_dyn_gen_frozen_needs_install_es(&es, g)) {
      KosEvalState_FreezeToContext(&es);
      return 0;
    }
    r = kos_dyn_gen_install(&es, g);
    KosEvalState_FreezeToContext(&es);
    return r;
  }
}


static PyObject *asyncgenasend_new_ex(
    KosPyAsyncGen *agen,
    PyObject *send_value,
    PyObject *throw_typ,
    PyObject *throw_exc,
    int closing);

static int asyncgen_close_drive(KosPyAsyncGen *agen);


/**
   Parse and normalize throw-method arguments (typ[, val[, tb]]) into an
   owned (type, exception-instance) pair shaped for the evaluator's
   throw ingress. Accepts an exception class with optional value, or a
   bare exception instance. format carries the method name for error
   messages, e.g. "O|OO:athrow".
*/
static int normalize_throw_args(
    PyObject *args,
    const char *format,
    PyObject **typ_out,
    PyObject **exc_out) {

  PyObject *typ = NULL, *val = NULL, *tb = NULL;

  if (! PyArg_ParseTuple(args, format, &typ, &val, &tb))
    return -1;

  if (PyExceptionClass_Check(typ)) {
    Py_INCREF(typ);
    Py_XINCREF(val);
    Py_XINCREF(tb);
    PyErr_NormalizeException(&typ, &val, &tb);
    Py_XDECREF(tb);
    if (! typ)
      return -1;
    if (! val) {
      val = typ;
      Py_INCREF(val);
    }
    *typ_out = typ;
    *exc_out = val;
    return 0;
  }

  if (PyExceptionInstance_Check(typ)) {
    if (val && val != Py_None) {
      PyErr_SetString(PyExc_TypeError,
                      "instance exception may not have a separate value");
      return -1;
    }
    Py_INCREF(typ);
    *exc_out = typ;
    *typ_out = (PyObject *) Py_TYPE(typ);
    Py_INCREF(*typ_out);
    return 0;
  }

  PyErr_SetString(PyExc_TypeError,
                  "exceptions must be classes or instances deriving "
                  "from BaseException");
  return -1;
}


/* === KosPyAsyncGen type === */


/**
   Dissociate a frozen async generator from its context without running
   the extent protocol: detach and free the frozen chain, release the
   gen. Leaves ctx ready for KosContext_Free.

   Safe at arbitrary points, including dealloc mid-unwind with a live
   exception: no Python calls, no evaluator re-entry, and the pending
   PyErr is never touched.
*/
static void asyncgen_dissociate_chain(KosPyAsyncGen *agen) {

  KosDynGen *gen = (KosDynGen *) agen->gen;
  KosDynNode *active;

  if (! gen)
    return;
  active = (KosDynNode *) KosDynGen_ACTIVE(gen);
  KosDynGen_ACTIVE(gen) = NULL;
  if (active)
    kos_dynchain_free(active, (KosDynNode *) gen);
  kos_dyngen_release(gen);
  agen->gen = NULL;
}


/**
   Teardown a frozen async generator: throw GeneratorExit, run to completion,
   or fall back to pure cleanup if the gen attempts to suspend (yield/await).

   Assumes agen is frozen: agen->gen set, GEN.active holds chain,
   KosContext_DYN_TOP(agen->ctx) == NULL.

   This is the demand-path close: the GeneratorExit drive runs
   synchronously to completion, and a suspend attempt during close takes
   the fallback (cannot honor). All koskari generators are async, so
   dealloc must still run their teardown; a future explicit aclose is
   where a genuinely suspending close could be honored.

   Safe at arbitrary points, including dealloc mid-unwind: the caller's
   pending exception is fenced (fetched before the drive, restored
   after), so the drive never observes it and the internal PyErr_Clear
   calls cannot destroy it.

   On success (primary path): chain unwound, gen released, ctx ready for free.
   On fallback: chain freed, gen released, ctx ready for free.
   Does not free ctx; caller does that. For clear, caller also sets
   agen->gen = NULL and agen->ctx = NULL.
*/
static void asyncgen_teardown_chain_on_ctx(KosPyAsyncGen *agen, KosContext *ctx) {

  KosDynGen *gen = (KosDynGen *) agen->gen;
  PyObject *throw_exc = NULL;
  PyObject *fence_typ = NULL;
  PyObject *fence_val = NULL;
  PyObject *fence_tb = NULL;

  if (! gen || ! KosDynGen_ACTIVE(gen)) {
    /* No active chain; release and let caller free ctx */
    if (gen)
      kos_dyngen_release(gen);
    agen->gen = NULL;
    return;
  }

  PyErr_Fetch(&fence_typ, &fence_val, &fence_tb);

  throw_exc = PyObject_CallNoArgs(PyExc_GeneratorExit);
  if (! throw_exc) {
    PyErr_Clear();
    goto fallback;
  }

  /* Install gen onto ctx so chain is restored before eval; in flatten path
     the chain lived in GEN->active and agen->ctx was never the running ctx. */
  {
    KosEvalState es;

    KosEvalState_Init(&es, ctx);
    KosEvalState_ThawFromContext(&es);
    if (kos_dyn_gen_install(&es, gen) < 0) {
      KosEvalState_FreezeToContext(&es);
      PyErr_Clear();
      Py_DECREF(throw_exc);
      goto fallback;
    }
    KosEvalState_FreezeToContext(&es);
  }

  {
    KosOpResult res = koskari_eval(ctx, NULL, PyExc_GeneratorExit, throw_exc);
    Py_DECREF(throw_exc);

    switch (res.kind) {
      case KOS_RES_RAISE:
      case KOS_RES_FATAL:
        /* RAISE means PyErr is set; clear it (GeneratorExit/StopIteration = success) */
        PyErr_Clear();
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
  {
    KosEvalState es;

    KosEvalState_Init(&es, ctx);
    KosEvalState_ThawFromContext(&es);
    if (kos_dyn_teardown_gen_exhausted(&es) < 0) {
      KosEvalState_FreezeToContext(&es);
      goto fallback;
    }
    KosEvalState_FreezeToContext(&es);
  }
  /* unwind_exhausted released only the install reference acquired above by
     kos_dyn_gen_install; the owner reference held by agen still remains.
     Release it now that the gen is consumed, matching the fallback path and
     the non-frozen branch in the callers. */
  kos_dyngen_release(gen);
  agen->gen = NULL;
  PyErr_Restore(fence_typ, fence_val, fence_tb);
  return;

fallback:
  PyErr_Clear();
  asyncgen_dissociate_chain(agen);
  PyErr_Restore(fence_typ, fence_val, fence_tb);
}


void KosPyAsyncGen_Teardown(KosPyAsyncGen *agen) {

  if (! agen)
    return;

  if (agen->ctx && KosContext_DYN_TOP(agen->ctx) && ! agen->in_progress) {
    /* Live chain: the generator was abandoned mid-await, where the
       frozen-chain teardown below cannot reach it. Demand-drive the
       close through the suspended context, exception-fenced --
       teardown runs from dealloc, so every outcome of the drive is
       swallowed and cleared. */
    PyObject *fence_typ, *fence_val, *fence_tb;
    PyErr_Fetch(&fence_typ, &fence_val, &fence_tb);
    (void) asyncgen_close_drive(agen);
    PyErr_Clear();
    PyErr_Restore(fence_typ, fence_val, fence_tb);
  }

  if (agen->gen && agen->ctx && ! KosContext_DYN_TOP(agen->ctx)) {
    asyncgen_teardown_chain_on_ctx(agen, agen->ctx);

  } else if (agen->gen) {
    kos_dyngen_release((KosDynGen *) agen->gen);
    agen->gen = NULL;
  }

  if (agen->ctx) {
    KosContext_Free(agen->ctx);
    agen->ctx = NULL;
  }
}


static void asyncgen_dealloc(PyObject *self) {

  PyObject_GC_UnTrack(self);

  KosPyAsyncGen *agen = (KosPyAsyncGen *) self;

  /* demand-path close; exception-fenced, so safe mid-unwind. A live
     (abandoned mid-await) chain is demand-driven closed first. */
  KosPyAsyncGen_Teardown(agen);

  Py_XDECREF(agen->code_name);

  Py_TYPE(self)->tp_free(self);
}


static int asyncgen_traverse(PyObject *self, visitproc visit, void *arg) {

  KosPyAsyncGen *agen = (KosPyAsyncGen *) self;
  int res;

  Py_VISIT(agen->code_name);

  if (agen->gen && agen->ctx && ! KosContext_DYN_TOP(agen->ctx)) {
    res = kos_dynnode_traverse((KosDynNode *) agen->gen, visit, arg);
    if (res)
      return res;
  }

  if (agen->ctx)
    return KosContext_Traverse(agen->ctx, visit, arg);

  return 0;
}


static int asyncgen_clear(PyObject *self) {

  KosPyAsyncGen *agen = (KosPyAsyncGen *) self;

  Py_CLEAR(agen->code_name);

  KosPyAsyncGen_Teardown(agen);

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

  return KosPyAsyncGenASend_New(agen, NULL);
}


static PyObject *asyncgen_asend(PyObject *self, PyObject *value) {

  /* No call-time closed check: CPython raises StopAsyncIteration when
     the awaitable is driven, not when it is constructed. The stepper's
     entry guard covers the closed case. */
  return asyncgenasend_new_ex((KosPyAsyncGen *) self, value, NULL, NULL, 0);
}


static PyObject *asyncgen_athrow(PyObject *self, PyObject *args) {

  KosPyAsyncGen *agen = (KosPyAsyncGen *) self;
  PyObject *typ = NULL, *exc = NULL;

  if (normalize_throw_args(args, "O|OO:athrow", &typ, &exc) < 0)
    return NULL;

  PyObject *stepper = asyncgenasend_new_ex(agen, NULL, typ, exc, 0);
  Py_DECREF(typ);
  Py_DECREF(exc);
  return stepper;
}


static PyObject *asyncgen_aclose(PyObject *self, PyObject *ignored) {

  KosPyAsyncGen *agen = (KosPyAsyncGen *) self;

  PyObject *exc = PyObject_CallNoArgs(PyExc_GeneratorExit);
  if (! exc)
    return NULL;

  PyObject *stepper = asyncgenasend_new_ex(
      agen, NULL, PyExc_GeneratorExit, exc, 1);
  Py_DECREF(exc);
  return stepper;
}


static PyObject *asyncgen_repr(PyObject *self) {

  KosPyAsyncGen *agen = (KosPyAsyncGen *) self;

  if (agen->code_name && PyUnicode_Check(agen->code_name))
    return PyUnicode_FromFormat(
        "<async_generator object %U at %p>", agen->code_name, (void *) self);
  return PyUnicode_FromFormat("<async_generator object at %p>", (void *) self);
}


static PyObject *asyncgen_tp_iter(PyObject *self) {

  return KosDemandGenerator_FromAsyncGen((KosPyAsyncGen *) self);
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
   Clear agen->ctx dyn/frame head and graft gen onto the consumer
   (KosEvalState_CONTEXT(es)). Frozen gens use ACTIVE; bootstrap gens use
   agen->ctx dyn_top only.
*/
static int agen_attach_gen_flatten(
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
   Shared core of ki_step and ki_throw: guard, mark busy, splice the
   generator's frames onto the consumer context. On success the drive is
   in flight (KOS_ITER_INSTALLED); any pending exception set by the
   caller (the throw ingress) is delivered at resume. send_val, when
   given, replaces the install placeholder on TOS as the value of the
   suspended yield expression.
*/
static KosIterStepResult agen_iter_drive(
    KosEvalState *es,
    KosPyAsyncGen *agen,
    PyObject *send_val,
    PyObject **out) {

  if (agen->in_progress) {
    PyErr_SetString(PyExc_RuntimeError,
                    "async generator already being iterated");
    return KOS_ITER_ERROR;
  }

  if (agen->closed || ! agen->ctx || ! agen->gen) {
    *out = NULL;
    return KOS_ITER_EXHAUSTED;
  }

  {
    KosDynGen *gen = (KosDynGen *) agen->gen;

    if (! KosDynGen_ACTIVE(gen) && ! KosContext_DYN_TOP(agen->ctx)) {
      /* No frozen chain and nothing live on the context: spent. */
      *out = NULL;
      return KOS_ITER_EXHAUSTED;
    }

    agen->in_progress = 1;
    if (agen_attach_gen_flatten(es, agen, gen) < 0) {
      agen->in_progress = 0;
      return KOS_ITER_ERROR;
    }
  }

  agen->started = 1;

  if (send_val && ! PyErr_Occurred()) {
    PyObject *popped = KosEvalState_StackPopRef(es);
    if (! popped)
      return KOS_ITER_ERROR;
    Py_DECREF(popped);
    if (KosEvalState_StackPushRef(es, send_val) < 0)
      return KOS_ITER_ERROR;
  }

  return KOS_ITER_INSTALLED;
}


static KosIterStepResult agen_iter_step(
    KosEvalState *es,
    PyObject *self,
    PyObject *send_val,
    PyObject **out) {

  return agen_iter_drive(es, (KosPyAsyncGen *) self, send_val, out);
}


static KosIterStepResult agen_iter_throw(
    KosEvalState *es,
    PyObject *self,
    PyObject *throw_typ,
    PyObject *throw_exc,
    PyObject **out) {

  KosPyAsyncGen *agen = (KosPyAsyncGen *) self;

  if (agen->in_progress) {
    PyErr_SetString(PyExc_RuntimeError,
                    "async generator already being iterated");
    return KOS_ITER_ERROR;
  }

  if (agen->closed || ! agen->ctx || ! agen->gen) {
    *out = NULL;
    return KOS_ITER_EXHAUSTED;
  }

  /* Throw ingress: prime the exception, then attach; the pending error
     is delivered at the resume point inside the flattened drive. */
  PyErr_SetObject(throw_typ, throw_exc);
  return agen_iter_drive(es, agen, NULL, out);
}


static int agen_iter_close(PyObject *self) {

  KosPyAsyncGen_Teardown((KosPyAsyncGen *) self);
  ((KosPyAsyncGen *) self)->closed = 1;
  return 0;
}


static void agen_iter_finish(PyObject *self, int exhausted) {

  KosPyAsyncGen *agen = (KosPyAsyncGen *) self;

  agen->in_progress = 0;
  if (exhausted)
    agen->closed = 1;
}


KosEvaluatedTypeObject KosPyAsyncGenType = {
  .type = {
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
    .tp_iter = asyncgen_tp_iter,
    .tp_methods = asyncgen_methods,
    .tp_base = KOS_EVALUATED_PYTYPE(KosEvaluatedType),
  },
  .kos_iter = {
    .ki_step = agen_iter_step,
    .ki_throw = agen_iter_throw,
    .ki_close = agen_iter_close,
    .ki_finish = agen_iter_finish,
  },
};


PyObject *KosPyAsyncGen_New(KosContext *ctx) {

  KosPyAsyncGen *agen = PyObject_GC_New(KosPyAsyncGen, KOS_EVALUATED_PYTYPE(KosPyAsyncGenType));
  if (! agen) {
    KosContext_Free(ctx);
    return NULL;
  }

  agen->ctx = ctx;
  agen->code_name = NULL;
  agen->in_progress = 0;
  agen->started = 0;
  agen->closed = 0;

  PyObject_GC_Track((PyObject *) agen);
  return (PyObject *) agen;
}


/* === KosPyAsyncGenASend type === */


static void asyncgenasend_dealloc(PyObject *self) {

  PyObject_GC_UnTrack(self);

  KosPyAsyncGenASend *asend = (KosPyAsyncGenASend *) self;

  /* An asend abandoned mid-await owns the generator's in_progress
     mark: release it so the generator is not wedged forever. */
  if (asend->awaiting && asend->agen)
    asend->agen->in_progress = 0;

  Py_XDECREF(asend->agen);
  Py_XDECREF(asend->send_value);
  Py_XDECREF(asend->throw_typ);
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
  Py_VISIT(asend->throw_typ);
  Py_VISIT(asend->throw_exc);

  return 0;
}


static int asyncgenasend_clear(PyObject *self) {

  KosPyAsyncGenASend *asend = (KosPyAsyncGenASend *) self;

  Py_CLEAR(asend->agen);
  Py_CLEAR(asend->send_value);
  Py_CLEAR(asend->throw_typ);
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

  if (! agen) {
    /* Spent by close(); CPython parity for reuse */
    PyErr_SetString(PyExc_RuntimeError,
                    "cannot reuse already awaited __anext__()/asend()");
    return NULL;
  }

  if (! agen->ctx || agen->closed) {
    /* CPython parity: a throw-flavored drive on a finished generator
       completes quietly (bare StopIteration, so awaiting yields None);
       a send-flavored drive raises StopAsyncIteration. */
    if (asend->is_throw)
      PyErr_SetNone(PyExc_StopIteration);
    else
      PyErr_SetNone(PyExc_StopAsyncIteration);
    return NULL;
  }

  /* Re-entrancy guard: a mid-step await keeps in_progress set (the
     generator stays in use across the suspension), and this asend owns
     that mark while its awaiting flag is up. Anyone else is barred. */
  if (agen->in_progress && ! asend->awaiting) {
    PyErr_SetString(PyExc_RuntimeError,
                    "async generator already being iterated");
    return NULL;
  }

  if (! agen->started && ! asend->is_throw
      && asend->send_value && asend->send_value != Py_None) {
    PyErr_SetString(PyExc_TypeError,
                    "can't send non-None value to a just-started "
                    "async generator");
    return NULL;
  }

  agen->in_progress = 1;

  PyObject *send_val = asend->send_value;
  PyObject *throw_typ = asend->throw_typ;
  PyObject *throw_exc = asend->throw_exc;
  asend->send_value = NULL;
  asend->throw_typ = NULL;
  asend->throw_exc = NULL;

  /* Resume-from-await: the wrapper owns driving the in-flight __await__
     iterator, mirroring coroutine_send's WAITING_PY contract. The step
     stays parked (no freeze, no install) until the awaitable completes;
     only then is eval re-entered, with the result as send_val so
     revive_await pushes it for the pending opcode. A throw arriving
     during the await -- primed fields, not the stale is_throw flag,
     which survives the first drive consuming them -- falls through:
     revive_await's throw path delivers it into the iterator inside
     eval. */
  if (asend->awaiting && ! throw_typ) {
    PyObject *it = KosContext_PY_AWAIT_ITER(agen->ctx);
    if (it) {
      PyObject *step = PyObject_CallMethod(
          it, "send", "O", send_val ? send_val : Py_None);
      Py_XDECREF(send_val);
      send_val = NULL;

      if (step)
        /* still awaiting: hand the next item to the loop */
        return step;

      if (! PyErr_ExceptionMatches(PyExc_StopIteration)) {
        /* the awaitable failed: deliver the exception into the
           generator so its handlers see it (raise-at-await shape) */
        PyObject *tb = NULL;
        PyErr_Fetch(&throw_typ, &throw_exc, &tb);
        PyErr_NormalizeException(&throw_typ, &throw_exc, &tb);
        if (throw_exc && tb)
          PyException_SetTraceback(throw_exc, tb);
        Py_XDECREF(tb);
        Py_CLEAR(KosContext_PY_AWAIT_ITER(agen->ctx));
        asend->awaiting = 0;
        /* fall through to eval with the throw primed */
      } else {
        /* awaitable completed: its value becomes the await result */
        PyObject *await_result = NULL;
        if (PyGen_FetchStopIterationValue(&await_result) < 0) {
          agen->in_progress = 0;
          asend->awaiting = 0;
          return NULL;
        }
        if (! await_result) {
          await_result = Py_None;
          Py_INCREF(await_result);
        }
        Py_CLEAR(KosContext_PY_AWAIT_ITER(agen->ctx));
        asend->awaiting = 0;
        send_val = await_result;
      }
    } else {
      /* stale flag with no iterator in flight */
      asend->awaiting = 0;
    }
  }

  if (install_frozen_gen_for_resume(agen->ctx, agen) < 0) {
    agen->in_progress = 0;
    Py_XDECREF(send_val);
    Py_XDECREF(throw_typ);
    Py_XDECREF(throw_exc);
    return NULL;
  }

  /* Drive the context */
  agen->started = 1;
  KosOpResult res = koskari_eval(agen->ctx, send_val, throw_typ, throw_exc);

  Py_XDECREF(send_val);
  Py_XDECREF(throw_typ);
  Py_XDECREF(throw_exc);

  if (res.kind == KOS_RES_AWAIT) {
    /* Mid-step await: the generator stays in use (in_progress held);
       this asend resumes it by driving the iterator above. */
    asend->awaiting = 1;
    return res.val;  /* transfer ownership to caller */
  }

  agen->in_progress = 0;
  asend->awaiting = 0;

  switch (res.kind) {
    case KOS_RES_RETURN: {
      /* Generator completed. Closing drive: awaiting aclose yields
         None. Normal drive: StopAsyncIteration, carrying the return
         value only when there is one -- CPython raises it bare, and a
         (None,) payload is a visible parity difference. */
      agen->closed = 1;
      PyObject *payload = res.val;
      if (asend->closing || ! payload || payload == Py_None) {
        Py_XDECREF(payload);
        if (asend->closing)
          PyErr_SetNone(PyExc_StopIteration);
        else
          PyErr_SetNone(PyExc_StopAsyncIteration);
        return NULL;
      }
      PyObject *exc = koskari_StopAsyncIteration_FromValue(payload);
      Py_DECREF(payload);
      if (exc) {
        PyErr_SetObject((PyObject *) Py_TYPE(exc), exc);
        Py_DECREF(exc);
      } else if (! PyErr_Occurred()) {
        PyErr_SetNone(PyExc_StopAsyncIteration);
      }
      return NULL;
    }

    case KOS_RES_RAISE:
    case KOS_RES_FATAL:
      agen->closed = 1;
      /* Closing drive: GeneratorExit escaping the body is the success
         shape -- awaiting aclose yields None. Anything else
         propagates. */
      if (asend->closing && PyErr_Occurred()
          && PyErr_ExceptionMatches(PyExc_GeneratorExit)) {
        PyErr_Clear();
        PyErr_SetNone(PyExc_StopIteration);
        return NULL;
      }
      /* PyErr already set (RAISE); for FATAL synthesize if needed */
      if (! PyErr_Occurred())
        PyErr_SetNone(PyExc_StopAsyncIteration);
      return NULL;

    case KOS_RES_YIELD: {
      if (asend->closing) {
        /* The body caught GeneratorExit and yielded */
        agen->closed = 1;
        Py_XDECREF(res.val);
        PyErr_SetString(PyExc_RuntimeError,
                        "async generator ignored GeneratorExit");
        return NULL;
      }
      /* Yielded a value - wrap in StopIteration so the __anext__ awaitable
         completes. Wrap in 1-tuple so PyErr_SetObject doesn't unpack args. */
      PyObject *args = PyTuple_Pack(1, res.val);
      Py_XDECREF(res.val);
      if (! args)
        return NULL;
      PyErr_SetObject(PyExc_StopIteration, args);
      Py_DECREF(args);
      return NULL;
    }

    default:
      /* KOS_RES_AWAIT is handled before the switch (in_progress held,
         awaiting set). */
      PyErr_SetString(PyExc_RuntimeError, "unexpected KosOpResult kind");
      return NULL;
  }
}


static PyObject *asyncgenasend_send(PyObject *self, PyObject *value) {

  KosPyAsyncGenASend *asend = (KosPyAsyncGenASend *) self;

  Py_XINCREF(value);
  Py_CLEAR(asend->send_value);
  asend->send_value = value;
  asend->is_throw = 0;
  Py_CLEAR(asend->throw_typ);
  Py_CLEAR(asend->throw_exc);

  return asyncgenasend_iternext(self);
}


static PyObject *asyncgenasend_throw(PyObject *self, PyObject *args) {

  KosPyAsyncGenASend *asend = (KosPyAsyncGenASend *) self;

  PyObject *typ = NULL, *exc = NULL;
  if (normalize_throw_args(args, "O|OO:throw", &typ, &exc) < 0)
    return NULL;

  asend->is_throw = 1;
  Py_CLEAR(asend->send_value);
  Py_CLEAR(asend->throw_typ);
  Py_CLEAR(asend->throw_exc);
  asend->throw_typ = typ;
  asend->throw_exc = exc;

  return asyncgenasend_iternext(self);
}


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
    PyObject *r = PyObject_CallMethod(it, "close", NULL);
    Py_XDECREF(r);
    PyErr_Clear();
    Py_CLEAR(KosContext_PY_AWAIT_ITER(agen->ctx));
  }

  PyObject *exc = PyObject_CallNoArgs(PyExc_GeneratorExit);
  if (! exc)
    return -1;

  KosOpResult res = koskari_eval(agen->ctx, NULL, PyExc_GeneratorExit, exc);
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


static PyObject *asyncgenasend_close(PyObject *self) {

  KosPyAsyncGenASend *asend = (KosPyAsyncGenASend *) self;
  KosPyAsyncGen *agen = asend->agen;

  /* Close cascades are terminal (loop shutdown, task destruction, GC
     of an abandoned chain), so closing a step's awaitable closes the
     generator too -- CPython 3.13+ semantics, applied uniformly across
     supported versions. The stepper is spent either way; a generator
     another stepper is actively driving is left alone. An asend
     abandoned mid-await owns the generator's in_progress mark; release
     it, mirroring dealloc, and the teardown demand-drives the close
     through the live chain (outcomes swallowed). */
  if (agen) {
    if (asend->awaiting) {
      agen->in_progress = 0;
      asend->awaiting = 0;
    }
    if (! agen->in_progress && ! agen->closed) {
      KosPyAsyncGen_Teardown(agen);
      agen->closed = 1;
    }
  }

  Py_CLEAR(asend->agen);
  Py_CLEAR(asend->send_value);
  Py_CLEAR(asend->throw_typ);
  Py_CLEAR(asend->throw_exc);

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


/**
   Build a stepper primed to send or throw on its first drive.
   throw_typ / throw_exc are optional borrowed references; when given,
   is_throw is set and the first drive delivers them through the
   evaluator's throw ingress. closing selects the aclose result mapping
   at terminal outcomes.
*/
static PyObject *asyncgenasend_new_ex(
    KosPyAsyncGen *agen,
    PyObject *send_value,
    PyObject *throw_typ,
    PyObject *throw_exc,
    int closing) {

  KosPyAsyncGenASend *asend = PyObject_GC_New(KosPyAsyncGenASend,
                                              &KosPyAsyncGenASendType);
  if (! asend)
    return NULL;

  Py_INCREF(agen);
  asend->agen = agen;

  Py_XINCREF(send_value);
  asend->send_value = send_value;

  asend->is_throw = (throw_typ != NULL);
  asend->closing = closing;
  Py_XINCREF(throw_typ);
  asend->throw_typ = throw_typ;
  Py_XINCREF(throw_exc);
  asend->throw_exc = throw_exc;

  asend->awaiting = 0;

  PyObject_GC_Track((PyObject *) asend);
  return (PyObject *) asend;
}


PyObject *KosPyAsyncGenASend_New(
    KosPyAsyncGen *agen,
    PyObject *send_value) {

  return asyncgenasend_new_ex(agen, send_value, NULL, NULL, 0);
}


/* === Unit lifecycle === */


int koskari_iter_init(PyObject *mod) {

  PyObject *dict = PyModule_GetDict(mod);
  if (! dict)
    return -1;

  if (PyType_Ready(KOS_EVALUATED_PYTYPE(KosPyAsyncGenType)) ||
      PyType_Ready(&KosPyAsyncGenASendType))
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
