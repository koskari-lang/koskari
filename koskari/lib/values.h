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


#ifndef KOSKARI_VALUES_H
#define KOSKARI_VALUES_H


#include <Python.h>


/* === values structure === */


typedef struct KosValues {
  PyObject_HEAD

  PyObject *args;
  PyObject *kwds;
  Py_uhash_t hashed;
} KosValues;


#define KosValues_ARGS(v)   (((KosValues *)(v))->args)
#define KosValues_KWDS(v)   (((KosValues *)(v))->kwds)
#define KosValues_HASHED(v) (((KosValues *)(v))->hashed)


/* === type objects === */


extern PyTypeObject KosValuesType;


/* === type check macros === */


#define KosValues_Check(obj)                                    \
  ((obj) && PyType_IsSubtype((obj)->ob_type, &KosValuesType))

#define KosValues_CheckExact(obj)               \
  ((obj) && ((obj)->ob_type == &KosValuesType))


/* === public API === */


PyObject *KosValues_New(PyObject *args, PyObject *kwds);


/* === unit lifecycle === */


int koskari_values_init(PyObject *mod);
int koskari_values_clear(PyObject *mod);


#endif


// The end.
