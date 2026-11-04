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


#ifndef KOSKARI_TRANSFORM_H
#define KOSKARI_TRANSFORM_H

#include <Python.h>

#include "callable.h"
#include "eval.h"


/**
   KosTransform: the lazy KosIter provider body that every provider
   under transform/ (map, filter, zip and the koskari.itertools set) is
   an instance of. This unit holds everything its providers share: the
   instance layout, one-source construction, the GC and Python
   surfaces, the suite functions other than the step itself, and the
   staging of a resume-carrying frame. Each provider keeps only its own
   step and the resume chain that step arms.

   A provider embeds KosTransform first, sets a larger tp_basicsize,
   and chains its traverse and clear to the base's. Every source is a
   bound request, its kind and provider suite classified once at
   acquisition so no step classifies it again; sources beyond the first
   live in storage the provider owns and publishes through the reqs
   view, which the base reads only to close what it holds.

   :author: Christopher O'Brien <obriencj@gmail.com>
   :license: GNU General Public License v3
*/


/**
   The busy mark. A step staged on a frame naming the transform as its
   resume state holds one reference to the transform until ki_finish
   or the step clear, so a holder released while that frame is still
   on a chain -- a containing transform's clear dropping its source --
   cannot free the transform under the frame. A frame on the
   transform's own context holds none (kos_transform_staged).
*/
typedef enum KosTransformBusy {
  KOS_TF_IDLE = 0,
  KOS_TF_STEPPING,  /* a step runs, or is installed with no frame naming it */
  KOS_TF_STAGED,    /* a frame names the transform: one reference held */
  KOS_TF_STAGED_OWN, /* a frame on the transform's own context names
                        it: no reference held (kos_transform_staged) */
} KosTransformBusy;


typedef struct KosTransform {
  PyObject_HEAD
  KosCallShape fn;       /* callable applied per element, shape
                            resolved at construction; cs_callable NULL
                            for filter's None predicate (the element's
                            truthiness decides) */
  KosIterRequest src;    /* arity-1 storage: the acquired iterator
                            (sync or async), owned, bound with its kind
                            and provider suite once at acquisition
                            (kos_iter_request_bind_next); reqs points
                            here */
  KosIterRequest *reqs;  /* borrowed view of nsrc bound requests, never
                            owned: &src at arity 1, else a provider's
                            own storage. A provider retracts the view
                            before releasing that storage, since
                            tp_clear runs while the object is still
                            alive */
  PyObject *pending;     /* value in flight across a suspension --
                            filter's element under test, a partial row
                            -- owned, and cleared at every step
                            completion by ki_finish. State that must
                            outlive a step belongs to the provider */
  KosDynFrame *cached_frame; /* inert frame cached between steps */
  KosContext *ctx;       /* durable drive context (ki_pyctx), created
                            lazily; adapter-recycled, freed with the
                            transform */
  uint16_t nsrc;         /* entries in sources; 0 once retracted */
  uint16_t idx;          /* resume position within a step that fetches
                            more than once; untouched by providers
                            that fetch a single element per step */
  uint8_t closed;        /* exhausted or closed */
  uint8_t busy;          /* KosTransformBusy: a step is in flight, from
                            kos_transform_step entry to its synchronous
                            return, or to ki_finish or the step clear
                            when installed */
} KosTransform;


/**
   What None means in a provider's callable position. A provider that
   substitutes a default for None rather than dropping the callable
   resolves that in its own tp_new; this enum does not carry it.
*/
typedef enum KosTransformNone {
  KOS_TF_NONE_REJECT = 0,  /* None is not callable here: TypeError */
  KOS_TF_NONE_BARE,        /* None means no callable at all, the way
                              filter falls back to truthiness */
} KosTransformNone;


/**
   How a provider's tp_new reads its argument tuple.
*/
typedef struct KosTransformSpec {
  uint8_t has_callable;  /* the leading argument is the callable */
  uint8_t none_means;    /* KosTransformNone; read only when
                            has_callable */
} KosTransformSpec;


