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


#ifndef KOSKARI_COMPAT_H
#define KOSKARI_COMPAT_H


#include <Python.h>

#if PY_MINOR_VERSION == 14
#include "compat_314.h"

#elif PY_MINOR_VERSION == 13
#include "compat_313.h"

#elif PY_MINOR_VERSION == 12
#include "compat_312.h"

#elif PY_MINOR_VERSION == 11
#include "compat_311.h"

#elif PY_MINOR_VERSION == 10
#include "compat_310.h"

#elif PY_MINOR_VERSION == 9
#include "compat_39.h"

#elif PY_MINOR_VERSION == 8
#include "compat_38.h"

#elif PY_MINOR_VERSION == 7
#include "compat_37.h"

#elif PY_MINOR_VERSION == 6
#include "compat_36.h"

#endif


/* === macros for all versions === */

#if PY_MINOR_VERSION < 13
#define PyMapping_HasKeyWithError(o, k)  PyMapping_HasKey((o), (k))
#endif

/**
   Return the value associated with a StopAsyncIteration instance (new ref).
   On 3.13+ uses .value; on earlier versions uses exc.args[0].  Returns
   Py_None (new ref) when no value is present.  Caller must Py_DECREF the result.
*/
PyObject *koskari_StopAsyncIteration_value(PyObject *exc);
PyObject *koskari_StopAsyncIteration_FromValue(PyObject *payload);

#define StopAsyncIteration_VALUE(exc)  koskari_StopAsyncIteration_value(exc)

/* _PyType_Lookup remains private on all supported versions */
#define PyType_Lookup _PyType_Lookup


/* === compat unit lifecycle === */

int koskari_compat_init(const PyObject *mod);
int koskari_compat_clear(PyObject *mod);


/* Our wrapper when the C API symbol is not exported (e.g. 3.12) */
PyObject *PyObject_GetAwaitable(PyObject *o);


/**
   Extend list with items from iterable.  Returns 0 on success, -1 on error.
   Replaces _PyList_Extend (removed in 3.13+).
*/
int koskari_List_Extend(PyObject *list, PyObject *iterable);


/**
   Return dict.items() view (new ref).  Replaces _PyDictView_New(,,PyDictItems_Type).
*/
PyObject *koskari_DictView_Items(PyObject *dict);

/**
   Return dict.keys() view (new ref).  Replaces _PyDictView_New(,,PyDictKeys_Type).
*/
PyObject *koskari_DictView_Keys(PyObject *dict);


/**
   Create a dict presized for minused entries (new ref).  Replaces
   PyDict_NewPresized when the symbol is not exported (e.g. 3.14).
*/
PyObject *koskari_Dict_NewPresized(Py_ssize_t minused);


/**
   Fetch value from current StopIteration/StopAsyncIteration exception.
   Returns 0 on success, -1 on error.  Clears the exception.  On success
   sets *ptr to the value (new ref) or Py_None.  Replaces
   PyGen_FetchStopIterationValue when the symbol is not exported (e.g. 3.14).
*/
int koskari_FetchStopIterationValue(PyObject **ptr);


/**
   Fetch and clear the current exception, return the exception instance
   (new ref).  Replaces PyErr_GetRaisedException (added in 3.12).
*/
PyObject *koskari_Err_GetRaisedException(void);


/**
   Set exception from type and message string.  On Python 3.10, wraps the
   string in exc_type(msg) so PyErr_Fetch returns the instance.  Shadowed
   via macro in compat_310.h.
*/
void koskari_Err_SetString(PyObject *exc_type, const char *message);


/**
   Set exception from type and format string.  On Python 3.10, uses
   PyUnicode_FromFormatV to create the exception value.  Shadowed via
   macro in compat_310.h.
*/
void koskari_Err_Format(PyObject *exc_type, const char *format, ...);


/**
   Set exception from type and value.  On Python 3.10, when value is a
   string or other non-instance, wraps it in exc_type(value) before
   calling PyErr_SetObject.  On 3.11+, delegates to PyErr_SetObject
   directly.  Use when value may be a plain string (e.g. KeyError key).
*/
void koskari_Err_SetObject(PyObject *exc_type, PyObject *value);


#endif


// The end.
