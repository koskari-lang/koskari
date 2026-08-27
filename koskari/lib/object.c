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
   koskari/lib/object.c

   Functional class builder. kos_build_class demand-wraps the
   synchronous dunders in the class namespace (Demand for
   value-returning dunders, Procedure for __init__, staticmethod(Demand)
   for __new__, classmethod(Demand) for the class-construction hooks)
   and delegates construction to the
   winning metaclass -- plain type by default. CPython's own slot
   machinery then dispatches tp_repr, tp_richcompare, and the rest
   through the wrapped, synchronous callables; no custom metaclass is
   involved. The one post-construction touch is the kos_instance_call
   sentinel, installed as tp_call when __call__ resolves to a
   KosCallable so the evaluator can inline instance calls.

   :author: Christopher O'Brien <obriencj@preoccupied.net>
   :license: GNU General Public License v3
*/


#include "lib.h"
#include "debug.h"


/* === Interned names === */


static PyObject *str_call_name = NULL;
static PyObject *str_func_name = NULL;


/**
   Dunders that cannot tolerate a coroutine escaping into a synchronous
   Python protocol, grouped by the wrapper each needs. Values under
   these names that are KosCallables get wrapped at class-build time;
   growing coverage is one string per list.

   - procedure dunders: run on the demand path with the result
     discarded (slot_tp_init rejects a non-None return).
   - static dunders: canonicalized to staticmethod(Demand); type_new
     staticmethod-wraps these itself but only for exact plain Python
     functions.
   - class dunders: canonicalized to classmethod(Demand); same
     PyFunction-only gate in type_new's auto-classmethod treatment.
   - demand dunders: every other synchronous object dunder, wrapped in
     Demand.

   Deliberately absent: __call__ (async semantics preserved; the
   evaluator inlines it via the kos_instance_call sentinel) and the
   async protocol dunders (__await__, __aiter__, __anext__, __aenter__,
   __aexit__).
*/


static const char *procedure_dunders[] = {
  "__init__",
  NULL,
};


static const char *static_dunders[] = {
  "__new__",
  NULL,
};


static const char *class_dunders[] = {
  "__init_subclass__",
  "__class_getitem__",
  "__subclasshook__",
  NULL,
};


static const char *demand_dunders[] = {
  "__repr__", "__str__", "__format__", "__bytes__",
  "__hash__", "__bool__",
  "__eq__", "__ne__", "__lt__", "__le__", "__gt__", "__ge__",
  "__getattr__", "__getattribute__", "__setattr__", "__delattr__",
  "__dir__",
  "__get__", "__set__", "__delete__", "__set_name__",
  "__len__", "__length_hint__",
  "__getitem__", "__setitem__", "__delitem__", "__missing__",
  "__iter__", "__next__", "__reversed__", "__contains__",
  "__add__", "__radd__", "__iadd__",
  "__sub__", "__rsub__", "__isub__",
  "__mul__", "__rmul__", "__imul__",
  "__matmul__", "__rmatmul__", "__imatmul__",
  "__truediv__", "__rtruediv__", "__itruediv__",
  "__floordiv__", "__rfloordiv__", "__ifloordiv__",
  "__mod__", "__rmod__", "__imod__",
  "__divmod__", "__rdivmod__",
  "__pow__", "__rpow__", "__ipow__",
  "__lshift__", "__rlshift__", "__ilshift__",
  "__rshift__", "__rrshift__", "__irshift__",
  "__and__", "__rand__", "__iand__",
  "__xor__", "__rxor__", "__ixor__",
  "__or__", "__ror__", "__ior__",
  "__neg__", "__pos__", "__abs__", "__invert__",
  "__complex__", "__int__", "__float__", "__index__",
  "__round__", "__trunc__", "__floor__", "__ceil__",
  "__enter__", "__exit__",
  "__reduce__", "__reduce_ex__",
  "__getstate__", "__setstate__",
  "__getnewargs__", "__getnewargs_ex__",
  "__copy__", "__deepcopy__", "__sizeof__",
  "__fspath__", "__del__",
  "__instancecheck__", "__subclasscheck__",
  NULL,
};