/**
   Fill a freshly allocated one-source transform: acquire the iterator
   from iterable, resolve func's call shape when there is one (NULL or
   None leaves the transform with no callable), publish the view over
   the base's own source slot, and reset the step state (pending, cached
   frame, context, closed and busy). The provider's own fields are left
   to it.

   A provider that parses its own argument shape -- trailing bounds, a
   count -- allocates, calls this, then stores its own fields. One
   that takes the plain shape goes through kos_transform_new instead.

   :param t: freshly allocated, zeroed
   :param func: the callable, or NULL/None for none
   :param iterable: borrowed
   :return: 0, or -1 with PyErr set
*/
int kos_transform_init_one(
    KosTransform *t,
    PyObject *func,
    PyObject *iterable);


/**
   Fill a freshly allocated transform whose sources live in storage
   the provider owns: no callable, the base's own source slot empty,
   the view published over that storage, and the step state reset as
   kos_transform_init_one does. The provider acquires into its storage
   before or after this call as it likes; the view is only read once
   the transform steps or closes.

   :param t: freshly allocated, zeroed
   :param reqs: the view, nsrc bound requests the provider keeps alive
                until it retracts the view
   :param nsrc: entries in the view
*/
void kos_transform_init_view(
    KosTransform *t,
    KosIterRequest *reqs,
    uint16_t nsrc);


/**
   The shape of a provider over several sources acquired together
   (map, zip, zip-longest, compress): the base and the array of bound
   requests that owns the acquired iterators and that the base's view
   points into. A provider with fields of its own embeds this first and
   chains its traverse, clear and dealloc to the multi ones, as a
   one-source provider chains to the base's. A provider that takes one
   source or several (map) is always this shape, with the one-source
   case keeping its request in the base's own slot and srcs NULL.
*/
typedef struct KosTransformMulti {
  KosTransform base;
  /* the nsrc bound requests, owning their iterators, the storage the
     base's reqs view points into; traversed and cleared here. NULL
     when the one source lives in the base's slot */
  KosIterRequest *srcs;
  /* a tuple of nsrc elements, one per source, refilled in place for
     each step. zip: the last row emitted, reused while zip alone
     holds it, NULL before the first. map: the argument vector of the
     call, private to the map, allocated at construction when there is
     more than one source and NULL otherwise */
  PyObject *row;
} KosTransformMulti;


/* Store an element at a row position, releasing the item a reused row
   held there; a fresh row's slot is NULL. element is consumed. */
static inline void kos_transform_row_set(
    PyObject *row,
    Py_ssize_t i,
    PyObject *element) {

  PyObject *old = PyTuple_GET_ITEM(row, i);

  PyTuple_SET_ITEM(row, i, element);
  Py_XDECREF(old);
}


/* A row just completed: the collector may have untracked a reused row
   whose items were all untrackable, and the refill can change that,
   so it is tracked again as CPython's zip does. */
static inline void kos_transform_row_retrack(PyObject *row) {
  if (! PyObject_GC_IsTracked(row))
    PyObject_GC_Track(row);
}


/**
   Fill a freshly allocated multi-source transform: acquire every
   iterable of the tuple into the srcs array of bound requests and
   publish the view over it.

   :param m: freshly allocated, zeroed
   :param iterables: borrowed tuple, at most UINT16_MAX long
   :return: 0, or -1 with PyErr set
*/
int kos_transform_init_multi(KosTransformMulti *m, PyObject *iterables);



/* The GC of a multi-source provider: the view is retracted before the
   storage it points into goes, since tp_clear runs while the object
   is still alive and the base reads the view whenever it closes. The
   dealloc is kos_transform_dealloc, which clears through tp_clear. */
int kos_transform_multi_traverse(PyObject *self, visitproc visit, void *arg);
int kos_transform_multi_clear(PyObject *self);


/**
   tp_new body for a one-source provider: KIND(func, iterable) when the
   spec declares a callable, KIND(iterable) otherwise. The iterator is
   acquired eagerly with the PUSH_FOREACH preference order: a KosIter
   provider or any iterator passes through as itself, an async iterable
   yields its async iterator, otherwise the sync protocol. The view is
   published over the base's own source slot.

   A multi-source provider acquires into storage it owns and publishes
   the view itself; this entry does not serve it.
*/
PyObject *kos_transform_new(
    PyTypeObject *type,
    PyObject *args,
    PyObject *kwds,
    const KosTransformSpec *spec);


/**
   A provider's step body: the work of one ki_step after the shared
   entry has admitted it.
*/
typedef KosIterStepResult (*KosTransformStepBody)(
    KosContext *ctx,
    PyObject *self,
    PyObject *send_val,
    PyObject **out);


