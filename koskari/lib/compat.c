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


#include <Python.h>

#define STR_CONST(which, val) {                 \
  if (! (which))                              \
    (which) = PyUnicode_FromString(val);      \
}


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
#if PY_MINOR_VERSION >= 12
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
   Extend list with items from iterable.  Returns 0 on success, -1 on error.
   Uses PyList_Extend on 3.13+, _PyList_Extend on earlier versions.
*/
int koskari_List_Extend(PyObject *list, PyObject *iterable) {
#if PY_MINOR_VERSION >= 13
  return PyList_Extend(list, iterable);
#else
  PyObject *tmp = _PyList_Extend((PyListObject *) list, iterable);
  if (! tmp)
    return -1;
  Py_DECREF(tmp);
  return 0;
#endif
}


#if PY_MINOR_VERSION >= 13
/**
   Extend list with items from iterable.  Returns list (new ref) on success,
   NULL on error.  Replaces _PyList_Extend for code that expects the old
   return-semantics (list pointer).  Used via _PyList_Extend macro in compat_313/314.h.
*/
PyObject *koskari_List_ExtendRet(PyObject *list, PyObject *iterable) {
  if (PyList_Extend(list, iterable) < 0)
    return NULL;
  Py_INCREF(list);
  return list;
}
#endif


int koskari_FetchStopIterationValue(PyObject **ptr) {
#if PY_MINOR_VERSION >= 13
  PyObject *type = NULL;
  PyObject *value = NULL;
  PyObject *tb = NULL;
  PyObject *exc = NULL;
  PyObject *val = NULL;
  PyObject *args = NULL;

  PyErr_Fetch(&type, &value, &tb);
  Py_XDECREF(tb);
  if (! type) {
    *ptr = NULL;
    return -1;
  }
  if (! (PyErr_GivenExceptionMatches(type, PyExc_StopIteration) ||
         PyErr_GivenExceptionMatches(type, PyExc_StopAsyncIteration))) {
    PyErr_Restore(type, value, NULL);
    *ptr = NULL;
    return -1;
  }

  exc = value ? value : type;
  args = PyException_GetArgs(exc);
  if (args && PyTuple_Check(args) && PyTuple_GET_SIZE(args) > 0) {
    val = PyTuple_GET_ITEM(args, 0);
    Py_INCREF(val);
    Py_DECREF(args);
  } else {
    Py_XDECREF(args);
    val = PyObject_GetAttrString(exc, "value");
    if (! val)
      PyErr_Clear();
    if (! val) {
      Py_INCREF(Py_None);
      val = Py_None;
    }
  }

  *ptr = val;
  Py_DECREF(type);
  Py_XDECREF(value);
  return 0;
#else
  return _PyGen_FetchStopIterationValue(ptr);
#endif
}


/**
   True when two str objects are equal, 0 otherwise: identity first, then
   PyUnicode_Compare.  Both operands must be str; there is no type check.
   Stands in for PyUnicode_Equal before 3.14.
*/
int koskari_Unicode_Equal(PyObject *a, PyObject *b) {
  return a == b || PyUnicode_Compare(a, b) == 0;
}


/**
   Fetch and clear the current exception, return the exception instance
   (new ref).  Uses PyErr_GetRaisedException on 3.12+, PyErr_Fetch on older.
*/
PyObject *koskari_Err_GetRaisedException(void) {
#if PY_MINOR_VERSION >= 12
  return PyErr_GetRaisedException();
#else
  PyObject *type = NULL;
  PyObject *value = NULL;
  PyObject *tb = NULL;
  PyErr_Fetch(&type, &value, &tb);
  if (! type)
    return NULL;
  PyErr_NormalizeException(&type, &value, &tb);
  if (tb)
    PyException_SetTraceback(value, tb);
  Py_DECREF(type);
  Py_XDECREF(tb);
  return value;
#endif
}


/**
   Create a dict presized for minused entries.  Uses _PyDict_NewPresized on
   older versions; PyDict_New() on 3.14 when PyDict_NewPresized is not exported.
*/
PyObject *koskari_Dict_NewPresized(Py_ssize_t minused) {
#if PY_MINOR_VERSION >= 14
  (void) minused;
  return PyDict_New();
#else
  return _PyDict_NewPresized(minused);
#endif
}


/**
   Return dict.items() view.  Uses PyObject_CallMethod on 3.13+ (replaces
   _PyDictView_New removed in 3.13).
*/
PyObject *koskari_DictView_Items(PyObject *dict) {
#if PY_MINOR_VERSION >= 13
  return PyObject_CallMethod(dict, "items", NULL);
#else
  return _PyDictView_New(dict, &PyDictItems_Type);
#endif
}


PyObject *koskari_DictView_Keys(PyObject *dict) {
#if PY_MINOR_VERSION >= 13
  return PyObject_CallMethod(dict, "keys", NULL);
#else
  return _PyDictView_New(dict, &PyDictKeys_Type);
#endif
}


#if PY_MINOR_VERSION >= 13
/**
   Replace _PyDictView_New (removed in 3.13).  Dispatches to items() or keys()
   based on type.  Used via macro in compat_313/314.h.
*/
PyObject *koskari_DictView_New(PyObject *dict, PyTypeObject *type) {
  if (type == &PyDictItems_Type)
    return koskari_DictView_Items(dict);
  if (type == &PyDictKeys_Type)
    return koskari_DictView_Keys(dict);
  PyErr_SetString(PyExc_TypeError, "unsupported dict view type");
  return NULL;
}
#endif


/**
   The iterator that awaiting o steps: an exact native coroutine is its
   own iterator, as CPython's await takes it, with am_send and no
   wrapper; else the type's am_await slot when it has one (every class
   defining __await__ does), else __await__ looked up on the instance
   and called. Caller must Py_DECREF the result.
*/
PyObject *kos_get_awaitable(PyObject *o) {
  PyTypeObject *tp = Py_TYPE(o);
  if (PyCoro_CheckExact(o))
    return Py_NewRef(o);
  if (tp->tp_as_async && tp->tp_as_async->am_await)
    return tp->tp_as_async->am_await(o);

  PyObject *meth = PyObject_GetAttr(o, _str_await);
  if (! meth)
    return NULL;
  PyObject *res = PyObject_CallNoArgs(meth);
  Py_DECREF(meth);
  return res;
}


#if PY_MINOR_VERSION >= 13
/**
   Replace _PyErr_FormatFromCause (moved to Python's internal-only header
   in 3.13+).  Fetches the current exception, formats a new one, chains
   the old exception as __cause__ and __context__, and restores the new
   one as current.  Used via macro in compat_313/314.h.
*/
PyObject *koskari_Err_FormatFromCause(PyObject *exception,
                                       const char *format, ...) {
  PyObject *exc = koskari_Err_GetRaisedException();

  va_list vargs;
  va_start(vargs, format);
  PyErr_FormatV(exception, format, vargs);
  va_end(vargs);

  PyObject *exc2 = koskari_Err_GetRaisedException();
  PyException_SetCause(exc2, Py_NewRef(exc));
  PyException_SetContext(exc2, Py_NewRef(exc));
  Py_DECREF(exc);
  PyErr_SetRaisedException(exc2);
  return NULL;
}
#endif


/* === unit lifecycle === */


int koskari_compat_init(const PyObject *mod) {

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
