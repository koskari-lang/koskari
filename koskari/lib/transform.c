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
   koskari/lib/transform.c

   The shared body of the transform/ providers. See transform.h.

   :author: Christopher O'Brien <obriencj@gmail.com>
   :license: GNU General Public License v3
*/


#include "lib.h"


/* The short kind name ("map", "filter") for messages: the class
   __name__, which is the last segment of the type's dotted name. */
static const char *transform_kind(PyTypeObject *type) {
  const char *name = type->tp_name;
  const char *dot = strrchr(name, '.');

  return dot ? dot + 1 : name;
}


/* === construction === */


PyObject *kos_transform_new(
    PyTypeObject *type,
    PyObject *args,
    PyObject *kwds,
    const KosTransformSpec *spec) {

  const char *kind = transform_kind(type);
  PyObject *func = NULL;
  PyObject *iterable = NULL;

  if (kwds && PyDict_Size(kwds)) {
    PyErr_Format(PyExc_TypeError, "%s takes no keyword arguments", kind);
    return NULL;
  }

  if (spec->has_callable) {
    int bare = (spec->none_means == KOS_TF_NONE_BARE);

    if (! PyArg_UnpackTuple(args, kind, 2, 2, &func, &iterable))
      return NULL;

    if (! PyCallable_Check(func) && ! (bare && func == Py_None)) {
      PyErr_Format(PyExc_TypeError, "%s func must be callable%s", kind,
                   bare ? " or None" : "");
      return NULL;
    }

  } else if (! PyArg_UnpackTuple(args, kind, 1, 1, &iterable)) {
    return NULL;
  }

  KosTransform *t = (KosTransform *) type->tp_alloc(type, 0);
  if (! t)
    return NULL;

  if (kos_transform_init_one(t, func, iterable)) {
    Py_DECREF(t);
    return NULL;
  }

  return (PyObject *) t;
}


int kos_transform_init_one(
    KosTransform *t,
    PyObject *func,
    PyObject *iterable) {

  PyObject *source = kos_iter_acquire(iterable);

  if (! source)
    return -1;

  t->source = source;
  t->sources = &t->source;
  t->nsrc = 1;
  t->idx = 0;

  /* Resolve the callable's shape once; it is fixed for the
     transform's lifetime. The source's protocol is resolved per
     acquisition, which for a one-source provider is the same thing. */
  if (func && func != Py_None) {
    KosCallShape_Init(&t->fn, func);
  } else {
    t->fn.cs_callable = NULL;
    t->fn.cs_kind = KOS_CALL_SHAPE_PLAIN;
  }
  t->src_sync = KosIter_CheckSync(source);

  t->pending = NULL;
  t->cached_frame = NULL;
  t->ctx = NULL;
  t->closed = 0;
  t->busy = 0;
  return 0;
}


/* === the step's shared pieces === */


int kos_transform_step_ready(
    KosTransform *t,
    PyObject *send_val,
    PyObject **out,
    KosIterStepResult *result) {

  if (t->closed) {
    *out = NULL;
    *result = KOS_ITER_EXHAUSTED;
    return 0;
  }

  if (t->busy) {
    PyErr_Format(PyExc_RuntimeError, "%s is already being iterated",
                 transform_kind(Py_TYPE(t)));
    *result = KOS_ITER_ERROR;
    return 0;
  }

  if (send_val && send_val != Py_None) {
    PyErr_Format(PyExc_TypeError,
                 "can't send a value into a %s; it takes none",
                 transform_kind(Py_TYPE(t)));
    *result = KOS_ITER_ERROR;
    return 0;
  }

  return 1;
}


KosIterStepResult kos_transform_step_abort(
    KosEvalState *es,
    KosTransform *t,
    KosDynNode *pre_top) {

  Py_CLEAR(t->pending);

  if (KosEvalState_DYN_TOP(es) != pre_top) {
    PyObject *ftyp, *fval, *ftb;

    PyErr_Fetch(&ftyp, &fval, &ftb);
    {
      /* a leave in flight here is abandoned with the step */
      KosOpResult wr = kos_dyn_unwind_to_node(es, pre_top,
                                              KOS_XFER_EVENT_RAISE, NULL);
      if (wr.kind == KOS_RES_AWAIT)
        Py_XDECREF(wr.val);
    }
    PyErr_Restore(ftyp, fval, ftb);
  }
  return KOS_ITER_ERROR;
}


