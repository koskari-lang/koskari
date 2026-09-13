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
   koskari/lib/transform/zip.c

   KosZip: a native lazy zip over N sources, duck-typed against
   builtins.zip, implemented as a KosIter provider on the shared
   transform body (transform.c). The first multi-source provider: the
   acquired iterators live in a tuple this unit owns, published to the
   base through the sources view so the base closes them without
   knowing where they are kept.

   The fast path is pure C: when every source steps through
   tp_iternext the whole row is built in one loop with the evaluator
   untouched. When any source is driven, the row walk runs under one
   resume-carrying frame and is flattened -- a single loop over the
   sources, requesting a resume only for a source that actually
   suspends, and re-entering that same loop at base.idx afterwards.
   There is no provider per source, no frame per source, and no
   bounce through the trampoline per element. The partial row lives in
   base.pending, which ki_finish and the step abort already clear, so
   a suspension mid-row and an exhaustion mid-row both unwind with no
   code of this unit's own.

   Sources are acquired eagerly at construction: acquisition never
   suspends for any of the three input shapes, every source is reached
   on the first row, and a bad argument then raises where it is
   written rather than inside whatever drives the zip.

   :author: Christopher O'Brien <obriencj@gmail.com>
   :license: GNU General Public License v3
*/


#include "lib.h"


typedef struct KosZip {
  KosTransform base;
  PyObject *srcs;      /* tuple of acquired iterators; the storage the
                          base's sources view points into */
  uint32_t sync_mask;  /* bit i: source i steps through tp_iternext
                          alone. A cache, not a limit -- sources past
                          32 are checked live rather than capping the
                          arity */
  uint8_t all_sync;    /* no source needs the evaluator */
} KosZip;


/**
   Does source i step through tp_iternext alone? Cached in the mask
   for the first 32, resolved live beyond it.
*/
static inline int zip_sync(KosZip *z, uint16_t i) {

  if (i < 32)
    return (int) ((z->sync_mask >> i) & 1u);

  return KosIter_CheckSync(z->base.sources[i]);
}


/* === the step's CPS resumes ===

   No frame-carrying capture: replaying a captured step would
   re-consume source elements, so continuation capture across a live
   zip-step frame refuses by name (see the kos_transform_stage call
   site below and kos_dynframe_clone's resume refusal).

   The row walk is carried by base.idx rather than by a resume per
   source: FETCH runs the loop and only leaves it for a source that
   suspends, and DELIVER files that source's element and hops straight
   back into the loop. */


static KOS_NOINLINE KOS_PRESERVE_NONE KosOpResult zip_resume_deliver(
    KosEvalState *es, void *state, PyObject *element);


/**
   FETCH: walk the sources from base.idx, filing each element into the
   row. A sync source completes inside the loop; a driven one requests
   the next element and leaves, to be re-entered here by
   zip_resume_deliver. A completed row parks as the step's value.
   Always entered with a NULL value.
*/
static KOS_NOINLINE KOS_PRESERVE_NONE KosOpResult zip_resume_fetch(
    KosEvalState *es, void *state, PyObject *value) {

  KosZip *z = (KosZip *) state;
  KosTransform *t = &z->base;

  (void) value;

  if (PyErr_Occurred())
    return EVAL_RAISE();

  while (t->idx < t->nsrc) {
    PyObject *element;

    if (! zip_sync(z, t->idx)) {
      KosOpResult r = KosEvalState_NextResume(es, t->sources[t->idx],
                                              NULL, zip_resume_deliver, z);
      RESULT_ENTRY(es, r, zip_resume_deliver, z);
    }

    element = PyIter_Next(t->sources[t->idx]);
    if (! element) {
      /* A source is spent, so the zip is: let StopIteration reach the
         consumer's iteration node, which owns the conversion. The
         partial row is cleared by ki_finish. */
      if (! PyErr_Occurred())
        PyErr_SetNone(PyExc_StopIteration);
      return EVAL_RAISE();
    }

    PyTuple_SET_ITEM(t->pending, (Py_ssize_t) t->idx, element);
    t->idx++;
  }

  {
    PyObject *row = t->pending;

    t->pending = NULL;
    return KosNativeFrame_ReturnPark(es, row, &t->cached_frame);
  }
}


