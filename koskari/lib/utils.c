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


#include "lib.h"


/* === Constantly callable type === */


typedef struct {
  PyObject_HEAD
  PyObject *value;
} KosConstantly;


static void constantly_dealloc(PyObject *self) {

  Py_CLEAR(((KosConstantly *) self)->value);
  Py_TYPE(self)->tp_free(self);
}


static PyObject *constantly_call(PyObject *self,
                                 PyObject *args, PyObject *kwds) {

  PyObject *value = ((KosConstantly *) self)->value;
  Py_INCREF(value);
  return value;
}


static PyObject *constantly_repr(PyObject *self) {

  PyObject *value = ((KosConstantly *) self)->value;
  return PyUnicode_FromFormat("constantly(%R)", value);
}


static PyTypeObject KosConstantlyType = {
  PyVarObject_HEAD_INIT(NULL, 0)

  .tp_name = "koskari.lib.constantly",
  .tp_basicsize = sizeof(KosConstantly),
  .tp_dealloc = constantly_dealloc,
  .tp_call = constantly_call,
  .tp_repr = constantly_repr,
  .tp_flags = Py_TPFLAGS_DEFAULT,
  .tp_doc = "A callable that always returns the same value",
};


/* === utility function implementations === */


PyObject *KosUtils_Reapply(PyObject *func, PyObject *data, long count) {

  PyObject *result = data;
  Py_INCREF(result);

  while (count-- > 0) {
    PyObject *next = PyObject_CallFunctionObjArgs(func, result, NULL);
    Py_DECREF(result);

    if (! next)
      return NULL;

    result = next;
  }

  return result;
}


PyObject *KosUtils_Apply(PyObject *func, PyObject *args, PyObject *kwargs) {

  PyObject *seq_args = NULL;
  PyObject *result = NULL;

  if (! func) {
    PyErr_SetString(PyExc_TypeError, "apply requires a callable");
    return NULL;
  }

  /* Handle args - convert pair to tuple if needed */
  if (! args || args == Py_None) {
    seq_args = PyTuple_New(0);

  } else if (KosPair_Check(args)) {
    /* Unpack pair to iterator, then convert to tuple */
    PyObject *iter = KosPair_Unpack(args);
    if (! iter)
      return NULL;
    seq_args = PySequence_Tuple(iter);
    Py_DECREF(iter);

  } else if (PyTuple_Check(args)) {
    seq_args = args;
    Py_INCREF(seq_args);

  } else if (PyList_Check(args)) {
    seq_args = PyList_AsTuple(args);

  } else {
    /* Try to convert arbitrary iterable to tuple */
    seq_args = PySequence_Tuple(args);
  }

  if (! seq_args)
    return NULL;

  /* Handle kwargs */
  if (kwargs && kwargs != Py_None && ! PyDict_Check(kwargs)) {
    PyErr_SetString(PyExc_TypeError,
                    "apply kwargs must be a dict or None");
    Py_DECREF(seq_args);
    return NULL;
  }

  if (kwargs == Py_None)
    kwargs = NULL;

  result = PyObject_Call(func, seq_args, kwargs);
  Py_DECREF(seq_args);

  return result;
}


PyObject *KosUtils_Identity(PyObject *value) {

  Py_INCREF(value);
  return value;
}


PyObject *KosUtils_Constantly(PyObject *value) {

  KosConstantly *obj = PyObject_New(KosConstantly, &KosConstantlyType);
  if (! obj)
    return NULL;

  Py_INCREF(value);
  obj->value = value;

  return (PyObject *) obj;
}


PyObject *KosUtils_BuildTuple(PyObject *const *args, Py_ssize_t nargs) {

  PyObject *result = PyTuple_New(nargs);
  if (! result)
    return NULL;

  for (Py_ssize_t i = 0; i < nargs; i++) {
    PyObject *item = args[i];
    Py_INCREF(item);
    PyTuple_SET_ITEM(result, i, item);
  }

  return result;
}


