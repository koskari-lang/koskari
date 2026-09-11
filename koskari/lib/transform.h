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
   KosTransform: the lazy one-source, one-callable KosIter provider that
   map (map.c) and filter (filter.c) are the two instances of. This
   unit holds everything the two share: the instance layout,
   construction, the GC and Python surfaces, the suite functions other
   than the step itself, and the staging of a resume-carrying frame.
   Each instance keeps only its own step and the resume chain that
   step arms.

   :author: Christopher O'Brien <obriencj@gmail.com>
   :license: GNU General Public License v3
*/


typedef struct KosTransform {
  PyObject_HEAD
  KosCallShape fn;       /* callable applied per element, shape
                            resolved at construction; cs_callable NULL
                            for filter's None predicate (the element's
                            truthiness decides) */
  PyObject *source;      /* acquired iterator (sync or async) */
  PyObject *pending;     /* filter's element under test while a
                            verdict is in flight; owned. map never
                            sets it */
  KosDynFrame *cached_frame; /* inert frame parked between steps */
  KosContext *ctx;       /* durable drive context (ki_pyctx), created
                            lazily; adapter-recycled, freed with the
                            transform */
  uint8_t src_sync;      /* source steps through tp_iternext alone */
  uint8_t closed;        /* exhausted or closed */
  uint8_t busy;          /* a step is in flight; ki_finish clears */
} KosTransform;


/**
   tp_new body shared by both types: KIND(func, iterable). allow_none
   admits None as the callable. The iterator is acquired eagerly with
   the PUSH_FOREACH preference order: a KosIter provider or any
   iterator passes through as itself, an async iterable yields its
   async iterator, otherwise the sync protocol.
*/
PyObject *kos_transform_new(
    PyTypeObject *type,
    PyObject *args,
    PyObject *kwds,
    int allow_none);


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
