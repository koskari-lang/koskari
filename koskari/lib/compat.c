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


/**
   Python version compatibility layer

   This module provides compatibility shims for different Python
   versions. Functions and macros are added here as needed by
   implementation units.
*/


#include "lib.h"


/* === string constants === */


static PyObject *_str_send = NULL;
static PyObject *_str_await = NULL;


/* === compatibility functions === */


/**
   Return the value associated with a StopAsyncIteration instance (new ref).
   Tries args[0] first (works for all versions when value was passed), then
   .value as fallback on 3.13+ if args is empty.  Returns Py_None (new ref)
   when no value is present.  Caller must Py_DECREF the result.
*/
PyObject *koskari_StopAsyncIteration_value(PyObject *exc) {
  if (! exc) {
    Py_INCREF(Py_None);
    return Py_None;
  }
  PyObject *args = NULL;
#if PY_MINOR_VERSION >= 9
  args = PyException_GetArgs(exc);
#else
  args = PyObject_GetAttrString(exc, "args");
#endif
  if (args && PyTuple_Check(args) && PyTuple_GET_SIZE(args) > 0) {
    PyObject *v = PyTuple_GET_ITEM(args, 0);
    Py_INCREF(v);
    Py_DECREF(args);
    return v;
  }
  Py_XDECREF(args);
#if PY_MINOR_VERSION >= 13
  {
    PyObject *v = PyObject_GetAttrString(exc, "value");
    if (v)
      return v;
    PyErr_Clear();
  }
#endif
  Py_INCREF(Py_None);
  return Py_None;
}


/**
   Create a StopAsyncIteration instance with the given payload (new ref).
   payload may be NULL; treated as Py_None.  On Python < 3.13, sets .value
   on the instance so runtimes that expect exception.value work.  Caller
   must Py_DECREF the result.
*/
PyObject *koskari_StopAsyncIteration_FromValue(PyObject *payload) {
  PyObject *exc = PyObject_CallFunctionObjArgs(PyExc_StopAsyncIteration,
                                               payload ? payload : Py_None,
                                               NULL);
  if (! exc)
    return NULL;
#if PY_MINOR_VERSION < 13
  if (payload && PyObject_SetAttrString(exc, "value", payload) < 0) {
    Py_DECREF(exc);
    return NULL;
  }
#endif
  return exc;
}


/**
   Return an awaitable (iterator) for o: if o has __await__, call it and
   return the result; otherwise return NULL. Caller must Py_DECREF the result.
*/
PyObject *PyObject_GetAwaitable(PyObject *o) {
  PyObject *meth = PyObject_GetAttr(o, _str_await);
  if (! meth)
    return NULL;
  PyObject *res = PyObject_CallNoArgs(meth);
  Py_DECREF(meth);
  return res;
}


/* === unit lifecycle === */


int koskari_compat_init(PyObject *mod) {

  STR_CONST(_str_send, "send");
  STR_CONST(_str_await, "__await__");

  return 0;
}


int koskari_compat_clear(PyObject *mod) {

  Py_CLEAR(_str_send);
  Py_CLEAR(_str_await);

  return 0;
}


// The end.