PyObject *KosUtils_BuildList(PyObject *const *args, Py_ssize_t nargs) {

  PyObject *result = PyList_New(nargs);
  if (! result)
    return NULL;

  for (Py_ssize_t i = 0; i < nargs; i++) {
    PyObject *item = args[i];
    Py_INCREF(item);
    PyList_SET_ITEM(result, i, item);
  }

  return result;
}


PyObject *KosUtils_BuildSet(PyObject *const *args, Py_ssize_t nargs) {

  PyObject *result = PySet_New(NULL);
  if (! result)
    return NULL;

  for (Py_ssize_t i = 0; i < nargs; i++) {
    if (PySet_Add(result, args[i]) < 0) {
      Py_DECREF(result);
      return NULL;
    }
  }

  return result;
}


PyObject *KosUtils_BuildDict(PyObject *args, PyObject *kwargs) {

  PyObject *result = PyDict_New();
  if (! result)
    return NULL;

  Py_ssize_t count = args ? PyTuple_GET_SIZE(args) : 0;

  /* Process positional arguments as (key, value) pairs */
  for (Py_ssize_t i = 0; i < count; i++) {
    PyObject *item = PyTuple_GET_ITEM(args, i);
    PyObject *key = NULL;
    PyObject *value = NULL;

    /* Handle pair type specially */
    if (KosPair_Check(item)) {
      /* Extract car as key, cadr as value */
      key = KosPair_CAR(item);
      PyObject *tail = KosPair_CDR(item);

      if (KosPair_Check(tail)) {
        value = KosPair_CAR(tail);
      } else if (KosNil_Check(tail)) {
        PyErr_SetString(PyExc_ValueError,
                        "build_dict pair must have at least 2 elements");
        Py_DECREF(result);
        return NULL;
      } else {
        /* improper pair - tail is the value */
        value = tail;
      }

      Py_INCREF(key);
      Py_INCREF(value);

    } else {
      /* Handle as sequence */
      if (! PySequence_Check(item) || PySequence_Size(item) != 2) {
        PyErr_SetString(PyExc_ValueError,
                        "build_dict items must be (key, value) pairs");
        Py_DECREF(result);
        return NULL;
      }

      key = PySequence_GetItem(item, 0);
      value = PySequence_GetItem(item, 1);

      if (! key || ! value) {
        Py_XDECREF(key);
        Py_XDECREF(value);
        Py_DECREF(result);
        return NULL;
      }
    }

    int err = PyDict_SetItem(result, key, value);
    Py_DECREF(key);
    Py_DECREF(value);

    if (err < 0) {
      Py_DECREF(result);
      return NULL;
    }
  }

  /* Merge keyword arguments */
  if (kwargs && PyDict_Size(kwargs) > 0) {
    if (PyDict_Merge(result, kwargs, 1) < 0) {
      Py_DECREF(result);
      return NULL;
    }
  }

  return result;
}


/* === module method wrappers === */


static PyObject *m_reapply(PyObject *mod,
                           PyObject *args, PyObject *kwds) {

  PyObject *func = NULL;
  PyObject *data = NULL;
  long count = 0;

  static char *keywords[] = { "func", "data", "count", NULL };

  if (! PyArg_ParseTupleAndKeywords(args, kwds, "OOl", keywords,
                                    &func, &data, &count))
    return NULL;

  return KosUtils_Reapply(func, data, count);
}


static PyObject *m_apply(PyObject *mod,
                         PyObject *args, PyObject *kwds) {

  PyObject *func = NULL;
  PyObject *call_args = NULL;
  PyObject *call_kwargs = NULL;

  static char *keywords[] = { "func", "args", "kwargs", NULL };

  if (! PyArg_ParseTupleAndKeywords(args, kwds, "O|OO", keywords,
                                    &func, &call_args, &call_kwargs))
    return NULL;

  return KosUtils_Apply(func, call_args, call_kwargs);
}


