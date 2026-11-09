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

  /* the source's kind and suite are bound once with it, so no step
     classifies it again */
  kos_iter_request_bind_next(&t->src, source);
  t->reqs = &t->src;
  t->nsrc = 1;
  t->idx = 0;

  /* Resolve the callable's shape once; it is fixed for the
     transform's lifetime. */
  if (func && func != Py_None) {
    KosCallShape_Init(&t->fn, func);
  } else {
    t->fn.cs_callable = NULL;
    t->fn.cs_kind = KOS_CALL_SHAPE_PLAIN;
  }
  t->pending = NULL;
  t->cached_frame = NULL;
  t->ctx = NULL;
  t->closed = 0;
  t->busy = KOS_TF_IDLE;
  return 0;
}


void kos_transform_init_view(
    KosTransform *t,
    KosIterRequest *reqs,
    uint16_t nsrc) {

  t->fn.cs_callable = NULL;
  t->fn.cs_kind = KOS_CALL_SHAPE_PLAIN;

  t->src.iter = NULL;
  t->src.islots = NULL;
  t->src.kind = KOS_IREQ_KIND_NONE;
  t->reqs = reqs;
  t->nsrc = nsrc;
  t->idx = 0;

  t->pending = NULL;
  t->cached_frame = NULL;
  t->ctx = NULL;
  t->closed = 0;
  t->busy = KOS_TF_IDLE;
}


int kos_transform_init_multi(KosTransformMulti *m, PyObject *iterables) {

  Py_ssize_t n = PyTuple_GET_SIZE(iterables);
  KosIterRequest *srcs = NULL;
  Py_ssize_t i;

  if (n) {
    srcs = PyMem_Calloc((size_t) n, sizeof(KosIterRequest));
    if (! srcs) {
      PyErr_NoMemory();
      return -1;
    }
  }

  /* each source bound as it is acquired; a failure releases the ones
     before it, every later slot being still clear */
  for (i = 0; i < n; i++) {
    PyObject *source = kos_iter_acquire(PyTuple_GET_ITEM(iterables, i));

    if (! source) {
      while (i--)
        Py_CLEAR(srcs[i].iter);
      PyMem_Free(srcs);
      return -1;
    }
    kos_iter_request_bind_next(&srcs[i], source);
  }

  m->srcs = srcs;
  kos_transform_init_view(&m->base, srcs, (uint16_t) n);
  return 0;
}


/* === the step's shared pieces === */


int kos_transform_source_spent(void) {

  return PyErr_Occurred() && PyErr_ExceptionMatches(PyExc_StopIteration);
}


int kos_transform_busy(PyObject *self, const char *method) {

  KosTransform *t = (KosTransform *) self;

  (void) method;
  if (! t->busy)
    return 0;

  PyErr_Format(PyExc_ValueError, "%s is already being iterated",
               transform_kind(Py_TYPE(self)));
  return 1;
}


KosIterStepResult kos_transform_step_abort(
    KosContext *ctx,
    KosTransform *t,
    KosDynNode *pre_top) {

  Py_CLEAR(t->pending);

  if (KosContext_DYN_TOP(ctx) != pre_top) {
    PyObject *ftyp, *fval, *ftb;

    PyErr_Fetch(&ftyp, &fval, &ftb);
    {
      /* a leave in flight here is abandoned with the step */
      KosOpResult wr = kos_dyn_unwind_to_node(ctx, pre_top,
                                              KOS_XFER_EVENT_RAISE, NULL);
      if (wr.kind == KOS_RES_AWAIT)
        Py_XDECREF(wr.val);
    }
    PyErr_Restore(ftyp, fval, ftb);
  }
  return KOS_ITER_ERROR;
}


