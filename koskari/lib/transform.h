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
   KosTransform: the lazy KosIter provider body that map (map.c) and
   filter (filter.c) are instances of. This unit holds everything its
   providers share: the instance layout, one-source construction, the
   GC and Python surfaces, the suite functions other than the step
   itself, and the staging of a resume-carrying frame. Each provider
   keeps only its own step and the resume chain that step arms.

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
  KosDynFrame *cached_frame; /* inert frame parked between steps */
  KosContext *ctx;       /* durable drive context (ki_pyctx), created
                            lazily; adapter-recycled, freed with the
                            transform */
  uint16_t nsrc;         /* entries in sources; 0 once retracted */
  uint16_t idx;          /* resume position within a step that fetches
                            more than once; untouched by providers
                            that fetch a single element per step */
  uint8_t src_sync;      /* the current source steps through
                            tp_iternext alone; re-resolved on each
                            acquisition by providers that swap sources
                            rather than fixed for the lifetime */
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
   None leaves the transform with no callable), and publish the view
   over the base's own source slot. Every other field is left as
   tp_alloc zeroed it.

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
   Undo a partially installed step before reporting KOS_ITER_ERROR:
   release the held element and pop everything pushed since the step
   began (the program frame, a failed call's prepped frame), so the
   suite contract -- a non-INSTALLED outcome leaves no partial state
   -- holds on every error exit. The step's real exception is fenced
   across the unwind.
*/
KosIterStepResult kos_transform_step_abort(
    KosEvalState *es,
    KosTransform *t,
    KosDynNode *pre_top);


/**
   Install the step's resume-carrying frame (from the parked cache when
   one is there), push value onto it when one is given, arm resume as
   the frame's resume with the transform as its state, and mark the
   step in flight. value is consumed. label names the frame for the
   capture-refusal message. Returns KOS_ITER_INSTALLED_VALUE, or aborts
   the step to pre_top and returns KOS_ITER_ERROR.
*/
KosIterStepResult kos_transform_stage(
    KosEvalState *es,
    KosTransform *t,
    PyObject *value,
    void *resume,
    const char *label,
    KosDynNode *pre_top);


/* The suite functions other than ki_step, shared by both types. */
KosIterStepResult kos_transform_throw(
    KosEvalState *es,
    PyObject *self,
    PyObject *throw_typ,
    PyObject *throw_exc,
    PyObject **out);
int kos_transform_close(PyObject *self);
void kos_transform_finish(PyObject *self, int exhausted);
KosContext *kos_transform_pyctx(PyObject *self);


/* The GC and Python surfaces, shared by both types. */
int kos_transform_traverse(PyObject *self, visitproc visit, void *arg);
int kos_transform_clear(PyObject *self);
void kos_transform_dealloc(PyObject *self);
PyObject *kos_transform_repr(PyObject *self);

extern PyMethodDef kos_transform_methods[];
extern PyAsyncMethods kos_transform_async_methods;


#endif /* KOSKARI_TRANSFORM_H */


// The end.