static PyObject *m_identity(PyObject *mod, PyObject *value) {

  return KosUtils_Identity(value);
}


static PyObject *m_constantly(PyObject *mod, PyObject *value) {

  return KosUtils_Constantly(value);
}


static PyObject *m_pass(PyObject *mod,
                        PyObject *args, PyObject *kwds) {

  Py_RETURN_NONE;
}


#if PY_MINOR_VERSION < 6
#define FASTCALL_METHOD_TYPE METH_VARARGS
#define FASTCALL_WRAPPER(name)                                          \
  static PyObject *m_ ## name(PyObject *mod, PyObject *args) {          \
    return KosUtils_ ## name(PySequence_Fast_ITEMS(args),               \
                             PySequence_Fast_GET_SIZE(args));           \
  }
#else
#define FASTCALL_METHOD_TYPE METH_FASTCALL
#define FASTCALL_WRAPPER(name)                                          \
  static PyObject *m_ ## name(PyObject *mod,                            \
                              PyObject *const *args,                    \
                              Py_ssize_t nargs) {                       \
    return KosUtils_ ## name(args, nargs);                              \
  }
#endif


FASTCALL_WRAPPER(BuildTuple);
FASTCALL_WRAPPER(BuildList);
FASTCALL_WRAPPER(BuildSet);


static PyObject *m_build_dict(PyObject *mod,
                              PyObject *args, PyObject *kwds) {

  return KosUtils_BuildDict(args, kwds);
}


static PyMethodDef utils_methods[] = {
  { "reapply", (PyCFunction) m_reapply, METH_VARARGS | METH_KEYWORDS,
    "reapply(func, data, count) -> result\n"
    "Calls data = func(data) count times, returns final result." },

  { "apply", (PyCFunction) m_apply, METH_VARARGS | METH_KEYWORDS,
    "apply(func, args=(), kwargs=None) -> result\n"
    "Call func with args and kwargs. Args may be a pair, tuple, or list." },

  { "identity", (PyCFunction) m_identity, METH_O,
    "identity(value) -> value\n"
    "Returns its argument unchanged." },

  { "constantly", (PyCFunction) m_constantly, METH_O,
    "constantly(value) -> callable\n"
    "Returns a callable that always returns value." },

  { "_pass", (PyCFunction) m_pass, METH_VARARGS | METH_KEYWORDS,
    "_pass(*args, **kwargs) -> None\n"
    "Accepts any arguments, returns None." },

  { "build_tuple", (PyCFunction) m_BuildTuple, FASTCALL_METHOD_TYPE,
    "build_tuple(*args) -> tuple\n"
    "Build a tuple from arguments." },

  { "build_list", (PyCFunction) m_BuildList, FASTCALL_METHOD_TYPE,
    "build_list(*args) -> list\n"
    "Build a list from arguments." },

  { "build_set", (PyCFunction) m_BuildSet, FASTCALL_METHOD_TYPE,
    "build_set(*args) -> set\n"
    "Build a set from arguments." },

  { "build_dict", (PyCFunction) m_build_dict, METH_VARARGS | METH_KEYWORDS,
    "build_dict(*pairs, **kwargs) -> dict\n"
    "Build a dict from (key, value) pairs and keyword arguments." },

  { NULL, },
};


/* === unit lifecycle === */


int koskari_utils_init(PyObject *mod) {

  if (! mod)
    return -1;

  if (PyType_Ready(&KosConstantlyType) < 0)
    return -1;

  for (PyMethodDef *mdef = utils_methods; mdef->ml_name; mdef++) {
    PyObject *func = PyCFunction_New(mdef, mod);
    if (! func)
      return -1;

    int res = PyObject_SetAttrString(mod, mdef->ml_name, func);
    Py_DECREF(func);

    if (res < 0)
      return -1;
  }

  return 0;
}


int koskari_utils_clear(PyObject *mod) {

  return 0;
}


// The end.
