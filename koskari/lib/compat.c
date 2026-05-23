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


#if PY_MINOR_VERSION >= 13
/**
   Fetch value from StopIteration/StopAsyncIteration.  Used on 3.13+ when
   PyGen_FetchStopIterationValue is not exported.  Implementation uses
   PyErr_Fetch and extracts value from exception args or .value.
*/
static int fetch_stop_iteration_value(PyObject **ptr, PyObject *exc) {
  PyObject *val = NULL;
  PyObject *args = PyException_GetArgs(exc);
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
  return 0;
}
#endif

int koskari_FetchStopIterationValue(PyObject **ptr) {
#if PY_MINOR_VERSION >= 13
  PyObject *type = NULL;
  PyObject *value = NULL;
  PyObject *tb = NULL;
  PyErr_Fetch(&type, &value, &tb);
  Py_XDECREF(tb);
  if (! type) {
    *ptr = NULL;
    return -1;
  }
  int ok = 0;
  if (PyErr_GivenExceptionMatches(type, PyExc_StopIteration) ||
      PyErr_GivenExceptionMatches(type, PyExc_StopAsyncIteration))
    ok = fetch_stop_iteration_value(ptr, value ? value : type);
  else {
    PyErr_Restore(type, value, NULL);
    *ptr = NULL;
    return -1;
  }
  Py_DECREF(type);
  Py_XDECREF(value);
  return ok;
#else
  return _PyGen_FetchStopIterationValue(ptr);
#endif
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
  Py_XDECREF(tb);
  if (! type)
    return NULL;
  PyObject *exc = value ? value : PyObject_CallFunctionObjArgs(type, NULL);
  Py_DECREF(type);
  Py_XDECREF(value);
  return exc;
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


/**
   Return 1 if another thread requested this thread drop the GIL, else 0
 */
int koskari_gil_drop_requested(void) {
#if defined(Py_GIL_DISABLED) && Py_GIL_DISABLED
  return 0;
#else
  PyThreadState *tstate;
#endif

#if PY_MINOR_VERSION >= 13
  /*
     struct _ts through eval_breaker (GIL drop is bit 0):
     https://github.com/python/cpython/blob/v3.13.0/Include/cpython/pystate.h
     https://github.com/python/cpython/blob/v3.13.0/Include/internal/pycore_ceval.h
   */
  struct koskari_ts_313 {
    PyThreadState *prev;
    PyThreadState *next;
    PyInterpreterState *interp;
    uintptr_t eval_breaker;
  };

  tstate = PyThreadState_Get();
  if (! tstate)
    return 0;

  return (((struct koskari_ts_313 *) tstate)->eval_breaker
          & (uintptr_t) 1) != 0;

#elif PY_MINOR_VERSION == 12
  /*
     struct _ceval_state:
     https://github.com/python/cpython/blob/v3.12.0/Include/internal/pycore_ceval_state.h
     struct _is (gc_opaque matches struct _gc_runtime_state size):
     https://github.com/python/cpython/blob/v3.12.0/Include/internal/pycore_interp.h
     https://github.com/python/cpython/blob/v3.12.0/Include/internal/pycore_gc.h
   */
  struct koskari_ceval_312 {
    int eval_breaker;
    int gil_drop_request;
    int recursion_limit;
  };

  struct koskari_pythreads_312 {
    uint64_t next_unique_id;
    PyThreadState *head;
    long count;
    size_t stacksize;
  };

  struct koskari_is_312 {
    PyInterpreterState *next;
    int64_t id;
    int64_t id_refcount;
    int requires_idref;
    void *id_mutex;
    int _initialized;
    int finalizing;
    uint64_t monitoring_version;
    uint64_t last_restart_version;
    struct koskari_pythreads_312 threads;
    void *runtime;
    uintptr_t _finalizing;
    unsigned char gc_opaque[240];
    PyObject *sysdict;
    PyObject *builtins;
    struct koskari_ceval_312 ceval;
  };

  PyInterpreterState *interp;

  tstate = PyThreadState_Get();
  if (! tstate || ! tstate->interp)
    return 0;

  interp = tstate->interp;
  return ((struct koskari_is_312 *) interp)->ceval.gil_drop_request != 0;

#elif PY_MINOR_VERSION == 11
  /*
     struct _ceval_state and struct _is:
     https://github.com/python/cpython/blob/v3.11.0/Include/internal/pycore_interp.h
   */
  struct koskari_ceval_311 {
    int recursion_limit;
    int eval_breaker;
    int gil_drop_request;
  };

  struct koskari_pythreads_311 {
    uint64_t next_unique_id;
    PyThreadState *head;
    long count;
    size_t stacksize;
  };

  struct koskari_is_311 {
    PyInterpreterState *next;
    struct koskari_pythreads_311 threads;
    void *runtime;
    int64_t id;
    int64_t id_refcount;
    int requires_idref;
    void *id_mutex;
    int _initialized;
    int finalizing;
    _Bool _static;
    struct koskari_ceval_311 ceval;
  };

  PyInterpreterState *interp;

  tstate = PyThreadState_Get();
  if (! tstate || ! tstate->interp)
    return 0;

  interp = tstate->interp;
  return ((struct koskari_is_311 *) interp)->ceval.gil_drop_request != 0;

#elif PY_MINOR_VERSION == 10
  /*
     struct _ceval_state and struct _is:
     https://github.com/python/cpython/blob/v3.10.0/Include/internal/pycore_interp.h
   */
  struct koskari_ceval_310 {
    int recursion_limit;
    int eval_breaker;
    int gil_drop_request;
  };

  struct koskari_is_310 {
    PyInterpreterState *next;
    PyThreadState *tstate_head;
    void *runtime;
    int64_t id;
    int64_t id_refcount;
    int requires_idref;
    void *id_mutex;
    int finalizing;
    struct koskari_ceval_310 ceval;
  };

  PyInterpreterState *interp;

  tstate = PyThreadState_Get();
  if (! tstate || ! tstate->interp)
    return 0;

  interp = tstate->interp;
  return ((struct koskari_is_310 *) interp)->ceval.gil_drop_request != 0;

#elif PY_MINOR_VERSION == 9
  /*
     struct _ceval_state and struct _is:
     https://github.com/python/cpython/blob/v3.9.0/Include/internal/pycore_interp.h
   */
  struct koskari_ceval_39 {
    int recursion_limit;
    int eval_breaker;
    int gil_drop_request;
  };

  struct koskari_is_39 {
    PyInterpreterState *next;
    PyThreadState *tstate_head;
    void *runtime;
    int64_t id;
    int64_t id_refcount;
    int requires_idref;
    void *id_mutex;
    int finalizing;
    struct koskari_ceval_39 ceval;
  };

  PyInterpreterState *interp;

  tstate = PyThreadState_Get();
  if (! tstate || ! tstate->interp)
    return 0;

  interp = tstate->interp;
  return ((struct koskari_is_39 *) interp)->ceval.gil_drop_request != 0;

#else
  return 0;
#endif
}


// The end.
