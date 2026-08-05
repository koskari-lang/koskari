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


/* === Class builder === */


/**
   Build a class from a captured namespace. Demand-wraps the synchronous
   dunders (Procedure for __init__, staticmethod(Demand) for __new__,
   classmethod(Demand) for __init_subclass__ and friends), resolves the
   winning metaclass
   (plain type by default), and delegates construction to it. Installs
   the kos_instance_call sentinel when __call__ resolves to a
   KosCallable.

   :param name: class name (str)
   :param bases: base classes tuple
   :param namespace: mapping of class attributes
   :param metaclass_or_null: explicit metaclass or NULL
   :return: new class object
*/
PyObject *kos_build_class(
    PyObject *name,
    PyObject *bases,
    PyObject *namespace,
    PyObject *metaclass_or_null);


/* === instance __call__ inlining === */


/**
   tp_call shim for Koskari instances whose __call__ is a KosCallable. Doubles
   as the sentinel KosInstanceCall_Check tests for, so the evaluator can inline
   the call through the install slot rather than re-entering koskari_eval.
*/
PyObject *kos_instance_call(PyObject *self, PyObject *args, PyObject *kwds);


/**
   Resolve the unbound __call__ KosCallable of self's type for inline dispatch.
   Borrowed reference, or NULL. The caller injects self as the first positional.
*/
PyObject *kos_instance_call_target(PyObject *self);


/**
   True when obj is an instance whose type carries the kos_instance_call
   sentinel, i.e. its __call__ is a KosCallable the evaluator can inline.
   Cheap pointer compare; used to gate the (slightly costlier) target lookup.
*/
#define KosInstanceCall_Check(obj) \
  ((obj) && Py_TYPE(obj)->tp_call == kos_instance_call)


/* === Unit lifecycle === */


int koskari_object_init(PyObject *mod);
int koskari_object_clear(PyObject *mod);


#endif


// The end.