/**
   DELIVER: a driven source produced its element. File it at the
   position the walk stopped on and hop back into the walk. A tail
   call, so a row of driven sources stays flat. element is consumed.
*/
static KOS_NOINLINE KOS_PRESERVE_NONE KosOpResult zip_resume_deliver(
    KosEvalState *es, void *state, PyObject *element) {

  KosZip *z = (KosZip *) state;
  KosTransform *t = &z->base;

  if (! element)
    return EVAL_RAISE();

  PyTuple_SET_ITEM(t->pending, (Py_ssize_t) t->idx, element);
  t->idx++;

  RESUME_HOP(zip_resume_fetch, es, z, NULL);
}


/* === the step === */


static KosIterStepResult zip_iter_step(
    KosEvalState *es,
    PyObject *self,
    PyObject *send_val,
    PyObject **out) {

  KosZip *z = (KosZip *) self;
  KosTransform *t = &z->base;
  KosDynNode *pre_top = KosEvalState_DYN_TOP(es);
  KosIterStepResult early;
  PyObject *row;

  if (! kos_transform_step_ready(t, send_val, out, &early))
    return early;

  /* zip over no sources is empty, as builtins.zip() is. */
  if (! t->nsrc) {
    t->closed = 1;
    *out = NULL;
    return KOS_ITER_EXHAUSTED;
  }

  row = PyTuple_New((Py_ssize_t) t->nsrc);
  if (! row)
    return KOS_ITER_ERROR;

  if (z->all_sync) {
    uint16_t i;

    for (i = 0; i < t->nsrc; i++) {
      PyObject *element = PyIter_Next(t->sources[i]);

      if (! element) {
        Py_DECREF(row);
        if (PyErr_Occurred())
          return KOS_ITER_ERROR;
        t->closed = 1;
        *out = NULL;
        return KOS_ITER_EXHAUSTED;
      }
      PyTuple_SET_ITEM(row, (Py_ssize_t) i, element);
    }

    *out = row;
    return KOS_ITER_VALUE;
  }

  /* At least one driven source: defer the whole walk. Nothing is
     fetched yet to stage, and a request that can suspend must run
     from inside koskari_eval's own dispatch rather than from here
     (see stepper_step). */
  {
    KosIterStepResult staged = kos_transform_stage(
        es, t, NULL, (void *) zip_resume_fetch, "zip-step", pre_top);

    if (staged != KOS_ITER_INSTALLED_VALUE) {
      Py_DECREF(row);
      return staged;
    }

    t->idx = 0;
    t->pending = row;  /* steals */
    return staged;
  }
}


/* === the type === */


static PyObject *koszip_new(
    PyTypeObject *type,
    PyObject *args,
    PyObject *kwds) {

  Py_ssize_t n = PyTuple_GET_SIZE(args);
  PyObject *srcs;
  KosZip *z;
  Py_ssize_t i;

  if (kwds && PyDict_Size(kwds)) {
    PyErr_SetString(PyExc_TypeError, "zip takes no keyword arguments");
    return NULL;
  }

  if (n > UINT16_MAX) {
    PyErr_SetString(PyExc_TypeError, "zip takes at most 65535 iterables");
    return NULL;
  }

  srcs = PyTuple_New(n);
  if (! srcs)
    return NULL;

  for (i = 0; i < n; i++) {
    PyObject *source = kos_iter_acquire(PyTuple_GET_ITEM(args, i));

    if (! source) {
      Py_DECREF(srcs);
      return NULL;
    }
    PyTuple_SET_ITEM(srcs, i, source);
  }

  z = (KosZip *) type->tp_alloc(type, 0);
  if (! z) {
    Py_DECREF(srcs);
    return NULL;
  }

  z->srcs = srcs;
  z->sync_mask = 0;
  z->all_sync = 1;

  for (i = 0; i < n; i++) {
    if (KosIter_CheckSync(PyTuple_GET_ITEM(srcs, i))) {
      if (i < 32)
        z->sync_mask |= (uint32_t) 1u << i;
    } else {
      z->all_sync = 0;
    }
  }

  z->base.fn.cs_callable = NULL;
  z->base.fn.cs_slots = NULL;
  z->base.fn.cs_kind = KOS_CALL_SHAPE_PLAIN;

  z->base.source = NULL;
  z->base.sources = n ? PySequence_Fast_ITEMS(srcs) : NULL;
  z->base.nsrc = (uint16_t) n;
  z->base.idx = 0;

  z->base.pending = NULL;
  z->base.cached_frame = NULL;
  z->base.ctx = NULL;
  z->base.src_sync = 0;
  z->base.closed = 0;
  z->base.busy = 0;

  return (PyObject *) z;
}