#define COUNT_OF(arr) (sizeof(arr) / sizeof((arr)[0]))

static PyObject *procedure_names[COUNT_OF(procedure_dunders)];
static PyObject *static_names[COUNT_OF(static_dunders)];
static PyObject *class_names[COUNT_OF(class_dunders)];
static PyObject *demand_names[COUNT_OF(demand_dunders)];


/* === Procedure === */


/*
  Procedure is a Demand whose result is discarded: calling it runs the
  wrapped callable on the demand path and returns None. It exists for
  __init__, where slot_tp_init raises TypeError on a non-None return
  while a Koskari function returns its body's value. Kept out of the
  module dict for now; construction, binding, idempotent re-wrap, and
  attribute forwarding are all inherited from Demand.
*/


static PyObject *procedure_tp_call(
    PyObject *self,
    PyObject *args,
    PyObject *kwargs) {

  PyObject *wrapped = KosDemand_GetWrapped(self);

  if (! wrapped) {
    PyErr_SetString(PyExc_TypeError, "Procedure object is not initialized");
    return NULL;
  }

  PyObject *result = Koskari_DemandSync(wrapped, args, kwargs);
  if (! result)
    return NULL;

  Py_DECREF(result);
  Py_RETURN_NONE;
}


static PyObject *procedure_repr(PyObject *self) {

  PyObject *wrapped = KosDemand_GetWrapped(self);

  if (! wrapped)
    return PyUnicode_FromString("<Procedure ?>");

  PyObject *name = PyObject_GetAttrString(wrapped, "__name__");
  if (! name) {
    PyErr_Clear();
    return PyUnicode_FromString("<Procedure <lambda>>");
  }

  PyObject *result = PyUnicode_FromFormat("<Procedure %R>", name);
  Py_DECREF(name);
  return result;
}


/**
   Evaluator install: run the wrapped callable via the demand core,
   discard its result, and push None -- the None contract holds on both
   the Python tp_call path and the evaluator path. No frame is
   requested, so this mirrors Demand's cold sync fallback rather than
   its flattening install.
*/
static KosOpResult procedure_kos_install(
    KosEvalState *es,
    PyObject *callable,
    PyObject *args_tuple,
    PyObject *kwds_dict,
    int tail_call) {

  PyObject *wrapped = KosDemand_GetWrapped(callable);
  PyObject *result = NULL;

  (void) tail_call;

  if (! wrapped) {
    PyErr_SetString(PyExc_TypeError, "Procedure object is not initialized");
    return EVAL_RAISE();
  }

  if (Koskari_DemandSyncEx(wrapped, args_tuple, kwds_dict, &result) < 0)
    return EVAL_RAISE();
  Py_XDECREF(result);

  Py_INCREF(Py_None);
  if (KosEvalState_StackPUSH(es, Py_None) < 0)
    return EVAL_RAISE();

  return EVAL_NORMAL();
}


static KosEvaluatedTypeObject KosProcedureCallableType = {
  .type = {
    PyVarObject_HEAD_INIT(NULL, 0)

    .tp_name = "koskari.lib.Procedure",
    .tp_basicsize = 0,  /* inherited from Demand by PyType_Ready */
    .tp_itemsize = 0,
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_doc = "A Demand with no result: runs its wrapped callable on the "
              "demand path and returns None",
    .tp_repr = procedure_repr,
    .tp_call = KosCallAdapt_CallAsync,
    .tp_base = KOS_EVALUATED_PYTYPE(KosDemandCallableType),
  },
  .kos_call = {
    .kc_install = procedure_kos_install,
    .kc_vinstall = NULL,
    .kc_wants_frame = 0,
    .kc_wants_frame_fn = NULL,
    .kc_pycall = procedure_tp_call,
    .kc_pyvectorcall = NULL,
  },
};


static PyObject *KosProcedure_New(PyObject *wrapped) {

  return PyObject_CallOneArg(
      (PyObject *) KOS_EVALUATED_PYTYPE(KosProcedureCallableType), wrapped);
}


