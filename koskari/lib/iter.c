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
   Dissociate a frozen async generator from its context without running
   the extent protocol: detach and free the frozen chain, release the
   gen. Leaves ctx ready to be released.

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

  if (agen->gen && KosDynGen_ACTIVE((KosDynGen *) agen->gen)
      && ! agen->started) {
    /* never driven: nothing entered, nothing to unwind */
    asyncgen_dissociate_chain(agen);

  } else if (agen->gen && KosDynGen_ACTIVE((KosDynGen *) agen->gen)) {
    /* suspended: the GeneratorExit drive needs a context; one spliced
       only from Koskari has none, so it gets one here, and falls back
       to the pure cleanup when even that cannot be had */
    KosContext *ctx = agen->ctx;
    if (! ctx) {
      PyObject *fence_typ, *fence_val, *fence_tb;
      PyErr_Fetch(&fence_typ, &fence_val, &fence_tb);
      ctx = KosContext_New(NULL);
      if (! ctx)
        PyErr_Clear();
      PyErr_Restore(fence_typ, fence_val, fence_tb);
    }
    if (ctx) {
      asyncgen_teardown_chain_on_ctx(agen, ctx);
      if (ctx != agen->ctx)
        Py_DECREF(ctx);
    } else {
      asyncgen_dissociate_chain(agen);
    }

  } else if (agen->gen) {
    kos_dyngen_release((KosDynGen *) agen->gen);
    agen->gen = NULL;
  }

  Py_CLEAR(agen->ctx);
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

  if (agen->gen && KosDynGen_ACTIVE((KosDynGen *) agen->gen)) {
    res = kos_dynnode_traverse((KosDynNode *) agen->gen, visit, arg);
    if (res)
      return res;
  }

  Py_VISIT(agen->ctx);

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




/* === KosIter suite === */


/**
   Graft the generator onto the drive context. Frozen gens install from
   GEN.ACTIVE; bootstrap gens move the chain from agen->ctx. When es IS
   agen->ctx (the Python-adapter drive), a live un-frozen chain is
   already in the thawed es -- nothing to install.
*/
static int agen_attach_gen_flatten(
    KosEvalState *es,
    KosPyAsyncGen *agen,
    KosDynGen *gen) {

  if (KosDynGen_ACTIVE(gen)) {
    if (agen->ctx) {
      KosContext_DYN_TOP(agen->ctx) = NULL;
      KosContext_FRAME_TOP(agen->ctx) = NULL;
    }
    if (! agen->started) {
      /* never driven: the frame rests in the slot; the first install
         enters fresh code rather than reviving a yield */
      KosDynNode *head = (KosDynNode *) KosDynGen_ACTIVE(gen);
      KosDynGen_ACTIVE(gen) = NULL;
      return kos_dyn_gen_install_bootstrap(es, gen, head);
    }
    return kos_dyn_gen_install(es, gen);
  }

  if (agen->ctx && KosEvalState_CONTEXT(es) == agen->ctx) {
    /* Own-ctx drive: the bootstrap (or mid-await) chain thawed into es
       with the context. */
    if (KosEvalState_DYN_TOP(es))
      return 0;

  } else if (agen->ctx && KosContext_DYN_TOP(agen->ctx)) {
    KosDynNode *head = KosContext_DYN_TOP(agen->ctx);
    KosContext_DYN_TOP(agen->ctx) = NULL;
    KosContext_FRAME_TOP(agen->ctx) = NULL;
    return kos_dyn_gen_install_bootstrap(es, gen, head);
  }

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

  if (agen->closed || ! agen->gen) {
    *out = NULL;
    return KOS_ITER_EXHAUSTED;
  }

  if (send_val && send_val != Py_None && ! agen->started) {
    PyErr_SetString(PyExc_TypeError,
                    "can't send non-None value to a just-started "
                    "async generator");
    return KOS_ITER_ERROR;
  }

  {
    KosDynGen *gen = (KosDynGen *) agen->gen;
    /* the slot holds either the unstarted frame or a suspended chain;
       only the latter is a frozen yield to revive */
    int resting = (KosDynGen_ACTIVE(gen) != NULL);
    int frozen = resting && agen->started;
    int own_ctx = agen->ctx && (KosEvalState_CONTEXT(es) == agen->ctx);

    if (! resting && ! (agen->ctx && KosContext_DYN_TOP(agen->ctx))
        && ! (own_ctx && KosEvalState_DYN_TOP(es))) {
      /* No frozen chain and nothing live on the context: spent. */
      *out = NULL;
      return KOS_ITER_EXHAUSTED;
    }

    agen->in_progress = 1;
    if (agen_attach_gen_flatten(es, agen, gen) < 0)
      goto drive_error;

    agen->started = 1;

    /* Resume-of-frozen-yield only: the frozen install pushed the
       Py_None placeholder for the suspended yield expression; replace
       it with the sent value. A bootstrap install enters fresh code
       with an empty stack -- nothing to swap. */
    if (frozen && send_val && ! PyErr_Occurred()) {
      KosDynFrame *frame = KosEvalState_FRAME(es);
      if (frame && KosDynFrame_RESUME_KIND(frame)) {
        /* waiting on a resume: the sent value is its delivery */
        Py_INCREF(send_val);
        KosEvalState_StageDelivery(es, send_val);
      } else {
        PyObject *popped = KosEvalState_StackPopRef(es);
        if (! popped)
          goto drive_error;
        Py_DECREF(popped);
        if (KosEvalState_StackPushRef(es, send_val) < 0)
          goto drive_error;
      }
    }
  }

  return KOS_ITER_INSTALLED;

drive_error:
  /* Every failed drive releases the busy mark; a wedged in_progress
     would refuse all further iteration of a still-live generator. */
  agen->in_progress = 0;
  return KOS_ITER_ERROR;
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

  if (agen->closed || ! agen->gen) {
    *out = NULL;
    return KOS_ITER_EXHAUSTED;
  }

  /* Throw ingress: prime the exception, then attach; the pending error
     is delivered at the resume point inside the flattened drive. */
  PyErr_SetObject(throw_typ, throw_exc);
  return agen_iter_drive(es, agen, NULL, out);
}


static int agen_iter_close(PyObject *self) {

  KosPyAsyncGen *agen = (KosPyAsyncGen *) self;

  /* Declined while another site is driving; that drive's completion
     owns the state. */
  if (agen->in_progress)
    return 0;

  KosPyAsyncGen_Teardown(agen);
  agen->closed = 1;
  return 0;
}


static void agen_iter_finish(PyObject *self, int exhausted) {

  KosPyAsyncGen *agen = (KosPyAsyncGen *) self;

  agen->in_progress = 0;
  if (exhausted)
    agen->closed = 1;
}


/**
   The wrapper's context, for a drive from Python: created on the first
   such drive with the domain and parameterize cells in force then, and
   kept for the generator's life. A generator only ever spliced from
   Koskari never has one. NULL on a creation failure, error cleared, so
   the stepper falls back to a context of its own.
*/
static KosContext *agen_iter_pyctx(PyObject *self) {

  KosPyAsyncGen *agen = (KosPyAsyncGen *) self;

  if (! agen->ctx) {
    agen->ctx = KosContext_New(NULL);
    if (! agen->ctx)
      PyErr_Clear();
  }
  return agen->ctx;
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
  },
};


PyObject *KosPyAsyncGen_New(KosContext *ctx) {

  KosPyAsyncGen *agen = PyObject_GC_New(KosPyAsyncGen, KOS_EVALUATED_PYTYPE(KosPyAsyncGenType));
  if (! agen) {
    Py_XDECREF(ctx);
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
