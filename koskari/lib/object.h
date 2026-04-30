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


/* === KosType === */


extern PyTypeObject KosTypeType;


#define KosType_Check(obj)                                           \
  ((obj) && PyType_Check(obj) &&                                     \
   PyType_IsSubtype(Py_TYPE(obj), &KosTypeType))

#define KosType_CheckExact(obj)                                      \
  ((obj) && PyType_Check(obj) &&                                     \
   (Py_TYPE(obj) == &KosTypeType))


/**
   Return the default metaclass for Koskari-defined classes (KosType).
   Caller receives a new reference.
*/
PyObject *kos_get_default_metaclass(void);

/**
   Build a class using Koskari metaclass composition semantics.

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


/* === Unit lifecycle === */


int koskari_object_init(PyObject *mod);
int koskari_object_clear(PyObject *mod);


#endif


// The end.
