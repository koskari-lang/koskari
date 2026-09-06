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
#include "hints.h"


/* type -> handler for KosUtils_Repr (see KosUtils_ReprRegister) */
static PyObject *kos_utils_repr_registry = NULL;


/* double-quote escape for KosUtils_Repr on plain str (see pair.c sib_quoted) */
static PyObject *_str_dq = NULL;
static PyObject *_str_esc_dq = NULL;
static PyObject *_str_kos_repr = NULL;

static PyObject *_str_colon_space = NULL;
static PyObject *_str_empty = NULL;
static PyObject *_str_hash_dict_cycle = NULL;
static PyObject *_str_hash_dict_empty = NULL;
static PyObject *_str_hash_dict_open = NULL;
static PyObject *_str_hash_list_cycle = NULL;
static PyObject *_str_hash_list_empty = NULL;
static PyObject *_str_hash_list_open = NULL;
static PyObject *_str_hash_tuple_cycle = NULL;
static PyObject *_str_hash_tuple_empty = NULL;
static PyObject *_str_hash_tuple_open = NULL;
static PyObject *_str_dict_close = NULL;
static PyObject *_str_list_close = NULL;
static PyObject *_str_space = NULL;
static PyObject *_str_tuple_close = NULL;


static PyObject *kos_utils_unicode_kos_repr(PyObject *obj) {

  PyObject *escaped = NULL;
  PyObject *result = NULL;

  escaped = PyUnicode_Replace(obj, _str_dq, _str_esc_dq, -1);
  if (! escaped)
    return NULL;

  result = PyUnicode_FromFormat("\"%U\"", escaped);
  Py_DECREF(escaped);
  return result;
}


/* === Constantly callable type === */


typedef struct {
  PyObject_HEAD
  PyObject *value;
} KosConstantly;


static void constantly_dealloc(PyObject *self) {

  Py_CLEAR(((KosConstantly *) self)->value);
  Py_TYPE(self)->tp_free(self);
}


static PyObject *constantly_call(
    PyObject *self,
    PyObject *args,
    PyObject *kwds) {

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


/* === KosApply install slots === */


static int kos_apply_build_args_tuple(
    PyObject *arglist,
    PyObject **out_args) {

  if (! arglist || arglist == Py_None) {
    *out_args = PyTuple_New(0);

  } else if (KosPair_Check(arglist)) {
    PyObject *iter = KosPair_Unpack(arglist);
    if (! iter)
      return -1;

    *out_args = PySequence_Tuple(iter);
    Py_DECREF(iter);

  } else if (PyTuple_Check(arglist)) {
    *out_args = arglist;
    Py_INCREF(*out_args);

  } else if (PyList_Check(arglist)) {
    *out_args = PyList_AsTuple(arglist);

  } else {
    *out_args = PySequence_Tuple(arglist);
  }

  return *out_args ? 0 : -1;
}


/**
   Shared apply install: fn/arglist/kwds already extracted (borrowed).
   apply wants no frame of its own (see KosApplyType) -- fn's own Call
   handles frame prep and tail reuse, and delivers straight to the
   real caller, since apply never occupies anything in between.
*/
static KosOpResult kos_apply_install_core(
    KosEvalState *es,
    PyObject *fn,
    PyObject *arglist,
    PyObject *kwds,
    int tail_call) {

  PyObject *seq_args = NULL;
  KosOpResult r;

  if (kos_apply_build_args_tuple(arglist, &seq_args) < 0)
    return EVAL_RAISE();

  if (kwds == Py_None)
    kwds = NULL;

  if (kwds && ! PyDict_Check(kwds)) {
    PyErr_SetString(PyExc_TypeError, "apply kwargs must be a dict or None");
    Py_DECREF(seq_args);
    return EVAL_RAISE();
  }

  r = KosEvalState_CallArgs(es, fn, seq_args, kwds, tail_call);
  Py_DECREF(seq_args);
  return r;
}


static KosOpResult kos_vinstall_apply(
    KosEvalState *es,
    PyObject *callable,
    PyObject *const *args,
    Py_ssize_t nargs,
    PyObject *kwnames,
    int tail_call) {

  (void) callable;
  (void) kwnames;  /* mirrored from the tuple install, which ignores kwds */
  if (nargs < 1 || nargs > 3) {
    PyErr_SetString(PyExc_TypeError, "apply expects 1, 2, or 3 arguments");
    return EVAL_RAISE();
  }

  return kos_apply_install_core(
      es,
      args[0],
      (nargs >= 2) ? args[1] : NULL,
      (nargs >= 3) ? args[2] : NULL,
      tail_call);
}


static PyObject *apply_call(
    PyObject *self,
    PyObject *args,
    PyObject *kwargs) {

  (void) self;

  PyObject *func = NULL;
  PyObject *call_args = NULL;
  PyObject *call_kwargs = NULL;

  static char *keywords[] = { "func", "args", "kwargs", NULL };

  if (! PyArg_ParseTupleAndKeywords(args, kwargs, "O|OO", keywords,
                                    &func, &call_args, &call_kwargs))
    return NULL;

  return KosUtils_Apply(func, call_args, call_kwargs);
}


static PyObject *apply_repr(PyObject *self) {

  (void) self;
  return PyUnicode_FromString("<koskari apply>");
}


static KosEvaluatedTypeObject KosApplyType = {
  .type = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "koskari.lib.apply",
    .tp_basicsize = sizeof(PyObject),
    .tp_dealloc = (destructor) PyObject_Del,
    .tp_repr = apply_repr,
    .tp_call = KosCallAdapt_CallAsync,
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_doc = "Callable helper implementing apply(func, args=(), kwargs=None)",
    .tp_new = PyType_GenericNew,
    .tp_base = KOS_EVALUATED_PYTYPE(KosEvaluatedType),
  },
  .kos_call = {
    .kc_vinstall = kos_vinstall_apply,
    .kc_wants_frame = 0,
    .kc_wants_frame_fn = NULL,
    .kc_pycall = apply_call,
    .kc_pyvectorcall = NULL,
  },
};


