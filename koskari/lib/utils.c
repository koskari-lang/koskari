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
   Native handler for apply (non-flattening path). Stack has [fn, seq_args, kwds]
   (kwds may be Py_None). Pops them, PyObject_Call, then pops the apply frame,
   pushes result onto parent stack, and push_or_await. The apply frame has
   code=NULL so we must detach it before continuing; otherwise the eval loop
   would segfault in prepare_dispatch.
*/
static int apply_native_handler(
    KosEvalState *es,
    KosEvent *out_ev,
    int is_resume,
    PyObject *delivered_value) {

  (void) delivered_value;
  if (unlikely(is_resume != 0)) {
    PyErr_SetString(PyExc_RuntimeError, "apply native handler resume not implemented");
    return -1;
  }

  PyObject *kwds = KosEvalState_StackPopRef(es);
  PyObject *seq_args = KosEvalState_StackPopRef(es);
  PyObject *fn = KosEvalState_StackPopRef(es);
  if (! fn || ! seq_args) {
    Py_XDECREF(kwds);
    Py_XDECREF(seq_args);
    Py_XDECREF(fn);
    return -1;
  }
  if (kwds == Py_None)
    kwds = NULL;

  KosDynFrame_NATIVE_HANDLER(KosEvalState_FRAME(es)) = NULL;
  PyObject *result = PyObject_Call(fn, seq_args, kwds);
  Py_DECREF(fn);
  Py_DECREF(seq_args);
  Py_XDECREF(kwds);
  if (! result)
    return -1;

  /* Walk RETURN to frame (FINALLY may consume). */
  KosDynFrame *detached = NULL;
  KosTransitionReq treq = {
    .kind = KOS_TRANS_WALK_RETURN_TO_FRAME,
    .target_frame = KosEvalState_FRAME(es),
    .payload = result,
  };
  KosTransitionResult tres = {0};
  if (KosEvalState_Transition(es, &treq, &tres, out_ev) < 0) {
    Py_DECREF(result);
    return -1;
  }
  int unw = tres.raw;
  if (unw == KOS_DYN_UNWIND_FINALLY_TAKING_RETURN) {
    /* Delivered to finally; eval continues at finally block */
    return 0;
  }
  if (unw == KOS_DYN_UNWIND_ASYNC_STARTED) {
    KosEvalState_SetPendingReturn(es, result, 1);
    return 0;
  }
  if (unw < 0) {
    Py_DECREF(result);
    return -1;
  }
  if (kos_dyn_detach_top_frame(es, &detached) < 0) {
    Py_DECREF(result);
    return -1;
  }
  if (detached)
    kos_dynframe_free(detached);

  return KosEvalState_PushOrAwait(es, out_ev, result);
}


static int kos_install_apply(
    KosEvalState *es,
    KosEvent *out_ev,
    PyObject *callable,
    PyObject *args_tuple,
    PyObject *kwds_dict,
    int tail_call) {

  (void) callable;
  Py_ssize_t n = args_tuple ? PyTuple_GET_SIZE(args_tuple) : 0;
  if (n < 1 || n > 3) {
    PyErr_SetString(PyExc_TypeError, "apply expects 1, 2, or 3 arguments");
    return -1;
  }

  PyObject *fn = PyTuple_GET_ITEM(args_tuple, 0);
  PyObject *arglist = (n >= 2) ? PyTuple_GET_ITEM(args_tuple, 1) : NULL;
  PyObject *kwds = (n >= 3) ? PyTuple_GET_ITEM(args_tuple, 2) : NULL;
  PyObject *seq_args = NULL;

  if (kos_apply_build_args_tuple(arglist, &seq_args) < 0)
    return -1;

  if (kwds == Py_None)
    kwds = NULL;

  if (kwds && ! PyDict_Check(kwds)) {
    PyErr_SetString(PyExc_TypeError, "apply kwargs must be a dict or None");
    Py_DECREF(seq_args);
    return -1;
  }

  /* Flattening: delegate to inner callable's install */
  if (KosCallable_Check(fn)) {
    KosInstallFunc inner = KosCallableType_INSTALL(Py_TYPE(fn));
    if (inner) {
      int r = inner(es, out_ev, fn, seq_args, kwds, tail_call);
      Py_DECREF(seq_args);
      return r;
    }
  }

  /* Non-flattening: configure frame with native handler, fn, seq_args, kwds */
  KosDynFrame *frame = KosEvalState_FRAME(es);
  typedef int (*NativeHandlerFn)(KosEvalState *, KosEvent *, int, PyObject *);
  KosDynFrame_NATIVE_HANDLER(frame) = (NativeHandlerFn) apply_native_handler;

  if (KosEvalState_StackPushRef(es, fn) < 0
      || KosEvalState_StackPushRef(es, seq_args) < 0) {
    Py_DECREF(seq_args);
    return -1;
  }
  PyObject *kwds_push = kwds ? kwds : Py_None;
  Py_INCREF(kwds_push);
  if (KosEvalState_StackPushRef(es, kwds_push) < 0) {
    Py_DECREF(kwds_push);
    return -1;
  }
  return 0;
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


static KosCallableTypeObject KosApplyType = {
  .type = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "koskari.lib.ApplyHelper",
    .tp_basicsize = sizeof(PyObject),
    .tp_dealloc = (destructor) PyObject_Del,
    .tp_call = apply_call,
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_doc = "Callable helper implementing apply(func, args=(), kwargs=None)",
    .tp_new = PyType_GenericNew,
    .tp_base = KOS_CALLABLE_PYTYPE(KosCallableType),
  },
  .tp_kos_install = kos_install_apply,
  .tp_kos_vinstall = NULL,
  .tp_kos_wants_frame = 1,
  .tp_kos_wants_frame_fn = NULL,
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

  { "build_set", (PyCFunction) m_BuildSet, FASTCALL_METHOD_TYPE,
    "build_set(*args) -> set\n"
    "Build a set from arguments." },

  { "getattr_star", (PyCFunction) m_getattr_star, METH_VARARGS,
    "getattr_star(obj, attrlist[, default]) -> value\n"
    "Chained attribute access through attrlist; optional default on\n"
    "AttributeError." },

  { "hasattr_star", (PyCFunction) m_hasattr_star, METH_VARARGS,
    "hasattr_star(obj, attrlist) -> bool\n"
    "Chained attribute presence test; false on first failed step." },

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

  if (! kos_utils_repr_registry) {
    // registration will create this, so it may have already
    // been allocated when lib objects registered themselves.
    kos_utils_repr_registry = PyDict_New();
    if (! kos_utils_repr_registry)
      return -1;
  }

  if (PyType_Ready(&KosConstantlyType) < 0 ||
      PyType_Ready(KOS_CALLABLE_PYTYPE(KosApplyType)) < 0)
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
  KosApplyObject = PyObject_CallNoArgs((PyObject *) KOS_CALLABLE_PYTYPE(KosApplyType));
  if (! KosApplyObject)
    return -1;

  if (PyObject_SetAttrString(mod, "apply", KosApplyObject) < 0)
    return -1;

  return 0;
}


int koskari_utils_clear(PyObject *mod) {

  (void) mod;
  Py_CLEAR(KosApplyObject);
  Py_CLEAR(kos_utils_repr_registry);
  Py_CLEAR(_str_kos_repr);
  Py_CLEAR(_str_dq);
  Py_CLEAR(_str_esc_dq);
  return 0;
}


// The end.
