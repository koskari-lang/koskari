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


#ifndef KOSKARI_OBJECT_H
#define KOSKARI_OBJECT_H


#include <Python.h>

#include "callable.h"


/* === KosType === */


extern KosCallableTypeObject KosTypeType;


#define KosType_Check(obj)                                           \
  ((obj) && PyType_Check(obj) &&                                     \
   PyType_IsSubtype(Py_TYPE(obj), KOS_CALLABLE_PYTYPE(KosTypeType)))

#define KosType_CheckExact(obj)                                      \
  ((obj) && PyType_Check(obj) &&                                     \
   (Py_TYPE(obj) == KOS_CALLABLE_PYTYPE(KosTypeType)))

/**
   True if obj should use the KosCallable install path (kos_callable_vcall etc).
   Includes both KosCallable types and KosType instances (Koskari classes).
   Workaround: PyType_IsSubtype(KosType, KosCallableType) may be false in some
   build/load contexts, so we explicitly allow KosType as well.
*/
#define KosCallableOrKosType_Check(obj)                              \
  (KosCallable_Check(obj) || KosType_Check(obj))


/**
   Return the default metaclass for Koskari-defined classes (KosType).
   Caller receives a new reference.
*/
PyObject *kos_get_default_metaclass(void);


/* === kos_construct === */


#define KOS_CONSTRUCT_MODE_ASYNC 0
#define KOS_CONSTRUCT_MODE_SYNC  1


/**
   Allocate an instance of cls and run __init__(self, *args, **kwargs).

   :param cls: type (KosType instance) to instantiate
   :param args: positional arguments for __init__
   :param kwargs: keyword arguments for __init__ (may be NULL)
   :param mode: KOS_CONSTRUCT_MODE_ASYNC (return awaitable) or
                KOS_CONSTRUCT_MODE_SYNC (block until done, return instance)

   :return: instance (mode SYNC) or awaitable yielding instance (mode ASYNC),
            or NULL on error
*/
PyObject *kos_construct(PyObject *cls, PyObject *args, PyObject *kwargs,
                       int mode);


/**
   Wrap (instance, init_coroutine) in an awaitable that drives the coroutine
   then yields the instance. Used when __init__ is async.

   :param instance: allocated instance (new ref, consumed)
   :param init_coroutine: result of calling async __init__ (new ref, consumed)
   :return: awaitable yielding instance, or NULL on error
*/
PyObject *kos_construct_awaitable_new(PyObject *instance,
                                      PyObject *init_coroutine);


/* === Unit lifecycle === */


int koskari_object_init(PyObject *mod);
int koskari_object_clear(PyObject *mod);


struct KosContext;
struct KosEvent;


/**
   Invoke a KosType (class) to construct an instance. Used when KosType
   is not recognized as KosCallable (e.g. tp_bases MRO not incorporated).
*/
int kostype_install(struct KosContext *ctx, struct KosEvent *out_ev,
                    PyObject *callable, PyObject *args_tuple,
                    PyObject *kwds_dict);


#endif


// The end.