static PyObject *KosApplyObject = NULL;


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


int KosUtils_NormalizeThrow(
    PyObject *typ,
    PyObject *val,
    PyObject *tb,
    PyObject **typ_out,
    PyObject **exc_out) {

  if (PyExceptionClass_Check(typ)) {
    Py_INCREF(typ);
    Py_XINCREF(val);
    Py_XINCREF(tb);
    PyErr_NormalizeException(&typ, &val, &tb);
    Py_XDECREF(tb);
    if (! typ)
      return -1;
    if (! val) {
      val = typ;
      Py_INCREF(val);
    }
    *typ_out = typ;
    *exc_out = val;
    return 0;
  }

  if (PyExceptionInstance_Check(typ)) {
    if (val && val != Py_None) {
      PyErr_SetString(PyExc_TypeError,
                      "instance exception may not have a separate value");
      return -1;
    }
    Py_INCREF(typ);
    *exc_out = typ;
    *typ_out = (PyObject *) Py_TYPE(typ);
    Py_INCREF(*typ_out);
    return 0;
  }

  PyErr_SetString(PyExc_TypeError,
                  "exceptions must be classes or instances deriving "
                  "from BaseException");
  return -1;
}


int KosUtils_NormalizeThrowArgs(
    PyObject *args,
    const char *format,
    PyObject **typ_out,
    PyObject **exc_out) {

  PyObject *typ = NULL, *val = NULL, *tb = NULL;

  if (! PyArg_ParseTuple(args, format, &typ, &val, &tb))
    return -1;

  return KosUtils_NormalizeThrow(typ, val, tb, typ_out, exc_out);
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


int KosUtils_ReprRegister(PyObject *type, PyObject *handler) {

  if (! PyType_Check(type)) {
    PyErr_SetString(PyExc_TypeError,
                    "kos_repr_register: first argument must be a type");
    return -1;
  }

  if (! (PyCallable_Check(handler) || KosCallable_Check(handler))) {
    PyErr_SetString(PyExc_TypeError,
                    "kos_repr_register: second argument must be a callable");
    return -1;
  }

  // we might be called before the module is initialized, so let's allow
  // pre-creating the registry if it doesn't exist yet.
  if (! kos_utils_repr_registry) {
    kos_utils_repr_registry = PyDict_New();
    if (! kos_utils_repr_registry)
      return -1;
  }

  if (PyDict_SetItem(kos_utils_repr_registry, type, handler) < 0)
    return -1;

  return 0;
}


int KosUtils_ReprRegisterBuiltin(PyObject *type) {

  // we have to potentially handle this being called before the module is
  // initialized, so let's just verify the STR_CONST exists
  STR_CONST(_str_kos_repr, "__kos_repr__");

  PyObject *handler = PyObject_GetAttr(type, _str_kos_repr);
  if (! handler) {
    return -1;
  }

  int res = KosUtils_ReprRegister(type, handler);
  Py_DECREF(handler);
  return res;
}


int KosUtils_ReprUnregister(PyObject *type) {

  if (! PyType_Check(type)) {
    PyErr_SetString(PyExc_TypeError,
                    "kos_repr_unregister: argument must be a type");
    return -1;
  }

  if (! kos_utils_repr_registry)
    return 0;

  if (PyDict_DelItem(kos_utils_repr_registry, type) < 0) {
    if (PyErr_ExceptionMatches(PyExc_KeyError)) {
      PyErr_Clear();
      return 0;
    }
    return -1;
  }

  return 0;
}


void KosUtils_ReprRegistryClear(void) {

  if (kos_utils_repr_registry)
    PyDict_Clear(kos_utils_repr_registry);
}


PyObject *KosUtils_ReprRegistrySnapshot(void) {

  if (! kos_utils_repr_registry)
    return PyDict_New();

  return PyDict_Copy(kos_utils_repr_registry);
}


static int kos_utils_repr_registry_dispatch(
    PyObject *obj,
    PyObject **out) {

  PyTypeObject *ty;
  PyObject *handler = NULL;
  PyObject *args = NULL;
  PyCFunction meth = NULL;
  int flags = 0;
  int r = 0;

  if (! kos_utils_repr_registry)
    return 1;

  for (ty = Py_TYPE(obj); ty; ty = ty->tp_base) {
    handler = PyDict_GetItem(kos_utils_repr_registry, (PyObject *) ty);

    if (! handler)
      continue;

    if (PyCFunction_Check(handler)) {
      flags = PyCFunction_GetFlags(handler);
      if ((flags & ~METH_CLASS) == METH_NOARGS) {
        meth = PyCFunction_GetFunction(handler);
        if (! meth)
          return -1;
        *out = meth(obj, NULL);
        if (! *out)
          return -1;
        if (! PyUnicode_CheckExact(*out)) {
          Py_DECREF(*out);
          *out = NULL;
          PyErr_SetString(PyExc_TypeError, "kos_repr handler must return str");
          return -1;
        }
        return 0;
      }
    }

    args = PyTuple_Pack(1, obj);
    if (! args)
      return -1;

    r = Koskari_DemandSyncEx(handler, args, NULL, out);
    Py_DECREF(args);
    return r;
  }

  return 1;
}


PyObject *KosUtils_Repr(PyObject *obj) {

  PyObject *meth = NULL;
  PyObject *out = NULL;

  int r;

  if (PyUnicode_CheckExact(obj))
    return kos_utils_unicode_kos_repr(obj);

  r = kos_utils_repr_registry_dispatch(obj, &out);
  if (r != 1) {
    // 0 is success, -1 is error, 1 means nothing found
    return out;
  }

  // would it literally be faster for our builtin lib types if
  // I just used the registry dispatch for __kos_repr__, avoiding
  // the bound method creation?

  // TODO: Use PyObject_GetOptionalAttr for 3.13+
  meth = PyObject_GetAttr(obj, _str_kos_repr);
  if (! meth) {
    if (PyErr_ExceptionMatches(PyExc_AttributeError)) {
      PyErr_Clear();
      return PyObject_Repr(obj);
    }
    return NULL;
  }

  // CallNoArgs for object. Formals apply for koscallable accepts NULL
  // for no args.
  r = Koskari_DemandSyncEx(meth, NULL, NULL, &out);
  Py_DECREF(meth);

  if (r == 1) {
    Py_XDECREF(out);
    PyErr_SetString(PyExc_RuntimeError,
                    "async suspension is not allowed in __kos_repr__");
    return NULL;
  }

  if (r < 0) {
    Py_XDECREF(out);
    return NULL;
  }

  if (! out || ! PyUnicode_CheckExact(out)) {
    Py_XDECREF(out);
    PyErr_SetString(PyExc_TypeError, "__kos_repr__ must return str");
    return NULL;
  }

  return out;
}


static PyObject *list_kos_repr(PyObject *self, PyObject *_unused) {

  PyObject *col = NULL;
  PyObject *tmp = NULL;
  Py_ssize_t i, n;

  (void) _unused;

  if (Py_ReprEnter(self) != 0) {
    PyErr_Clear();
    Py_INCREF(_str_hash_list_cycle);
    return _str_hash_list_cycle;
  }

  n = PyList_GET_SIZE(self);
  if (n == 0) {
    Py_ReprLeave(self);
    Py_INCREF(_str_hash_list_empty);
    return _str_hash_list_empty;
  }

  col = PyList_New(0);
  if (! col)
    goto error;

  PyList_Append(col, _str_hash_list_open);

  for (i = 0; i < n; i++) {
    tmp = KosUtils_Repr(PyList_GET_ITEM(self, i));
    if (! tmp)
      goto error;

    PyList_Append(col, tmp);
    Py_DECREF(tmp);

    if (i + 1 < n)
      PyList_Append(col, _str_space);
  }

  PyList_Append(col, _str_list_close);

  tmp = PyUnicode_Join(_str_empty, col);
  Py_DECREF(col);
  Py_ReprLeave(self);
  return tmp;

 error:
  Py_XDECREF(col);
  Py_ReprLeave(self);
  return NULL;
}


static PyObject *tuple_kos_repr(PyObject *self, PyObject *_unused) {

  PyObject *col = NULL;
  PyObject *tmp = NULL;
  Py_ssize_t i, n;

  (void) _unused;

  if (Py_ReprEnter(self) != 0) {
    PyErr_Clear();
    Py_INCREF(_str_hash_tuple_cycle);
    return _str_hash_tuple_cycle;
  }

  n = PyTuple_GET_SIZE(self);
  if (n == 0) {
    Py_ReprLeave(self);
    Py_INCREF(_str_hash_tuple_empty);
    return _str_hash_tuple_empty;
  }

  col = PyList_New(0);
  if (! col)
    goto error;

  PyList_Append(col, _str_hash_tuple_open);

  for (i = 0; i < n; i++) {
    tmp = KosUtils_Repr(PyTuple_GET_ITEM(self, i));
    if (! tmp)
      goto error;

    PyList_Append(col, tmp);
    Py_DECREF(tmp);

    if (i + 1 < n)
      PyList_Append(col, _str_space);
  }

  PyList_Append(col, _str_tuple_close);

  tmp = PyUnicode_Join(_str_empty, col);
  Py_DECREF(col);
  Py_ReprLeave(self);
  return tmp;

 error:
  Py_XDECREF(col);
  Py_ReprLeave(self);
  return NULL;
}


static PyObject *dict_kos_repr(PyObject *self, PyObject *_unused) {

  PyObject *col = NULL;
  PyObject *tmp = NULL;
  PyObject *key = NULL;
  PyObject *val = NULL;
  Py_ssize_t pos = 0;
  int first = 1;

  (void) _unused;

  if (Py_ReprEnter(self) != 0) {
    PyErr_Clear();
    Py_INCREF(_str_hash_dict_cycle);
    return _str_hash_dict_cycle;
  }

  if (PyDict_GET_SIZE(self) == 0) {
    Py_ReprLeave(self);
    Py_INCREF(_str_hash_dict_empty);
    return _str_hash_dict_empty;
  }

  col = PyList_New(0);
  if (! col)
    goto error;

  PyList_Append(col, _str_hash_dict_open);

  while (PyDict_Next(self, &pos, &key, &val)) {
    if (! first)
      PyList_Append(col, _str_space);
    first = 0;

    tmp = KosUtils_Repr(key);
    if (! tmp)
      goto error;
    PyList_Append(col, tmp);
    Py_DECREF(tmp);

    PyList_Append(col, _str_colon_space);

    tmp = KosUtils_Repr(val);
    if (! tmp)
      goto error;
    PyList_Append(col, tmp);
    Py_DECREF(tmp);
  }

  PyList_Append(col, _str_dict_close);

  tmp = PyUnicode_Join(_str_empty, col);
  Py_DECREF(col);
  Py_ReprLeave(self);
  return tmp;

 error:
  Py_XDECREF(col);
  Py_ReprLeave(self);
  return NULL;
}


/* A type object reprs via normal repr, not via the __kos_repr__ method
   its instances carry: looking up __kos_repr__ on the class itself would
   otherwise find the unbound instance method and try to call it. */
static PyObject *type_kos_repr(PyObject *self, PyObject *_unused) {
  (void) _unused;
  return PyObject_Repr(self);
}


/* Ellipsis is an immutable builtin singleton; PyObject_Repr yields
   "Ellipsis", but Koskari source writes it as "...". */
static PyObject *ellipsis_kos_repr(PyObject *self, PyObject *_unused) {
  (void) self;
  (void) _unused;
  return PyUnicode_FromString("...");
}


static PyMethodDef list_kos_repr_def = {
  "list_kos_repr", (PyCFunction) list_kos_repr, METH_NOARGS, NULL
};


static PyMethodDef type_kos_repr_def = {
  "type_kos_repr", (PyCFunction) type_kos_repr, METH_NOARGS, NULL
};


static PyMethodDef ellipsis_kos_repr_def = {
  "ellipsis_kos_repr", (PyCFunction) ellipsis_kos_repr, METH_NOARGS, NULL
};


static PyMethodDef tuple_kos_repr_def = {
  "tuple_kos_repr", (PyCFunction) tuple_kos_repr, METH_NOARGS, NULL
};


static PyMethodDef dict_kos_repr_def = {
  "dict_kos_repr", (PyCFunction) dict_kos_repr, METH_NOARGS, NULL
};


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


PyObject *KosUtils_BuildStr(PyObject *const *args, Py_ssize_t nargs) {
  return KosOperator_BuildStr(args, (size_t) nargs);
}


int KosUtils_ListAppend(PyObject *list, PyObject *item) {

  if (! PyList_Check(list)) {
    PyErr_Format(PyExc_TypeError, "list_append requires list, not %R", list);
    return -1;
  }

  return PyList_Append(list, item);
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


PyObject *KosUtils_GetAttrStar(
    PyObject *element,
    PyObject *attrlist,
    PyObject *default_val) {

  Py_ssize_t count = 0;
  Py_ssize_t i = 0;

  attrlist = PySequence_Fast(attrlist, "attrlist must be a sequence");
  if (! attrlist)
    return NULL;

  count = PySequence_Fast_GET_SIZE(attrlist);
  if (! count) {
    Py_DECREF(attrlist);
    PyErr_SetString(PyExc_ValueError, "attrlist is empty");
    return NULL;
  }

  Py_INCREF(element);

  for (i = 0; i < count; i++) {
    PyObject *name = PySequence_Fast_GET_ITEM(attrlist, i);
    PyObject *val = PyObject_GetAttr(element, name);

    if (! val) {
      Py_DECREF(element);
      Py_DECREF(attrlist);

      if (default_val && PyErr_ExceptionMatches(PyExc_AttributeError)) {
        PyErr_Clear();
        Py_INCREF(default_val);
        return default_val;
      }
      return NULL;
    }

    Py_DECREF(element);
    element = val;
  }

  Py_DECREF(attrlist);
  return element;
}


PyObject *KosUtils_HasAttrStar(
    PyObject *element,
    PyObject *attrlist) {

  Py_ssize_t count = 0;
  Py_ssize_t i = 0;

  attrlist = PySequence_Fast(attrlist, "attrlist must be a sequence");
  if (! attrlist)
    return NULL;

  count = PySequence_Fast_GET_SIZE(attrlist);
  if (! count) {
    Py_DECREF(attrlist);
    PyErr_SetString(PyExc_ValueError, "attrlist is empty");
    return NULL;
  }

  Py_INCREF(element);

  for (i = 0; i < count; i++) {
    PyObject *name = PySequence_Fast_GET_ITEM(attrlist, i);
    PyObject *val = PyObject_GetAttr(element, name);

    if (! val) {
      Py_DECREF(element);
      Py_DECREF(attrlist);
      PyErr_Clear();
      Py_RETURN_FALSE;
    }

    Py_DECREF(element);
    element = val;
  }

  Py_DECREF(element);
  Py_DECREF(attrlist);
  Py_RETURN_TRUE;
}


PyObject *KosUtils_GetItemStar(
    PyObject *element,
    PyObject *keylist,
    PyObject *default_val) {

  Py_ssize_t count = 0;
  Py_ssize_t i = 0;

  keylist = PySequence_Fast(keylist, "keylist must be a sequence");
  if (! keylist)
    return NULL;

  count = PySequence_Fast_GET_SIZE(keylist);
  if (! count) {
    Py_DECREF(keylist);
    PyErr_SetString(PyExc_ValueError, "keylist is empty");
    return NULL;
  }

  Py_INCREF(element);

  for (i = 0; i < count; i++) {
    PyObject *key = PySequence_Fast_GET_ITEM(keylist, i);
    PyObject *val = PyObject_GetItem(element, key);

    if (! val) {
      Py_DECREF(element);
      Py_DECREF(keylist);

      if (default_val && PyErr_ExceptionMatches(PyExc_LookupError)) {
        PyErr_Clear();
        Py_INCREF(default_val);
        return default_val;
      }
      return NULL;
    }

    Py_DECREF(element);
    element = val;
  }

  Py_DECREF(keylist);
  return element;
}


PyObject *KosUtils_HasItemStar(
    PyObject *element,
    PyObject *keylist) {

  Py_ssize_t count = 0;
  Py_ssize_t i = 0;

  keylist = PySequence_Fast(keylist, "keylist must be a sequence");
  if (! keylist)
    return NULL;

  count = PySequence_Fast_GET_SIZE(keylist);
  if (! count) {
    Py_DECREF(keylist);
    PyErr_SetString(PyExc_ValueError, "keylist is empty");
    return NULL;
  }

  Py_INCREF(element);

  for (i = 0; i < count; i++) {
    PyObject *key = PySequence_Fast_GET_ITEM(keylist, i);
    PyObject *val = PyObject_GetItem(element, key);

    if (! val) {
      Py_DECREF(element);
      Py_DECREF(keylist);
      PyErr_Clear();
      Py_RETURN_FALSE;
    }

    Py_DECREF(element);
    element = val;
  }

  Py_DECREF(element);
  Py_DECREF(keylist);
  Py_RETURN_TRUE;
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
      } else if (KosNilP(tail)) {
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


PyObject *KosUtils_BuildClass(
    PyObject *name,
    PyObject *bases,
    PyObject *namespace,
    PyObject *metaclass) {

  if (metaclass == Py_None)
    metaclass = NULL;

  return kos_build_class(name, bases, namespace, metaclass);
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



static PyObject *m_identity(PyObject *mod, PyObject *value) {

  return KosUtils_Identity(value);
}


static PyObject *m_constantly(PyObject *mod, PyObject *value) {

  return KosUtils_Constantly(value);
}


static PyObject *m_kos_repr(PyObject *mod, PyObject *value) {

  (void) mod;
  return KosUtils_Repr(value);
}


static PyObject *m_kos_repr_register(PyObject *mod, PyObject *args) {

  PyObject *type = NULL;
  PyObject *handler = NULL;

  (void) mod;

  if (! PyArg_ParseTuple(args, "OO", &type, &handler))
    return NULL;

  if (KosUtils_ReprRegister(type, handler) < 0)
    return NULL;

  Py_RETURN_NONE;
}


static PyObject *m_kos_repr_unregister(PyObject *mod, PyObject *args) {

  PyObject *type = NULL;

  (void) mod;

  if (! PyArg_ParseTuple(args, "O", &type))
    return NULL;

  if (KosUtils_ReprUnregister(type) < 0)
    return NULL;

  Py_RETURN_NONE;
}


static PyObject *m_kos_repr_registry_clear(PyObject *mod, PyObject *unused) {

  (void) mod;
  (void) unused;

  KosUtils_ReprRegistryClear();
  Py_RETURN_NONE;
}


static PyObject *m_kos_repr_registry_snapshot(PyObject *mod, PyObject *unused) {

  PyObject *snap = NULL;

  (void) mod;
  (void) unused;

  snap = KosUtils_ReprRegistrySnapshot();
  if (! snap)
    return NULL;

  return snap;
}


static PyObject *m_pass(const PyObject *mod,
                        const PyObject *args, const PyObject *kwds) {

  (void) mod;
  (void) args;
  (void) kwds;

  Py_RETURN_NONE;
}


#define FASTCALL_METHOD_TYPE METH_FASTCALL
#define FASTCALL_WRAPPER(name)                                          \
  static PyObject *m_ ## name(PyObject *mod,                            \
                              PyObject *const *args,                    \
                              Py_ssize_t nargs) {                       \
    return KosUtils_ ## name(args, nargs);                              \
  }


FASTCALL_WRAPPER(BuildTuple);
FASTCALL_WRAPPER(BuildList);
FASTCALL_WRAPPER(BuildSet);
FASTCALL_WRAPPER(BuildStr);


static PyObject *m_ListAppend(
    PyObject *mod,
    PyObject *const *args,
    Py_ssize_t nargs) {

  (void) mod;

  PyObject *list;
  PyObject *item;

  if (nargs != 2) {
    PyErr_SetString(PyExc_TypeError, "list_append takes exactly 2 arguments");
    return NULL;
  }

  list = args[0];
  item = args[1];
  if (KosUtils_ListAppend(list, item) < 0)
    return NULL;

  Py_RETURN_NONE;
}


static PyObject *m_getattr_star(PyObject *mod, PyObject *args) {

  PyObject *element = NULL;
  PyObject *attrlist = NULL;
  PyObject *default_val = NULL;

  (void) mod;

  if (! PyArg_ParseTuple(args, "OO|O", &element, &attrlist, &default_val))
    return NULL;

  return KosUtils_GetAttrStar(element, attrlist, default_val);
}


static PyObject *m_hasattr_star(PyObject *mod, PyObject *args) {

  PyObject *element = NULL;
  PyObject *attrlist = NULL;

  (void) mod;

  if (! PyArg_ParseTuple(args, "OO", &element, &attrlist))
    return NULL;

  return KosUtils_HasAttrStar(element, attrlist);
}


static PyObject *m_getitem_star(PyObject *mod, PyObject *args) {

  PyObject *element = NULL;
  PyObject *keylist = NULL;
  PyObject *default_val = NULL;

  (void) mod;

  if (! PyArg_ParseTuple(args, "OO|O", &element, &keylist, &default_val))
    return NULL;

  return KosUtils_GetItemStar(element, keylist, default_val);
}


static PyObject *m_hasitem_star(PyObject *mod, PyObject *args) {

  PyObject *element = NULL;
  PyObject *keylist = NULL;

  (void) mod;

  if (! PyArg_ParseTuple(args, "OO", &element, &keylist))
    return NULL;

  return KosUtils_HasItemStar(element, keylist);
}


static PyObject *m_build_dict(PyObject *mod,
                              PyObject *args, PyObject *kwds) {

  return KosUtils_BuildDict(args, kwds);
}


static PyObject *m_build_class(PyObject *mod,
                               PyObject *args, PyObject *kwds) {

  PyObject *name = NULL;
  PyObject *bases = NULL;
  PyObject *namespace = NULL;
  PyObject *metaclass = Py_None;

  static char *keywords[] = { "name", "bases", "namespace", "metaclass", NULL };

  if (! PyArg_ParseTupleAndKeywords(args, kwds, "OOO|O", keywords,
                                    &name, &bases, &namespace, &metaclass))
    return NULL;

  return KosUtils_BuildClass(name, bases, namespace, metaclass);
}


static PyMethodDef utils_methods[] = {
  { "reapply", (PyCFunction) m_reapply, METH_VARARGS | METH_KEYWORDS,
    "reapply(func, data, count) -> result\n"
    "Calls data = func(data) count times, returns final result." },


  { "identity", (PyCFunction) m_identity, METH_O,
    "identity(value) -> value\n"
    "Returns its argument unchanged." },

  { "constantly", (PyCFunction) m_constantly, METH_O,
    "constantly(value) -> callable\n"
    "Returns a callable that always returns value." },

  { "kos_repr", (PyCFunction) m_kos_repr, METH_O,
    "kos_repr(obj) -> str\n"
    "Koskari-style string: str uses double quotes; then type registry;\n"
    "then __kos_repr__; otherwise repr(obj)." },

  { "kos_repr_register", (PyCFunction) m_kos_repr_register, METH_VARARGS,
    "kos_repr_register(type, handler) -> None\n"
    "Register a type-to-callable mapping for kos_repr. Handler receives\n"
    "the object and must return str." },

  { "kos_repr_unregister", (PyCFunction) m_kos_repr_unregister, METH_VARARGS,
    "kos_repr_unregister(type) -> None\n"
    "Remove a type from the kos_repr registry if present." },

  { "kos_repr_registry_clear", (PyCFunction) m_kos_repr_registry_clear,
    METH_NOARGS,
    "kos_repr_registry_clear() -> None\n"
    "Clear all kos_repr type registry entries." },

  { "kos_repr_registry_snapshot", (PyCFunction) m_kos_repr_registry_snapshot,
    METH_NOARGS,
    "kos_repr_registry_snapshot() -> dict\n"
    "Return a shallow copy of the type registry for inspection." },

  { "_pass", (PyCFunction) m_pass, METH_VARARGS | METH_KEYWORDS,
    "_pass(*args, **kwargs) -> None\n"
    "Accepts any arguments, returns None." },

  { "build_tuple", (PyCFunction) m_BuildTuple, FASTCALL_METHOD_TYPE,
    "build_tuple(*args) -> tuple\n"
    "Build a tuple from arguments." },

  { "build_list", (PyCFunction) m_BuildList, FASTCALL_METHOD_TYPE,
    "build_list(*args) -> list\n"
    "Build a list from arguments." },

  { "list_append", (PyCFunction) m_ListAppend,
    METH_FASTCALL,
    "list_append(list, item) -> None\n"
    "Append item to list; TypeError when list is not a list." },

  { "build_set", (PyCFunction) m_BuildSet, FASTCALL_METHOD_TYPE,
    "build_set(*args) -> set\n"
    "Build a set from arguments." },

  { "build_str", (PyCFunction) m_BuildStr, FASTCALL_METHOD_TYPE,
    "build_str(*args) -> str\n"
    "Join the str of each argument; an exact str is taken as it is." },

  { "getattr_star", (PyCFunction) m_getattr_star, METH_VARARGS,
    "getattr_star(obj, attrlist[, default]) -> value\n"
    "Chained attribute access through attrlist; optional default on\n"
    "AttributeError." },

  { "hasattr_star", (PyCFunction) m_hasattr_star, METH_VARARGS,
    "hasattr_star(obj, attrlist) -> bool\n"
    "Chained attribute presence test; false on first failed step." },

  { "getitem_star", (PyCFunction) m_getitem_star, METH_VARARGS,
    "getitem_star(obj, keylist[, default]) -> value\n"
    "Chained item access through keylist; optional default on\n"
    "LookupError." },

  { "hasitem_star", (PyCFunction) m_hasitem_star, METH_VARARGS,
    "hasitem_star(obj, keylist) -> bool\n"
    "Chained item presence test; false on first failed step." },

  { "build_dict", (PyCFunction) m_build_dict, METH_VARARGS | METH_KEYWORDS,
    "build_dict(*pairs, **kwargs) -> dict\n"
    "Build a dict from (key, value) pairs and keyword arguments." },

  { "build_class", (PyCFunction) m_build_class, METH_VARARGS | METH_KEYWORDS,
    "build_class(name, bases, namespace, metaclass=None) -> type\n"
    "Build a class using Koskari class-construction semantics." },

  { NULL, },
};


/* === unit lifecycle === */


int koskari_utils_init(PyObject *mod) {

  if (! mod)
    return -1;

  STR_CONST(_str_dq, "\"");
  STR_CONST(_str_esc_dq, "\\\"");
  STR_CONST(_str_kos_repr, "__kos_repr__");

  STR_CONST(_str_colon_space, ": ");
  STR_CONST(_str_empty, "");
  STR_CONST(_str_hash_dict_cycle, "#{...}");
  STR_CONST(_str_hash_dict_empty, "#{}");
  STR_CONST(_str_hash_dict_open, "#{");
  STR_CONST(_str_hash_list_cycle, "#[...]");
  STR_CONST(_str_hash_list_empty, "#[]");
  STR_CONST(_str_hash_list_open, "#[");
  STR_CONST(_str_hash_tuple_cycle, "#(...)");
  STR_CONST(_str_hash_tuple_empty, "#()");
  STR_CONST(_str_hash_tuple_open, "#(");
  STR_CONST(_str_dict_close, "}");
  STR_CONST(_str_list_close, "]");
  STR_CONST(_str_space, " ");
  STR_CONST(_str_tuple_close, ")");

  if (! kos_utils_repr_registry) {
    // registration will create this, so it may have already
    // been allocated when lib objects registered themselves.
    kos_utils_repr_registry = PyDict_New();
    if (! kos_utils_repr_registry)
      return -1;
  }

  if (PyType_Ready(&KosConstantlyType) < 0 ||
      PyType_Ready(KOS_EVALUATED_PYTYPE(KosApplyType)) < 0)
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

  Py_CLEAR(KosApplyObject);
  KosApplyObject = PyObject_CallNoArgs((PyObject *) KOS_EVALUATED_PYTYPE(KosApplyType));
  if (! KosApplyObject)
    return -1;

  if (PyObject_SetAttrString(mod, "apply", KosApplyObject) < 0)
    return -1;

  {
    PyObject *handler = PyCFunction_New(&list_kos_repr_def, NULL);

    if (! handler)
      return -1;

    if (KosUtils_ReprRegister((PyObject *) &PyList_Type, handler) < 0) {
      Py_DECREF(handler);
      return -1;
    }

    Py_DECREF(handler);
  }

  {
    PyObject *handler = PyCFunction_New(&tuple_kos_repr_def, NULL);

    if (! handler)
      return -1;

    if (KosUtils_ReprRegister((PyObject *) &PyTuple_Type, handler) < 0) {
      Py_DECREF(handler);
      return -1;
    }

    Py_DECREF(handler);
  }

  {
    PyObject *handler = PyCFunction_New(&dict_kos_repr_def, NULL);

    if (! handler)
      return -1;

    if (KosUtils_ReprRegister((PyObject *) &PyDict_Type, handler) < 0) {
      Py_DECREF(handler);
      return -1;
    }

    Py_DECREF(handler);
  }

  {
    /* registered against the type metaclass: any class object dispatches
       here (Py_TYPE(cls) is type) and reprs normally, rather than falling
       through to its instances' __kos_repr__ method. */
    PyObject *handler = PyCFunction_New(&type_kos_repr_def, NULL);

    if (! handler)
      return -1;

    if (KosUtils_ReprRegister((PyObject *) &PyType_Type, handler) < 0) {
      Py_DECREF(handler);
      return -1;
    }

    Py_DECREF(handler);
  }

  {
    PyObject *handler = PyCFunction_New(&ellipsis_kos_repr_def, NULL);

    if (! handler)
      return -1;

    if (KosUtils_ReprRegister((PyObject *) &PyEllipsis_Type, handler) < 0) {
      Py_DECREF(handler);
      return -1;
    }

    Py_DECREF(handler);
  }

  return 0;
}


int koskari_utils_clear(PyObject *mod) {

  (void) mod;
  Py_CLEAR(KosApplyObject);
  Py_CLEAR(kos_utils_repr_registry);
  Py_CLEAR(_str_colon_space);
  Py_CLEAR(_str_empty);
  Py_CLEAR(_str_hash_dict_cycle);
  Py_CLEAR(_str_hash_dict_empty);
  Py_CLEAR(_str_hash_dict_open);
  Py_CLEAR(_str_hash_list_cycle);
  Py_CLEAR(_str_hash_list_empty);
  Py_CLEAR(_str_hash_list_open);
  Py_CLEAR(_str_hash_tuple_cycle);
  Py_CLEAR(_str_hash_tuple_empty);
  Py_CLEAR(_str_hash_tuple_open);
  Py_CLEAR(_str_dict_close);
  Py_CLEAR(_str_list_close);
  Py_CLEAR(_str_space);
  Py_CLEAR(_str_tuple_close);
  Py_CLEAR(_str_kos_repr);
  Py_CLEAR(_str_dq);
  Py_CLEAR(_str_esc_dq);
  return 0;
}


// The end.
