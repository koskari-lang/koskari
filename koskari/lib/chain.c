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
   koskari/lib/chain.c

   KosChain: a native lazy chain over a series of iterables,
   duck-typed against itertools.chain, implemented as a KosIter
   provider on the shared transform body (transform.c). This unit
   holds only the chain step and the resume chain it arms.

   One source is live at a time, so the base's single source slot is
   the whole of the view: nsrc stays 1 and close reaches whichever
   source is current. The iterables behind it are held unacquired
   until reached, since acquiring one has effects -- calling a
   generator function, opening a file in __iter__ -- that a chain
   which never gets that far must not cause. That laziness is the
   reason itertools.chain acquires as it goes, and the reason the
   koskari-written version it replaces did too.

   This is the provider the base's per-acquisition src_sync rule
   exists for: a chain can run a list into an async iterator, so the
   flag is resolved again at every acquisition rather than fixed for
   the transform's lifetime.

   Exhaustion of one source is not exhaustion of the chain, so like
   batched, zip-longest and cycle this provider catches a source
   running out rather than propagating it -- and, having more sources
   to fetch from, carries the step on by threading back into its own
   fetch.

   :author: Christopher O'Brien <obriencj@gmail.com>
   :license: GNU General Public License v3
*/


#include "lib.h"


typedef struct KosChain {
  KosTransform base;
  PyObject *pending_srcs;  /* tuple of iterables not yet acquired */
  Py_ssize_t next;         /* index of the next one to acquire */
} KosChain;


/**
   Is the pending failure just a source running out?
*/
static int chain_spent(void) {

  return PyErr_Occurred() && PyErr_ExceptionMatches(PyExc_StopIteration);
}


/**
   Move to the next iterable in the series: release the spent source
   and acquire its successor, re-resolving whether the new one steps
   through tp_iternext alone.

   :return: 1 when a new source is live, 0 when the series is spent,
            -1 with PyErr set when acquiring failed
*/
static int chain_advance(KosChain *c) {

  KosTransform *t = &c->base;
  PyObject *source;

  Py_CLEAR(t->source);

  if (c->next >= PyTuple_GET_SIZE(c->pending_srcs))
    return 0;

  source = kos_iter_acquire(PyTuple_GET_ITEM(c->pending_srcs, c->next));
  if (! source)
    return -1;

  c->next++;
  t->source = source;
  t->src_sync = KosIter_CheckSync(source);

  return 1;
}


/* === the step's CPS resumes ===

   No frame-carrying capture: replaying a captured step would
   re-consume source elements, so continuation capture across a live
   chain-step frame refuses by name (see the kos_transform_stage call
   site below and kos_dynframe_clone's resume refusal). */


static KOS_NOINLINE KOS_PRESERVE_NONE KosOpResult chain_resume_deliver(
    KosEvalState *es, void *state, PyObject *element);


/**
   FETCH: take from the live source, advancing through the series as
   each one runs out. A sync source completes inside the loop; a
   driven one requests its element and leaves, to be re-entered here
   by chain_resume_deliver. Always entered with a NULL value.
*/
static KOS_NOINLINE KOS_PRESERVE_NONE KosOpResult chain_resume_fetch(
    KosEvalState *es, void *state, PyObject *value) {

  KosChain *c = (KosChain *) state;
  KosTransform *t = &c->base;

  (void) value;

  if (PyErr_Occurred())
    return EVAL_RAISE();

  for (;;) {
    PyObject *element;

    if (! t->source) {
      int moved = chain_advance(c);

      if (moved < 0)
        return EVAL_RAISE();
      if (! moved) {
        PyErr_SetNone(PyExc_StopIteration);
        return EVAL_RAISE();
      }
      continue;
    }

    if (! t->src_sync) {
      KosOpResult r = KosEvalState_NextResume(es, t->source, NULL,
                                              chain_resume_deliver, c);
      RESULT_ENTRY(es, r, chain_resume_deliver, c);
    }

    element = PyIter_Next(t->source);
    if (! element) {
      if (PyErr_Occurred())
        return EVAL_RAISE();

      /* This source is spent; the chain is not until the series is. */
      Py_CLEAR(t->source);
      continue;
    }

    return KosNativeFrame_ReturnPark(es, element, &t->cached_frame);
  }
}


/**
   DELIVER: the live driven source produced an element, or ran out.
   Running out moves to the next iterable rather than ending the
   chain, so the pending StopIteration is handled here. element is
   consumed.
*/
static KOS_NOINLINE KOS_PRESERVE_NONE KosOpResult chain_resume_deliver(
    KosEvalState *es, void *state, PyObject *element) {

  KosChain *c = (KosChain *) state;
  KosTransform *t = &c->base;

  if (! element) {
    if (! chain_spent())
      return EVAL_RAISE();

    PyErr_Clear();
    Py_CLEAR(t->source);

    RESUME_HOP(chain_resume_fetch, es, c, NULL);
  }

  return KosNativeFrame_ReturnPark(es, element, &t->cached_frame);
}


/* === the step === */


static KosIterStepResult chain_iter_step(
    KosEvalState *es,
    PyObject *self,
    PyObject *send_val,
    PyObject **out) {

  KosChain *c = (KosChain *) self;
  KosTransform *t = &c->base;
  KosDynNode *pre_top = KosEvalState_DYN_TOP(es);
  KosIterStepResult early;

  if (! kos_transform_step_ready(t, send_val, out, &early))
    return early;

  /* Walk the series in C for as long as the sources are sync, and
     defer only once a driven one is reached. */
  for (;;) {
    PyObject *element;

    if (! t->source) {
      int moved = chain_advance(c);

      if (moved < 0)
        return KOS_ITER_ERROR;
      if (! moved) {
        t->closed = 1;
        *out = NULL;
        return KOS_ITER_EXHAUSTED;
      }
      continue;
    }

    if (! t->src_sync)
      return kos_transform_stage(es, t, NULL, (void *) chain_resume_fetch,
                                 "chain-step", pre_top);

    element = PyIter_Next(t->source);
    if (! element) {
      if (PyErr_Occurred())
        return KOS_ITER_ERROR;
      Py_CLEAR(t->source);
      continue;
    }

    *out = element;
    return KOS_ITER_VALUE;
  }
}