/* === Namespace preparation === */


static PyObject *build_default_namespace(PyObject *namespace) {

  PyObject *ns_dict = PyDict_New();
  if (! ns_dict)
    return NULL;

  if (PyDict_Merge(ns_dict, namespace, 1) < 0) {
    Py_DECREF(ns_dict);
    return NULL;
  }

  /* __class__ is filled on the input namespace after the metaclass returns;
     omit it from the mapping passed to type.__new__. */
  if (PyDict_DelItemString(ns_dict, "__class__") < 0) {
    if (! PyErr_ExceptionMatches(PyExc_KeyError)) {
      Py_DECREF(ns_dict);
      return NULL;
    }
    PyErr_Clear();
  }

  return ns_dict;
}


/**
   Wrap every KosCallable under the listed names in place using wrap
   (KosDemand_New or KosProcedure_New). Both constructors are
   idempotent, so re-running over an already-wrapped namespace is a
   no-op.
*/
static int wrap_name_list(
    PyObject *ns_dict,
    PyObject **names,
    PyObject *(*wrap)(PyObject *)) {

  for (size_t i = 0; names[i]; i++) {
    PyObject *val = PyDict_GetItem(ns_dict, names[i]);
    if (! val)
      continue;

    if (! KosCallable_Check(val))
      continue;

    PyObject *wrapped = wrap(val);
    if (! wrapped)
      return -1;

    if (wrapped != val
        && PyDict_SetItem(ns_dict, names[i], wrapped) < 0) {
      Py_DECREF(wrapped);
      return -1;
    }
    Py_DECREF(wrapped);
  }

  return 0;
}


/**
   Canonicalize KosCallables under the listed names to
   descr_new(Demand(fn)) -- staticmethod(Demand) or classmethod(Demand).
   type_new performs its own staticmethod/classmethod treatment for
   these names, but only for exact plain Python functions
   (PyFunction_Check); a bare KosCallable would otherwise reach the
   synchronous construction machinery unwrapped. An author-written
   staticmethod/classmethod is unwrapped through __func__ and rebuilt
   the same way, so both spellings converge on one canonical form.
   Already-canonical entries and plain Python callables are untouched.
*/
static int canonicalize_name_list(
    PyObject *ns_dict,
    PyObject **names,
    PyTypeObject *descr_type,
    PyObject *(*descr_new)(PyObject *)) {

  for (size_t i = 0; names[i]; i++) {
    PyObject *val = PyDict_GetItem(ns_dict, names[i]);
    if (! val)
      continue;

    PyObject *inner = val;
    PyObject *func_ref = NULL;
    int is_wrapped = (Py_TYPE(val) == descr_type);

    if (is_wrapped) {
      func_ref = PyObject_GetAttr(val, str_func_name);
      if (! func_ref)
        return -1;
      inner = func_ref;
    }

    if (! KosCallable_Check(inner)) {
      Py_XDECREF(func_ref);
      continue;
    }

    PyObject *demanded = KosDemand_New(inner);
    Py_XDECREF(func_ref);
    if (! demanded)
      return -1;

    if (is_wrapped && demanded == inner) {
      /* already descr(Demand(...)): true no-op */
      Py_DECREF(demanded);
      continue;
    }

    PyObject *descr = descr_new(demanded);
    Py_DECREF(demanded);
    if (! descr)
      return -1;

    int r = PyDict_SetItem(ns_dict, names[i], descr);
    Py_DECREF(descr);
    if (r < 0)
      return -1;
  }

  return 0;
}


/**
   Apply the four wrap passes to the class namespace in place. Only
   KosCallables are touched; plain Python callables are already
   synchronous and pass through untouched. Every pass is idempotent.
*/
static int demand_wrap_namespace(PyObject *ns_dict) {

  if (wrap_name_list(ns_dict, procedure_names, KosProcedure_New) < 0)
    return -1;

  if (wrap_name_list(ns_dict, demand_names, KosDemand_New) < 0)
    return -1;

  if (canonicalize_name_list(ns_dict, static_names,
                             &PyStaticMethod_Type, PyStaticMethod_New) < 0)
    return -1;

  return canonicalize_name_list(ns_dict, class_names,
                                &PyClassMethod_Type, PyClassMethod_New);
}