static int koszip_traverse(PyObject *self, visitproc visit, void *arg) {

  KosZip *z = (KosZip *) self;

  Py_VISIT(z->srcs);
  return kos_transform_traverse(self, visit, arg);
}


/**
   Retract the view before the storage it points into goes: tp_clear
   runs while the object is still alive, and the base reads the view
   whenever it closes.
*/
static int koszip_clear(PyObject *self) {

  KosZip *z = (KosZip *) self;

  z->base.sources = NULL;
  z->base.nsrc = 0;
  Py_CLEAR(z->srcs);

  return kos_transform_clear(self);
}


static void koszip_dealloc(PyObject *self) {

  KosZip *z = (KosZip *) self;

  PyObject_GC_UnTrack(self);
  z->base.sources = NULL;
  z->base.nsrc = 0;
  Py_CLEAR(z->srcs);

  kos_transform_dealloc(self);
}


KosEvaluatedTypeObject KosZipType = {
  .type = {
    PyVarObject_HEAD_INIT(NULL, 0)

    .tp_name = "koskari.lib.zip",
    .tp_basicsize = sizeof(KosZip),
    .tp_itemsize = 0,
    .tp_dealloc = koszip_dealloc,
    .tp_repr = kos_transform_repr,
    .tp_as_async = &kos_transform_async_methods,
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC,
    .tp_doc = "KosZip(*iterables) - native lazy zip; iterable both "
              "asynchronously (ambient await, koskari-flattened) and "
              "synchronously (demand path)",
    .tp_traverse = koszip_traverse,
    .tp_clear = koszip_clear,
    .tp_iter = KosIterAdapt_DemandIter,
    .tp_methods = kos_transform_methods,
    .tp_new = koszip_new,
    .tp_base = KOS_EVALUATED_PYTYPE(KosEvaluatedType),
  },
  .kos_iter = {
    .ki_step = zip_iter_step,
    .ki_throw = kos_transform_throw,
    .ki_close = kos_transform_close,
    .ki_finish = kos_transform_finish,
    .ki_pyctx = kos_transform_pyctx,
    .ki_pyctx_recyclable = 1,
  },
};


/* === Unit lifecycle === */


int koskari_zip_init(PyObject *mod) {

  PyObject *dict = PyModule_GetDict(mod);
  if (! dict)
    return -1;

  if (PyType_Ready(KOS_EVALUATED_PYTYPE(KosZipType)))
    return -1;

  if (PyDict_SetItemString(dict, "KosZip",
                           (PyObject *) KOS_EVALUATED_PYTYPE(KosZipType)))
    return -1;

  /* short alias matching the class __name__ */
  if (PyDict_SetItemString(dict, "zip",
                           (PyObject *) KOS_EVALUATED_PYTYPE(KosZipType)))
    return -1;

  return 0;
}


int koskari_zip_clear(PyObject *mod) {

  (void) mod;
  return 0;
}


// The end.
