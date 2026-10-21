/*
  This library is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 3 of the License, or
  (at your option) any later version.

  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this library; if not, see <http://www.gnu.org/licenses/>.
*/


#ifndef KOSKARI_DEMAND_H
#define KOSKARI_DEMAND_H


#include <Python.h>

#include "callable.h"


extern KosEvaluatedTypeObject KosDemandCallableType;


/**
   The layout of Demand and its subtypes (Procedure, IterFunction).
   vectorcall is the per-type Python vectorcall entry, taken from the
   type's kc_pyvectorcall at construction; the types are method
   descriptors, so CPython calls them unbound with self prepended.
*/
typedef struct {
  PyObject_HEAD
  PyObject *wrapped;
  vectorcallfunc vectorcall;
  KosContext *cached_ctx;   /* the drive context boundary calls reuse;
                               NULL until the first one caches it, and
                               detached while a call runs */
  KosDynFrame *cached_frame; /* the inert code-less root frame boundary
                               calls reuse; NULL until the first one
                               returns it, and emptied while a call
                               runs */
} KosDemandObject;

#define KosDemand_VECTORCALL_OFFSET offsetof(KosDemandObject, vectorcall)


#define KosDemand_CheckExact(obj) \
  ((obj) && ((obj)->ob_type == KOS_EVALUATED_PYTYPE(KosDemandCallableType)))

#define KosDemand_Check(obj) \
  ((obj) && PyObject_TypeCheck((obj), KOS_EVALUATED_PYTYPE(KosDemandCallableType)))


/**
   Return the inner callable held by a Demand instance.

   obj must be a Demand. Returns a borrowed reference.
*/
PyObject *KosDemand_GetWrapped(PyObject *obj);


/**
   Create a new Demand wrapping the given callable. Returns a new reference.
   Idempotent: if wrapped is already a Demand, the same object is returned.

   wrapped is borrowed. Caller retains ownership.
*/
PyObject *KosDemand_New(PyObject *wrapped);


int koskari_demand_init(PyObject *mod);
int koskari_demand_clear(PyObject *mod);

/**
   Run callable on the demand path from the Python boundary and return
   its finished result: a KosCallable runs to completion on a fresh
   context with suspension denied; a plain Python callable is called
   and an awaitable result stepped, and denied if it suspends. args
   (may be NULL) and kwargs (may be NULL) are borrowed. Returns a new
   reference, or NULL with PyErr set.
*/
PyObject *Koskari_DemandSync(PyObject *callable, PyObject *args, PyObject *kwargs);

/**
   Koskari_DemandSync over a vectorcall-shaped argument vector: args
   holds nargs positionals then one value per kwnames entry (kwnames
   may be NULL). args and kwnames are borrowed. 0 with *out_result a
   new reference, or -1 with PyErr set.
*/
int Koskari_DemandSyncVector(
    PyObject *callable,
    PyObject *const *args,
    Py_ssize_t nargs,
    PyObject *kwnames,
    PyObject **out_result);


/**
   A Demand wrapper, or a subtype, called from Python: the demand core
   over the vector on the wrapped callable, driving the context the
   wrapper caches. args holds nargsf positionals (vectorcall-encoded)
   then one value per kwnames entry. New reference, or NULL with PyErr
   set; TypeError when the wrapper holds nothing.
*/
PyObject *KosDemand_CallVector(
    PyObject *self,
    PyObject *const *args,
    size_t nargsf,
    PyObject *kwnames);


/**
   Set KoskariSyncViolation on PyErr with msg as the message string.

   Called from eval guard sites (kos_eval_demand_deny_suspend) to raise the
   violation as an in-eval exception without duplicating exception registration.
*/
void kos_demand_set_sync_violation(const char *msg);


#endif /* KOSKARI_DEMAND_H */


// The end.