/* === Metaclass resolution === */


/**
   Determine the winning metaclass, CPython __build_class__ style: start
   from the explicit metaclass (or type), then take the most derived of
   it and every base's metaclass. An explicit non-type callable is used
   as given. Returns a new reference, or NULL with PyErr set on a
   metaclass conflict.
*/
static PyObject *resolve_metaclass(
    PyObject *bases,
    PyObject *metaclass_or_null) {

  PyObject *winner = metaclass_or_null
      ? metaclass_or_null
      : (PyObject *) &PyType_Type;

  if (! PyType_Check(winner)) {
    Py_INCREF(winner);
    return winner;
  }

  Py_ssize_t n = PyTuple_GET_SIZE(bases);
  for (Py_ssize_t i = 0; i < n; i++) {
    PyObject *base = PyTuple_GET_ITEM(bases, i);
    if (! PyType_Check(base))
      continue;

    PyTypeObject *base_meta = Py_TYPE(base);
    if (PyType_IsSubtype((PyTypeObject *) winner, base_meta))
      continue;
    if (PyType_IsSubtype(base_meta, (PyTypeObject *) winner)) {
      winner = (PyObject *) base_meta;
      continue;
    }

    PyErr_SetString(PyExc_TypeError,
        "metaclass conflict: the metaclass of a derived class must be a "
        "(non-strict) subclass of the metaclasses of all its bases");
    return NULL;
  }

  Py_INCREF(winner);
  return winner;
}


static PyObject *resolve_prepare_namespace(
    PyObject *metaclass,
    PyObject *name,
    PyObject *bases) {

  PyObject *prepare = PyObject_GetAttrString(metaclass, "__prepare__");
  if (! prepare) {
    if (PyErr_ExceptionMatches(PyExc_AttributeError)) {
      PyErr_Clear();
      return PyDict_New();
    }
    return NULL;
  }

  PyObject *prepared = PyObject_CallFunctionObjArgs(prepare, name, bases, NULL);
  Py_DECREF(prepare);
  return prepared;
}


/* === instance __call__ inlining === */


/**
   tp_call shim installed on classes whose __call__ resolves to a
   KosCallable. It doubles as a sentinel: the evaluator recognizes it via
   KosInstanceCall_Check and inlines the call through the install slot instead
   of starting a fresh eval loop (see KosEvalState_Call).

   Unlike the demand-wrapped dunders this preserves async semantics -- a
   bound async __call__ returns a coroutine here, exactly as CPython's
   slot_tp_call would. Self injection follows ordinary descriptor binding:
   getattr yields a bound method for a KosFunction/Demand accessor.
*/
PyObject *kos_instance_call(PyObject *self, PyObject *args, PyObject *kwds) {

  PyObject *call = PyObject_GetAttr(self, str_call_name);
  if (! call)
    return NULL;

  PyObject *result = PyObject_Call(call, args, kwds);
  Py_DECREF(call);
  return result;
}


/**
   Resolve the unbound __call__ of self's type for inline dispatch. Returns a
   BORROWED reference to a KosCallable, or NULL if __call__ is absent or not a
   KosCallable. Uses the type method cache (PyType_Lookup) -- no allocation, no
   descriptor binding, no transient string. The evaluator injects self as the
   first positional itself, so no bound method is built.
*/
PyObject *kos_instance_call_target(PyObject *self) {

  PyObject *fn = PyType_Lookup(Py_TYPE(self), str_call_name);
  if (fn && KosCallable_Check(fn))
    return fn;
  return NULL;
}


/**
   Install the kos_instance_call sentinel on a freshly built class whose
   __call__ resolves to a KosCallable. A plain Python __call__ keeps
   CPython's slot_tp_call.
*/
static void install_call_sentinel(PyTypeObject *type) {

  PyObject *fn = PyType_Lookup(type, str_call_name);
  if (fn && KosCallable_Check(fn)) {
    type->tp_call = kos_instance_call;
    PyType_Modified(type);
  }
}