KosIterStepResult kos_transform_stage(
    KosEvalState *es,
    KosTransform *t,
    PyObject *value,
    const KosResumeKind *resume,
    KosDynNode *pre_top) {

  KosDynFrame *frame = KosResumeFrame_Install(
      es, resume, t, &t->cached_frame);

  if (! frame) {
    Py_XDECREF(value);
    return kos_transform_step_abort(es, t, pre_top);
  }
  /* the frame carries the kind and the record from here; an abort
     below tears it down through the kind's clear */
  t->busy = 1;
  if (value) {
    if (KosEvalState_StackPushRef(es, value) < 0) {
      Py_DECREF(value);
      return kos_transform_step_abort(es, t, pre_top);
    }
    Py_DECREF(value);
  }
  return KOS_ITER_INSTALLED_VALUE;
}


/* === the rest of the iter suite === */


/**
   Close every acquired iterator the view names. A NULL slot -- one a
   lazy provider has not reached, or one it cleared as it finished --
   is a no-op in kos_iter_close, so a partly populated view needs no
   special case. A retracted view (sources NULL) closes nothing.
*/
static void close_sources(KosTransform *t) {

  uint16_t i;

  if (! t->sources)
    return;

  for (i = 0; i < t->nsrc; i++)
    kos_iter_close(t->sources[i]);
}


KosIterStepResult kos_transform_throw(
    KosEvalState *es,
    PyObject *self,
    PyObject *throw_typ,
    PyObject *throw_exc,
    PyObject **out) {

  KosTransform *t = (KosTransform *) self;

  (void) es;
  (void) out;

  if (t->busy) {
    /* A step is in flight and its driver owns the transform's state:
       every second caller is refused, GeneratorExit included -- the
       same rule CPython applies to a running async generator. */
    PyErr_Format(PyExc_RuntimeError, "%s is already being iterated",
                 transform_kind(Py_TYPE(self)));
    return KOS_ITER_ERROR;
  }

  if (PyErr_GivenExceptionMatches(throw_typ, PyExc_GeneratorExit)) {
    /* Generator close semantics: run the source's cleanup, mark
       closed, and let GeneratorExit propagate as the success shape. */
    t->closed = 1;
    t->busy = 0;
    close_sources(t);
  }

  PyErr_SetObject(throw_typ, throw_exc);
  return KOS_ITER_ERROR;
}


int kos_transform_close(PyObject *self) {

  KosTransform *t = (KosTransform *) self;

  if (t->closed)
    return 0;

  /* Declined while a step is in flight; that drive's completion owns
     the state. Reached from teardown paths, so no error is raised. */
  if (t->busy)
    return 0;

  t->closed = 1;
  t->busy = 0;
  close_sources(t);

  return 0;
}


void kos_transform_finish(PyObject *self, int exhausted) {

  KosTransform *t = (KosTransform *) self;

  t->busy = 0;
  Py_CLEAR(t->pending);
  if (exhausted)
    t->closed = 1;
}


void kos_transform_step_clear(void *state) {

  KosTransform *t = (KosTransform *) state;

  t->busy = 0;
  Py_CLEAR(t->pending);
}


/**
   The transform's durable drive context, created lazily: python-adapter
   drives host their work here across steps instead of allocating a
   context per element. The suite marks it recyclable, so the adapters
   reset it at their completion points; a creation failure falls back
   to an adapter-owned host.
*/
KosContext *kos_transform_pyctx(PyObject *self) {

  KosTransform *t = (KosTransform *) self;

  if (! t->ctx) {
    t->ctx = KosContext_New(NULL);
    if (! t->ctx)
      PyErr_Clear();
  }
  return t->ctx;
}


/* === GC + dealloc === */


int kos_transform_traverse(PyObject *self, visitproc visit, void *arg) {

  KosTransform *t = (KosTransform *) self;

  Py_VISIT(t->fn.cs_callable);
  Py_VISIT(t->source);
  Py_VISIT(t->pending);
  Py_VISIT(t->ctx);
  return 0;
}


int kos_transform_clear(PyObject *self) {

  KosTransform *t = (KosTransform *) self;

  KosCallShape_Clear(&t->fn);
  Py_CLEAR(t->source);
  Py_CLEAR(t->pending);
  /* a stepper mid-step on this context holds its own reference and
     finishes its drive before letting go; the last holder frees */
  Py_CLEAR(t->ctx);
  /* a cleared transform has nothing left to step; closed makes any
     later step report exhaustion instead of reading the freed fields */
  t->closed = 1;
  return 0;
}