/* === the type === */


static PyObject *koschain_new(
    PyTypeObject *type,
    PyObject *args,
    PyObject *kwds) {

  KosChain *c;

  if (kwds && PyDict_Size(kwds)) {
    PyErr_SetString(PyExc_TypeError, "chain takes no keyword arguments");
    return NULL;
  }

  c = (KosChain *) type->tp_alloc(type, 0);
  if (! c)
    return NULL;

  Py_INCREF(args);
  c->pending_srcs = args;
  c->next = 0;

  c->base.fn.cs_callable = NULL;
  c->base.fn.cs_slots = NULL;
  c->base.fn.cs_kind = KOS_CALL_SHAPE_PLAIN;

  /* One source is live at a time, so the arity-one view serves: the
     step fills the slot as it advances and close reaches whichever
     source is current. A NULL slot is nothing to close. */
  c->base.source = NULL;
  c->base.sources = &c->base.source;
  c->base.nsrc = 1;
  c->base.idx = 0;

  c->base.pending = NULL;
  c->base.cached_frame = NULL;
  c->base.ctx = NULL;
  c->base.src_sync = 0;
  c->base.closed = 0;
  c->base.busy = 0;

  return (PyObject *) c;
}


/**
   chain.from-iterable(iterables): the series comes from one iterable
   rather than from the argument list. It is read eagerly into a
   tuple, which is what holding the unacquired series requires; the
   iterables in it are still acquired only as they are reached.
*/
static PyObject *koschain_from_iterable(PyObject *self, PyObject *arg) {

  PyObject *series = PySequence_Tuple(arg);
  PyObject *made;

  (void) self;

  if (! series)
    return NULL;

  made = koschain_new(KOS_EVALUATED_PYTYPE(KosChainType), series, NULL);
  Py_DECREF(series);
  return made;
}


static PyMethodDef koschain_from_iterable_def = {
  "chain-from-iterable", (PyCFunction) koschain_from_iterable, METH_O,
  "chain-from-iterable(iterables) - native lazy chain over a series "
  "taken from one iterable",
};


static int koschain_traverse(PyObject *self, visitproc visit, void *arg) {

  KosChain *c = (KosChain *) self;

  Py_VISIT(c->pending_srcs);
  return kos_transform_traverse(self, visit, arg);
}


static int koschain_clear(PyObject *self) {

  KosChain *c = (KosChain *) self;

  Py_CLEAR(c->pending_srcs);
  return kos_transform_clear(self);
}


static void koschain_dealloc(PyObject *self) {

  KosChain *c = (KosChain *) self;

  PyObject_GC_UnTrack(self);
  Py_CLEAR(c->pending_srcs);

  kos_transform_dealloc(self);
}


KosEvaluatedTypeObject KosChainType = {
  .type = {
    PyVarObject_HEAD_INIT(NULL, 0)

    .tp_name = "koskari.lib.chain",
    .tp_basicsize = sizeof(KosChain),
    .tp_itemsize = 0,
    .tp_dealloc = koschain_dealloc,
    .tp_repr = kos_transform_repr,
    .tp_as_async = &kos_transform_async_methods,
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC,
    .tp_doc = "KosChain(*iterables) - native lazy chain; each iterable "
              "is acquired only when the one before it runs out. "
              "Iterable both asynchronously (ambient await, "
              "koskari-flattened) and synchronously (demand path)",
    .tp_traverse = koschain_traverse,
    .tp_clear = koschain_clear,
    .tp_iter = KosIterAdapt_DemandIter,
    .tp_methods = kos_transform_methods,
    .tp_new = koschain_new,
    .tp_base = KOS_EVALUATED_PYTYPE(KosEvaluatedType),
  },
  .kos_iter = {
    .ki_step = chain_iter_step,
    .ki_throw = kos_transform_throw,
    .ki_close = kos_transform_close,
    .ki_finish = kos_transform_finish,
    .ki_pyctx = kos_transform_pyctx,
    .ki_pyctx_recyclable = 1,
  },
};


/* === Unit lifecycle === */


int koskari_chain_init(PyObject *mod) {

  PyObject *dict = PyModule_GetDict(mod);
  if (! dict)
    return -1;

  if (PyType_Ready(KOS_EVALUATED_PYTYPE(KosChainType)))
    return -1;

  if (PyDict_SetItemString(
          dict, "KosChain",
          (PyObject *) KOS_EVALUATED_PYTYPE(KosChainType)))
    return -1;

  /* short alias matching the class __name__ */
  if (PyDict_SetItemString(
          dict, "chain",
          (PyObject *) KOS_EVALUATED_PYTYPE(KosChainType)))
    return -1;

  {
    PyObject *fi = PyCFunction_New(&koschain_from_iterable_def, NULL);

    if (! fi)
      return -1;
    if (PyDict_SetItemString(dict, "chain-from-iterable", fi)) {
      Py_DECREF(fi);
      return -1;
    }
    Py_DECREF(fi);
  }

  return 0;
}


int koskari_chain_clear(PyObject *mod) {

  (void) mod;
  return 0;
}


// The end.