/* The short kind name ("map", "filter") for messages: the class
   __name__, which is the last segment of the type's dotted name. */
static inline const char *transform_kind(PyTypeObject *type) {
  const char *name = type->tp_name;
  const char *dot = strrchr(name, '.');

  return dot ? dot + 1 : name;
}


/**
   The suite's ki_busy: 1 with "KIND is already being iterated" set as
   a ValueError when a step is in flight, else 0, whichever method
   asks. kos_transform_step and the throw refuse a busy transform
   through it.
*/
int kos_transform_busy(PyObject *self, const char *method);


/**
   The ki_step entry every provider steps through: a closed transform
   is exhausted, a busy one refuses, and a non-None send value is a
   TypeError. Otherwise the transform is marked busy and body runs.
   The mark covers the whole step, from entry to a synchronous return
   or, for an installed step (KOS_ITER_INSTALLED or
   KOS_ITER_INSTALLED_VALUE), to ki_finish or the step clear, so a
   source fetch or a plain callable running synchronously in body is
   covered as well.
*/
KOS_OP_INLINE KosIterStepResult kos_transform_step(
    KosContext *ctx,
    PyObject *self,
    PyObject *send_val,
    PyObject **out,
    KosTransformStepBody body) {

  KosTransform *t = (KosTransform *) self;
  KosIterStepResult result;

  if (t->closed) {
    *out = NULL;
    return KOS_ITER_EXHAUSTED;
  }

  if (kos_transform_busy(self, NULL))
    return KOS_ITER_ERROR;

  if (send_val && send_val != Py_None) {
    PyErr_Format(PyExc_TypeError,
                 "can't send a value into a %s; it takes none",
                 transform_kind(Py_TYPE(self)));
    return KOS_ITER_ERROR;
  }

  t->busy = KOS_TF_STEPPING;
  result = body(ctx, self, send_val, out);

  /* an installed step keeps the mark until ki_finish or the step
     clear; every synchronous outcome releases it here */
  if (result != KOS_ITER_INSTALLED && result != KOS_ITER_INSTALLED_VALUE)
    t->busy = KOS_TF_IDLE;

  return result;
}


/**
   Is the pending exception a source running out? A provider that
   carries on past one source's end (chain, cycle, batched,
   zip-longest) asks this of a NULL delivery before handling it; any
   other exception, or none, propagates.
*/
int kos_transform_source_spent(void);


/**
   A fetch resume's first act: the source's step, deferred or not, is
   over. An async source that suspended before its end delivers that
   end as StopAsyncIteration; the transform is a provider on the sync
   protocol, whose end is StopIteration, so the source's end is
   restated as the transform's own here, before the finish, the spent
   test and the consumer judge it. A provider is finished (ki_finish),
   exhausted when the delivery is its end; a plain iterator needs
   nothing. The transform owns its source, so the finish is its own.
*/
static inline void kos_transform_fetched(
    const KosIterRequest *req,
    PyObject *element) {

  if (! element && PyErr_ExceptionMatches(PyExc_StopAsyncIteration)) {
    PyErr_Clear();
    PyErr_SetNone(PyExc_StopIteration);
  }
  if (req->islots)
    kos_iter_finish(req->iter, req->islots,
                    ! element && kos_transform_source_spent());
}


/**
   Decline a failed delivery from a transform's resume: the step frame
   is set to return to the transform's cache when the raise walk frees
   it, and the pending exception propagates.
*/
static inline KosOpResult kos_transform_decline(
    KosContext *ctx,
    KosTransform *t) {

  KosNativeFrame_CacheOnFree(ctx, &t->cached_frame);
  return EVAL_RAISE();
}


/**
   Undo a partially installed step before reporting KOS_ITER_ERROR:
   release the held element and pop everything pushed since the step
   began (the program frame, a failed call's prepped frame), so the
   suite contract -- a non-INSTALLED outcome leaves no partial state
   -- holds on every error exit. The step's real exception is fenced
   across the unwind.
*/
KosIterStepResult kos_transform_step_abort(
    KosContext *ctx,
    KosTransform *t,
    KosDynNode *pre_top);