void kos_transform_dealloc(PyObject *self) {

  KosTransform *t = (KosTransform *) self;

  PyObject_GC_UnTrack(self);
  /* the context teardown runs first: a ReturnPark-armed frame still
     on the chain parks into cached_frame, and the free below reclaims
     it either way */
  kos_transform_clear(self);
  if (t->cached_frame) {
    kos_dynframe_free(t->cached_frame);
    t->cached_frame = NULL;
  }
  Py_TYPE(self)->tp_free(self);
}


PyObject *kos_transform_repr(PyObject *self) {

  return PyUnicode_FromFormat("<koskari %s object at %p>",
                              transform_kind(Py_TYPE(self)), self);
}


/* === Python surface === */


static PyObject *transform_aiter(PyObject *self) {

  Py_INCREF(self);
  return self;
}


PyMethodDef kos_transform_methods[] = {
  { "asend", (PyCFunction) KosIterAdapt_Asend, METH_O,
    "Send a value into the step; returns an awaitable" },

  { "athrow", (PyCFunction) KosIterAdapt_Athrow, METH_VARARGS,
    "Throw an exception in; returns an awaitable" },

  { "aclose", (PyCFunction) KosIterAdapt_Aclose, METH_NOARGS,
    "Close; returns an awaitable" },

  { NULL, NULL, 0, NULL },
};


PyAsyncMethods kos_transform_async_methods = {
  .am_aiter = transform_aiter,
  .am_anext = KosIterAdapt_Anext,
};


/* === The providers ===

   Each supplies only its ki_step, the resume chain that step arms,
   and its type object. They are fragments of this unit, so the shared
   body above is in scope for all of them. */


#include "transform/map.c"
#include "transform/filter.c"
#include "transform/zip.c"
#include "transform/zip_longest.c"
#include "transform/chain.c"
#include "transform/compress.c"
#include "transform/cycle.c"
#include "transform/accumulate.c"
#include "transform/batched.c"
#include "transform/dropwhile.c"
#include "transform/takewhile.c"
#include "transform/starmap.c"
#include "transform/islice.c"
#include "transform/pairwise.c"


/* === Unit lifecycle === */


/**
   Every provider type, with the name its class carries and the short
   alias matching that class __name__.
*/
static const struct {
  KosEvaluatedTypeObject *type;
  const char *cls;
  const char *alias;
} transform_types[] = {
  { &KosMapType,         "KosMap",         "map" },
  { &KosFilterType,      "KosFilter",      "filter" },
  { &KosFilterFalseType, "KosFilterFalse", "filterfalse" },
  { &KosZipType,         "KosZip",         "zip" },
  { &KosZipLongestType,  "KosZipLongest",  "zip-longest" },
  { &KosChainType,       "KosChain",       "chain" },
  { &KosCompressType,    "KosCompress",    "compress" },
  { &KosCycleType,       "KosCycle",       "cycle" },
  { &KosAccumulateType,  "KosAccumulate",  "accumulate" },
  { &KosBatchedType,     "KosBatched",     "batched" },
  { &KosDropWhileType,   "KosDropWhile",   "dropwhile" },
  { &KosTakeWhileType,   "KosTakeWhile",   "takewhile" },
  { &KosStarMapType,     "KosStarMap",     "starmap" },
  { &KosISliceType,      "KosISlice",      "islice" },
  { &KosPairwiseType,    "KosPairwise",    "pairwise" },
};


/**
   Bind a provider that is a plain function rather than a type.
   chain-from-iterable is the one: a chain that takes its series from
   an iterable.
*/
static int transform_bind_function(PyObject *dict, PyMethodDef *def) {

  PyObject *fn = PyCFunction_New(def, NULL);
  int failed;

  if (! fn)
    return -1;

  failed = PyDict_SetItemString(dict, def->ml_name, fn);
  Py_DECREF(fn);

  return failed;
}


int koskari_transform_init(PyObject *mod) {

  PyObject *dict = PyModule_GetDict(mod);
  size_t i;

  if (! dict)
    return -1;

  for (i = 0; i < sizeof(transform_types) / sizeof(*transform_types); i++) {
    PyTypeObject *type = KOS_EVALUATED_PYTYPE(*transform_types[i].type);

    if (PyType_Ready(type))
      return -1;
    if (PyDict_SetItemString(dict, transform_types[i].cls, (PyObject *) type))
      return -1;
    if (PyDict_SetItemString(dict, transform_types[i].alias,
                             (PyObject *) type))
      return -1;
  }

  if (transform_bind_function(dict, &koschain_from_iterable_def))
    return -1;

  return 0;
}


int koskari_transform_clear(PyObject *mod) {

  (void) mod;
  return 0;
}


// The end.
