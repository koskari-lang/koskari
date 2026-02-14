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

#if PY_MINOR_VERSION == 11
#include "compat_311.h"

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

/**
   Return the value associated with a StopAsyncIteration instance (new ref).
   On 3.13+ uses .value; on earlier versions uses exc.args[0].  Returns
   Py_None (new ref) when no value is present.  Caller must Py_DECREF the result.
*/
PyObject *koskari_StopAsyncIteration_value(PyObject *exc);

#define StopAsyncIteration_VALUE(exc)  koskari_StopAsyncIteration_value(exc)

#define PyDict_NewPresized _PyDict_NewPresized
#define PyGen_FetchStopIterationValue _PyGen_FetchStopIterationValue
#define PyObject_LookupAttr _PyObject_LookupAttr
#define PyType_Lookup _PyType_Lookup


/* === compat unit lifecycle === */

int koskari_compat_init(PyObject *mod);
int koskari_compat_clear(PyObject *mod);


/* Our wrapper when the C API symbol is not exported (e.g. 3.12) */
PyObject *PyObject_GetAwaitable(PyObject *o);


#endif


// The end.