/**
   Install the step's resume-carrying frame (from the cache when
   one is there), push value onto it when one is given, and set resume
   as the frame's resume with the transform as its state. The busy
   mark kos_transform_step set stays on through the install, and the
   install takes the stage's reference (kos_transform_staged). value is
   consumed. label names the frame for the capture-refusal message.
   Returns KOS_ITER_INSTALLED_VALUE, or aborts the step to pre_top and
   returns KOS_ITER_ERROR.
*/
KosIterStepResult kos_transform_stage(
    KosContext *ctx,
    KosTransform *t,
    PyObject *value,
    const KosResumeKind *resume,
    KosDynNode *pre_top);


/**
   Mark the step staged, once per step, taking the stage's reference
   when the frame sits on another context's chain. A frame on the
   transform's own context (a Python adapter's step) takes none: the
   transform holds that context, so the reference would be a cycle
   through it, and the transform dropped mid-await could not be
   finalized until a collection. Without it the frame never outlives
   the transform: the transform's clear frees its context before
   anything else it holds, and a stepper holding the context and the
   transform drops the context first (stepper_release_state).
*/
static inline void kos_transform_staged(KosContext *ctx, KosTransform *t) {

  if (t->busy == KOS_TF_STAGED || t->busy == KOS_TF_STAGED_OWN)
    return;
  if (ctx == t->ctx) {
    t->busy = KOS_TF_STAGED_OWN;
    return;
  }
  t->busy = KOS_TF_STAGED;
  Py_INCREF(t);
}


/**
   KosContext_NextOrDefer with the transform as the frame's state and
   its cache as the frame cache; a frame installed takes the stage's
   reference
*/
KOS_OP_INLINE KosIterRequestStatus kos_transform_next_or_defer(
    KosContext *ctx,
    KosTransform *t,
    const KosIterRequest *req,
    const KosResumeKind *resume,
    PyObject **out) {

  KosIterRequestStatus s = KosContext_NextOrDefer(
      ctx, req, resume, t, &t->cached_frame, out);

  if (s == KOS_IREQ_CONTINUES)
    kos_transform_staged(ctx, t);
  return s;
}


/* A provider step kind taking its delivery as it is. No clone: a
   capture across the step refuses by name. */
#define KOS_TRANSFORM_STEP_KIND_AS_IS(ident, label, fnp)                \
  static const KosResumeKind ident = {                                  \
    (label), (fnp), kos_transform_step_clear, NULL, NULL, 0,            \
    kos_transform_step_traverse }

/* A provider step kind taking a call's result, settled first. */
#define KOS_TRANSFORM_STEP_KIND(ident, label, fnp)                      \
  static const KosResumeKind ident = {                                  \
    (label), (fnp), kos_transform_step_clear, NULL, NULL, 1,            \
    kos_transform_step_traverse }


/**
   The fetch a provider's step makes, with the outcomes every provider
   handles alike: on DELIVERED element is the new reference and the
   step goes on; on EXHAUSTED the provider closes and reports it; on
   CONTINUES the fetch was deferred to resume on an installed frame
   and the step reports its work installed; on ERROR the step aborts
   to pre_top. req is the source's bound request, whose kind decides
   the fetch.
*/
#define KOS_TRANSFORM_FETCH(ctx, t, req, resume, pre_top, out, element)  \
  do {                                                                  \
    switch (kos_transform_next_or_defer((ctx), (KosTransform *) (t),  \
                                        (req), (resume), &(element))) { \
      case KOS_IREQ_DELIVERED:                                          \
        break;                                                          \
      case KOS_IREQ_EXHAUSTED:                                          \
        (t)->closed = 1;                                                \
        *(out) = NULL;                                                  \
        return KOS_ITER_EXHAUSTED;                                      \
      case KOS_IREQ_CONTINUES:                                          \
        return KOS_ITER_INSTALLED_VALUE;                                \
      default:                                                          \
        return kos_transform_step_abort((ctx), (t), (pre_top));         \
    }                                                                   \
  } while (0)


/* The suite functions other than ki_step, shared by every provider. */
KosIterStepResult kos_transform_throw(
    KosContext *ctx,
    PyObject *self,
    PyObject *throw_typ,
    PyObject *throw_exc,
    PyObject **out);
int kos_transform_close(PyObject *self);
void kos_transform_finish(PyObject *self, int exhausted);


/**
   The clear of every provider step kind: a step frame torn down with
   its resume still set releases the record's busy mark and the value
   in flight, so the transform can be stepped again, and the stage's
   reference last. Takes the KosTransform the frame's state points at
*/
void kos_transform_step_clear(void *state);

