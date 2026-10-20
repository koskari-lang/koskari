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
   and chains its traverse and clear to the base's. Sources beyond the
   first live in storage the provider owns and publishes through the
   sources view; the base reads that view only to close what it holds.

   :author: Christopher O'Brien <obriencj@gmail.com>
   :license: GNU General Public License v3
*/


typedef struct KosTransform {
  PyObject_HEAD
  KosCallShape fn;       /* callable applied per element, shape
                            resolved at construction; cs_callable NULL
                            for filter's None predicate (the element's
                            truthiness decides) */
  PyObject *source;      /* arity-1 storage: acquired iterator (sync
                            or async); sources points here */
  PyObject **sources;    /* borrowed view of nsrc acquired iterators,
                            never owned: &source at arity 1, else a
                            provider's own storage. A provider
                            retracts the view before releasing that
                            storage, since tp_clear runs while the
                            object is still alive */
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
  uint8_t busy;          /* a step is in flight; ki_finish clears */
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
   :param sources: the view, nsrc slots the provider keeps alive until
                   it retracts the view
   :param nsrc: entries in the view
*/
void kos_transform_init_view(
    KosTransform *t,
    PyObject **sources,
    uint16_t nsrc);


/**
   The shape of a provider over several sources acquired together
   (zip, zip-longest, compress): the base and the tuple that holds
   the acquired iterators and that the base's view points into. A provider
   with fields of its own embeds this first and chains its traverse,
   clear and dealloc to the multi ones, as a one-source provider
   chains to the base's.
*/
typedef struct KosTransformMulti {
  KosTransform base;
  PyObject *srcs;      /* tuple of acquired iterators; the storage the
                          base's sources view points into */
  PyObject *row;       /* zip: the last row emitted, refilled in place
                          for the next element while zip alone holds
                          it; NULL before the first */
} KosTransformMulti;


/**
   Fill a freshly allocated multi-source transform: acquire every
   iterable of the tuple into the srcs tuple and publish the view over
   the tuple's items.

   :param m: freshly allocated, zeroed
   :param iterables: borrowed tuple, at most UINT16_MAX long
   :return: 0, or -1 with PyErr set
*/
int kos_transform_init_multi(KosTransformMulti *m, PyObject *iterables);



/* The GC and dealloc of a multi-source provider: the view is
   retracted before the storage it points into goes, since tp_clear
   runs while the object is still alive and the base reads the view
   whenever it closes. */
int kos_transform_multi_traverse(PyObject *self, visitproc visit, void *arg);
int kos_transform_multi_clear(PyObject *self);
void kos_transform_multi_dealloc(PyObject *self);


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
   The step preamble every ki_step runs first: a closed transform is
   exhausted, a busy one refuses, and a non-None send value is a
   TypeError. Returns 1 when the step should proceed, else 0 with
   *result and *out set for the caller to return.
*/
int kos_transform_step_ready(
    KosTransform *t,
    PyObject *send_val,
    PyObject **out,
    KosIterStepResult *result);


/**
   Is the pending exception a source running out? A provider that
   carries on past one source's end (chain, cycle, batched,
   zip-longest) asks this of a NULL delivery before handling it; any
   other exception, or none, propagates.
*/
int kos_transform_source_spent(void);


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
   one is there), push value onto it when one is given, set resume as
   the frame's resume with the transform as its state, and mark the
   step in flight. value is consumed. label names the frame for the
   capture-refusal message. Returns KOS_ITER_INSTALLED_VALUE, or aborts
   the step to pre_top and returns KOS_ITER_ERROR.
*/
KosIterStepResult kos_transform_stage(
    KosContext *ctx,
    KosTransform *t,
    PyObject *value,
    const KosResumeKind *resume,
    KosDynNode *pre_top);


/**
   The fetch a provider's step makes, with the outcomes every provider
   handles alike: on DELIVERED element is the new reference and the
   step goes on; on EXHAUSTED the provider closes and reports it; on
   CONTINUES the fetch was deferred to resume on an installed frame
   and the step reports its work installed; on ERROR the step aborts
   to pre_top. The kind of the source is the request entry's business.
*/
#define KOS_TRANSFORM_FETCH(ctx, t, source, resume, pre_top, out, element) \
  do {                                                                  \
    switch (KosEvalState_NextOrDefer((ctx), (source), (resume), (t),   \
                                     &(t)->cached_frame, &(element))) { \
      case KOS_IREQ_DELIVERED:                                          \
        break;                                                          \
      case KOS_IREQ_EXHAUSTED:                                          \
        (t)->closed = 1;                                                \
        *(out) = NULL;                                                  \
        return KOS_ITER_EXHAUSTED;                                      \
      case KOS_IREQ_CONTINUES:                                          \
        (t)->busy = 1;                                                  \
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
   in flight, so the transform can be stepped again. Takes the
   KosTransform the frame's state points at
*/
void kos_transform_step_clear(void *state);
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