KosIterStepResult kos_transform_stage(
    KosContext *ctx,
    KosTransform *t,
    PyObject *value,
    const KosResumeKind *resume,
    KosDynNode *pre_top) {

  KosDynFrame *frame = KosResumeFrame_Install(
      ctx, resume, t, &t->cached_frame);

  if (! frame) {
    Py_XDECREF(value);
    return kos_transform_step_abort(ctx, t, pre_top);
  }
  /* the frame names t from here: the stage holds t until the step's
     finish or clear, unless the frame is on t's own context */
  kos_transform_staged(ctx, t);
  /* the frame carries the kind and the record from here; an abort
     below tears it down through the kind's clear */
  if (value) {
    if (KosContext_StackPushRef(ctx, value) < 0) {
      Py_DECREF(value);
      return kos_transform_step_abort(ctx, t, pre_top);
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
   special case. A retracted view (reqs NULL) closes nothing.
*/
static void close_sources(KosTransform *t) {

  uint16_t i;

  if (! t->reqs)
    return;

  for (i = 0; i < t->nsrc; i++)
    kos_iter_close(t->reqs[i].iter);
}


KosIterStepResult kos_transform_throw(
    KosContext *ctx,
    PyObject *self,
    PyObject *throw_typ,
    PyObject *throw_exc,
    PyObject **out) {

  KosTransform *t = (KosTransform *) self;

  (void) ctx;
  (void) out;

  /* A step is in flight and its driver owns the transform's state:
     every second caller is refused, GeneratorExit included -- the
     same rule CPython applies to a running async generator. */
  if (kos_transform_busy(self, NULL))
    return KOS_ITER_ERROR;

  if (PyErr_GivenExceptionMatches(throw_typ, PyExc_GeneratorExit)) {
    /* Generator close semantics: run the source's cleanup, mark
       closed, and let GeneratorExit propagate as the success shape. */
    t->closed = 1;
    t->busy = KOS_TF_IDLE;
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
  t->busy = KOS_TF_IDLE;
  close_sources(t);

  return 0;
}


void kos_transform_finish(PyObject *self, int exhausted) {

  KosTransform *t = (KosTransform *) self;
  int staged = (t->busy == KOS_TF_STAGED);

  t->busy = KOS_TF_IDLE;
  Py_CLEAR(t->pending);
  if (exhausted)
    t->closed = 1;
  /* last: the stage's reference may be the transform's last */
  if (staged)
    Py_DECREF(self);
}


void kos_transform_step_clear(void *state) {

  KosTransform *t = (KosTransform *) state;
  int staged = (t->busy == KOS_TF_STAGED);

  t->busy = KOS_TF_IDLE;
  Py_CLEAR(t->pending);
  /* last: the stage's reference may be the transform's last */
  if (staged)
    Py_DECREF((PyObject *) t);
}


int kos_transform_step_traverse(void *state, visitproc visit, void *arg) {

  KosTransform *t = (KosTransform *) state;

  if (t->busy == KOS_TF_STAGED)
    Py_VISIT((PyObject *) t);
  return 0;
}


/**
   The transform's durable context, created lazily: python-adapter
   steps run their work here instead of allocating a context per
   element. The suite marks it recyclable, so the adapters reset it at
   their completion points. NULL with the error set on a creation
   failure, and the step fails with it.
*/
KosContext *kos_transform_pyctx(PyObject *self) {

  KosTransform *t = (KosTransform *) self;

  if (! t->ctx)
    t->ctx = KosContext_New(NULL, kos_tls_current());
  return t->ctx;
}


/* === GC + dealloc === */


int kos_transform_traverse(PyObject *self, visitproc visit, void *arg) {

  KosTransform *t = (KosTransform *) self;

  Py_VISIT(t->fn.cs_callable);
  Py_VISIT(t->src.iter);
  Py_VISIT(t->pending);
  Py_VISIT(t->ctx);
  return 0;
}


int kos_transform_clear(PyObject *self) {

  KosTransform *t = (KosTransform *) self;

  /* the context goes first, so a chain no one else holds is freed
     while the source is still the transform's. A source generator
     with its node on a chain held elsewhere may go first: the chain's
     later free does not touch the generator. A source with a step in
     flight is held by that step's stage reference, so dropping it
     here frees nothing a live frame names; the last holder frees. */
  Py_CLEAR(t->ctx);
  KosCallShape_Clear(&t->fn);
  Py_CLEAR(t->src.iter);
  Py_CLEAR(t->pending);
  /* a cleared transform has nothing left to step; closed makes any
     later step report exhaustion instead of reading the freed fields */
  t->closed = 1;
  return 0;
}


/**
   The owners of the GEN nodes on ctx's chain, innermost first, each a
   strong reference, so the generators outlive the close of the chain
   that holds them only borrowed. NULL when the chain carries none or
   the list cannot be had (the error cleared): the close then runs
   without them.
*/
static PyObject *transform_chain_gens(KosContext *ctx) {

  KosDynNode *n = (KosDynNode *) kos_dyn_find_gen(ctx);
  PyObject *owners;

  if (! n)
    return NULL;

  owners = PyList_New(0);
  if (! owners) {
    PyErr_Clear();
    return NULL;
  }
  for (; n; n = KosDynNode_PARENT(n)) {
    if (KosDynNode_IS_GEN(n)
        && PyList_Append(owners, KosDynGen_OWNER((KosDynGen *) n)) < 0) {
      PyErr_Clear();
      break;
    }
  }
  return owners;
}


/**
   Finalizer: a transform dropped with a step left mid-await on its own
   context (its stepper dropped first) closes that step before anything
   is cleared. The step is the context's: the chain is closed from its
   top through the context (kos_close_drive), so every generator the
   step installed on it finishes with GeneratorExit and its cleanup
   runs now, innermost first, as a dropped coroutine closes the
   generators on its chain. The step is then spent, and what the close
   owes is reported against the innermost generator when the chain
   carried one, else against the transform. Each generator on the
   chain ends closed: a close that reached its GEN node finished it,
   and one the close could not finish (a cleanup that suspended) is
   finished here, its span gone with the chain. A step on a
   consumer's chain leaves this context empty, and belongs to that
   consumer; one a drive is running is that drive's.

   The close is a drive of its own, begun from the finalizer: the
   sanctioned exception to flattening, as for the generator's.
*/
static void transform_finalize(PyObject *self) {

  KosTransform *t = (KosTransform *) self;
  KosContext *ctx = t->ctx;
  PyObject *fence_typ, *fence_val, *fence_tb;
  PyObject *typ, *val, *tb;
  PyObject *gens = NULL;
  KosIterSlots *islots;

  if (t->busy == KOS_TF_IDLE || ! ctx
      || (KosContext_FLAGS(ctx) & KOS_CTX_RUNNING))
    return;
  if (! KosContext_DYN_TOP(ctx)
      && ! (KosContext_FLAGS(ctx) & KOS_CTX_CLEARED))
    return;

  PyErr_Fetch(&fence_typ, &fence_val, &fence_tb);
  Py_INCREF(ctx);

  if (! (KosContext_FLAGS(ctx) & KOS_CTX_CLEARED))
    gens = transform_chain_gens(ctx);

  (void) kos_close_drive(ctx, &typ, &val, &tb);
  if (KosIter_CheckSlots(self, &islots))
    islots->ki_finish(self, 1);
  KosContext_ReleaseChain(ctx);

  if (gens) {
    Py_ssize_t i;

    for (i = 0; i < PyList_GET_SIZE(gens); i++) {
      PyObject *owner = PyList_GET_ITEM(gens, i);
      if (KosIter_CheckSlots(owner, &islots))
        islots->ki_finish(owner, 1);
    }
  }

  kos_close_report(
      (gens && PyList_GET_SIZE(gens)) ? PyList_GET_ITEM(gens, 0) : self,
      typ, val, tb);

  Py_XDECREF(gens);
  Py_DECREF(ctx);
  PyErr_Restore(fence_typ, fence_val, fence_tb);
}


void kos_transform_dealloc(PyObject *self) {

  KosTransform *t = (KosTransform *) self;

  /* A transform over a transform over ... releases one link per
     dealloc, each clear dropping the source that is the next. The
     trashcan defers that recursion off the C stack, as CPython's own
     map does; it needs the object untracked. */
  PyObject_GC_UnTrack(self);
  Py_TRASHCAN_BEGIN(self, kos_transform_dealloc);

  /* Only a step in flight gives the finalizer anything to do, so an
     idle transform pays the busy test alone. The finalizer may
     resurrect the transform, in which case the dealloc is abandoned
     here and happens on the next drop; a resurrected object must be
     tracked, so the finalizer runs tracked. */
  if (t->busy != KOS_TF_IDLE && t->ctx) {
    PyObject_GC_Track(self);
    if (PyObject_CallFinalizerFromDealloc(self) < 0)
      goto done;
    PyObject_GC_UnTrack(self);
  }

  /* the type's clear, so a provider's own fields go in the one order
     its tp_clear states. The context teardown runs first: a frame
     still on the chain that KosNativeFrame_CacheOnFree flagged is
     stored into cached_frame, and the free below reclaims it either
     way */
  Py_TYPE(self)->tp_clear(self);
  if (t->cached_frame) {
    kos_dynframe_free(t->cached_frame);
    t->cached_frame = NULL;
  }
  Py_TYPE(self)->tp_free(self);

done:
  Py_TRASHCAN_END;
}


PyObject *kos_transform_repr(PyObject *self) {

  return PyUnicode_FromFormat("<koskari %s object at %p>",
                              transform_kind(Py_TYPE(self)), self);
}


/* Retract the view and release the requests' iterators and the array
   that held them: the view goes first, since the base reads it
   whenever it closes and tp_clear runs on a live object. */
static void multi_release_srcs(KosTransformMulti *m) {

  KosIterRequest *srcs = m->srcs;
  uint16_t nsrc = m->base.nsrc;
  uint16_t i;

  m->base.reqs = NULL;
  m->base.nsrc = 0;
  m->srcs = NULL;
  if (! srcs)
    return;
  for (i = 0; i < nsrc; i++)
    Py_CLEAR(srcs[i].iter);
  PyMem_Free(srcs);
}


int kos_transform_multi_traverse(PyObject *self, visitproc visit, void *arg) {

  KosTransformMulti *m = (KosTransformMulti *) self;
  uint16_t i;

  for (i = 0; m->srcs && i < m->base.nsrc; i++)
    Py_VISIT(m->srcs[i].iter);
  Py_VISIT(m->row);
  return kos_transform_traverse(self, visit, arg);
}


int kos_transform_multi_clear(PyObject *self) {

  KosTransformMulti *m = (KosTransformMulti *) self;

  multi_release_srcs(m);
  Py_CLEAR(m->row);

  return kos_transform_clear(self);
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

    /* every provider shares the one finalizer (transform_finalize) */
    type->tp_finalize = transform_finalize;
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