/* The traverse of every provider step kind: the staged reference. */
int kos_transform_step_traverse(void *state, visitproc visit, void *arg);
KosContext *kos_transform_pyctx(PyObject *self);


/* The GC and Python surfaces, shared by every provider. */
int kos_transform_traverse(PyObject *self, visitproc visit, void *arg);
int kos_transform_clear(PyObject *self);
void kos_transform_dealloc(PyObject *self);
PyObject *kos_transform_repr(PyObject *self);

extern PyMethodDef kos_transform_methods[];
extern PyAsyncMethods kos_transform_async_methods;


/* === The providers ===
   Each is its own file under transform/, included into transform.c.
   They declare nothing of their own, so their types are declared
   here. */

extern KosEvaluatedTypeObject KosMapType;
extern KosEvaluatedTypeObject KosFilterType;
extern KosEvaluatedTypeObject KosFilterFalseType;
extern KosEvaluatedTypeObject KosZipType;
extern KosEvaluatedTypeObject KosZipLongestType;
extern KosEvaluatedTypeObject KosChainType;
extern KosEvaluatedTypeObject KosCompressType;
extern KosEvaluatedTypeObject KosCycleType;
extern KosEvaluatedTypeObject KosAccumulateType;
extern KosEvaluatedTypeObject KosBatchedType;
extern KosEvaluatedTypeObject KosDropWhileType;
extern KosEvaluatedTypeObject KosTakeWhileType;
extern KosEvaluatedTypeObject KosStarMapType;
extern KosEvaluatedTypeObject KosISliceType;
extern KosEvaluatedTypeObject KosPairwiseType;


#define KosMap_CheckExact(obj) \
  ((obj) && ((obj)->ob_type == KOS_EVALUATED_PYTYPE(KosMapType)))

#define KosFilter_CheckExact(obj) \
  ((obj) && ((obj)->ob_type == KOS_EVALUATED_PYTYPE(KosFilterType)))

#define KosZip_CheckExact(obj) \
  ((obj) && ((obj)->ob_type == KOS_EVALUATED_PYTYPE(KosZipType)))

#define KosZipLongest_CheckExact(obj) \
  ((obj) && ((obj)->ob_type == KOS_EVALUATED_PYTYPE(KosZipLongestType)))

#define KosChain_CheckExact(obj) \
  ((obj) && ((obj)->ob_type == KOS_EVALUATED_PYTYPE(KosChainType)))

#define KosCompress_CheckExact(obj) \
  ((obj) && ((obj)->ob_type == KOS_EVALUATED_PYTYPE(KosCompressType)))

#define KosCycle_CheckExact(obj) \
  ((obj) && ((obj)->ob_type == KOS_EVALUATED_PYTYPE(KosCycleType)))

#define KosAccumulate_CheckExact(obj) \
  ((obj) && ((obj)->ob_type == KOS_EVALUATED_PYTYPE(KosAccumulateType)))

#define KosBatched_CheckExact(obj) \
  ((obj) && ((obj)->ob_type == KOS_EVALUATED_PYTYPE(KosBatchedType)))

#define KosDropWhile_CheckExact(obj) \
  ((obj) && ((obj)->ob_type == KOS_EVALUATED_PYTYPE(KosDropWhileType)))

#define KosTakeWhile_CheckExact(obj) \
  ((obj) && ((obj)->ob_type == KOS_EVALUATED_PYTYPE(KosTakeWhileType)))

#define KosStarMap_CheckExact(obj) \
  ((obj) && ((obj)->ob_type == KOS_EVALUATED_PYTYPE(KosStarMapType)))

#define KosISlice_CheckExact(obj) \
  ((obj) && ((obj)->ob_type == KOS_EVALUATED_PYTYPE(KosISliceType)))

#define KosPairwise_CheckExact(obj) \
  ((obj) && ((obj)->ob_type == KOS_EVALUATED_PYTYPE(KosPairwiseType)))


/**
   Ready every provider and insert it into the module dict.
   KosTransform is a struct rather than a type, so the base has
   nothing to ready ahead of them and the order is free.
*/
int koskari_transform_init(PyObject *mod);
int koskari_transform_clear(PyObject *mod);


#endif /* KOSKARI_TRANSFORM_H */


// The end.