/* === kos_build_class === */


PyObject *kos_build_class(
    PyObject *name,
    PyObject *bases,
    PyObject *namespace,
    PyObject *metaclass_or_null) {

  if (! name || ! PyUnicode_Check(name)) {
    PyErr_SetString(PyExc_TypeError, "name must be a str");
    return NULL;
  }

  PyObject *bases_tuple = bases;
  if (! PyTuple_CheckExact(bases_tuple)) {
    bases_tuple = PySequence_Tuple(bases);
    if (! bases_tuple)
      return NULL;
  } else {
    Py_INCREF(bases_tuple);
  }

  PyObject *ns_dict = build_default_namespace(namespace);
  if (! ns_dict) {
    Py_DECREF(bases_tuple);
    return NULL;
  }

  if (demand_wrap_namespace(ns_dict) < 0) {
    Py_DECREF(ns_dict);
    Py_DECREF(bases_tuple);
    return NULL;
  }

  PyObject *metaclass = resolve_metaclass(bases_tuple, metaclass_or_null);
  if (! metaclass) {
    Py_DECREF(ns_dict);
    Py_DECREF(bases_tuple);
    return NULL;
  }

  PyObject *prepared_ns = resolve_prepare_namespace(metaclass, name, bases_tuple);
  if (! prepared_ns) {
    Py_DECREF(metaclass);
    Py_DECREF(ns_dict);
    Py_DECREF(bases_tuple);
    return NULL;
  }

  Py_ssize_t pos = 0;
  PyObject *key = NULL;
  PyObject *value = NULL;
  while (PyDict_Next(ns_dict, &pos, &key, &value)) {
    if (PyObject_SetItem(prepared_ns, key, value) < 0) {
      Py_DECREF(ns_dict);
      Py_DECREF(prepared_ns);
      Py_DECREF(metaclass);
      Py_DECREF(bases_tuple);
      return NULL;
    }
  }
  Py_DECREF(ns_dict);

  PyObject *cls = PyObject_CallFunctionObjArgs(
      metaclass,
      name,
      bases_tuple,
      prepared_ns,
      NULL);

  Py_DECREF(prepared_ns);
  Py_DECREF(metaclass);
  Py_DECREF(bases_tuple);

  if (cls && PyType_Check(cls))
    install_call_sentinel((PyTypeObject *) cls);

  return cls;
}


/* === Unit lifecycle === */


static int intern_name_list(const char **specs, PyObject **out) {

  size_t i;
  for (i = 0; specs[i]; i++) {
    out[i] = PyUnicode_InternFromString(specs[i]);
    if (! out[i])
      return -1;
  }
  out[i] = NULL;
  return 0;
}


static void clear_name_list(PyObject **names) {

  for (size_t i = 0; names[i]; i++)
    Py_CLEAR(names[i]);
}


int koskari_object_init(PyObject *mod) {

  (void) mod;

  str_call_name = PyUnicode_InternFromString("__call__");
  if (! str_call_name)
    return -1;

  str_func_name = PyUnicode_InternFromString("__func__");
  if (! str_func_name)
    return -1;

  if (intern_name_list(procedure_dunders, procedure_names) < 0
      || intern_name_list(static_dunders, static_names) < 0
      || intern_name_list(class_dunders, class_names) < 0
      || intern_name_list(demand_dunders, demand_names) < 0)
    return -1;

  /* Demand must be ready first: koskari_demand_init runs earlier in the
     unit order (see lib.c). */
  if (PyType_Ready(KOS_EVALUATED_PYTYPE(KosProcedureCallableType)) < 0)
    return -1;

  return 0;
}


int koskari_object_clear(PyObject *mod) {

  Py_CLEAR(str_call_name);
  Py_CLEAR(str_func_name);
  clear_name_list(procedure_names);
  clear_name_list(static_names);
  clear_name_list(class_names);
  clear_name_list(demand_names);
  (void) mod;
  return 0;
}


// The end.
