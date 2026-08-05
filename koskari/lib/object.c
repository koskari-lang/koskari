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
   slot-backed dunders in the class namespace (Demand for value-returning
   dunders, Procedure for __init__) and delegates construction to the
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


/**
   Slot-backed dunders that cannot tolerate a coroutine escaping into a
   C protocol. Values under these names that are KosCallables get
   demand-wrapped at class-build time; __init__ takes the Procedure
   wrapper so its result is discarded, satisfying slot_tp_init's
   None-return check. Growing coverage is one row per name.

   Deliberately absent: __call__ (async semantics preserved; the
   evaluator inlines it via the kos_instance_call sentinel) and the
   async protocol dunders (__await__, __aiter__, __anext__, __aenter__,
   __aexit__).
*/
static const struct {
  const char *name;
  int procedure;
} wrap_specs[] = {
  {"__init__", 1},
  {"__repr__", 0},
  {"__str__", 0},
  {"__add__", 0},
  {"__eq__", 0},
  {"__ne__", 0},
  {"__lt__", 0},
  {"__le__", 0},
  {"__gt__", 0},
  {"__ge__", 0},
  {"__len__", 0},
  {"__bool__", 0},
  {"__iter__", 0},
};

#define WRAP_SPEC_COUNT (sizeof(wrap_specs) / sizeof(wrap_specs[0]))

static PyObject *wrap_names[WRAP_SPEC_COUNT];


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


static KosCallableTypeObject KosProcedureCallableType = {
  .type = {
    PyVarObject_HEAD_INIT(NULL, 0)

    .tp_name = "koskari.lib.Procedure",
    .tp_basicsize = 0,  /* inherited from Demand by PyType_Ready */
    .tp_itemsize = 0,
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_doc = "A Demand with no result: runs its wrapped callable on the "
              "demand path and returns None",
    .tp_repr = procedure_repr,
    .tp_call = procedure_tp_call,
    .tp_base = KOS_CALLABLE_PYTYPE(KosDemandCallableType),
  },
  .tp_kos_install = procedure_kos_install,
  .tp_kos_vinstall = NULL,
  .tp_kos_wants_frame = 0,
  .tp_kos_wants_frame_fn = NULL,
};


static PyObject *KosProcedure_New(PyObject *wrapped) {

  return PyObject_CallOneArg(
      (PyObject *) KOS_CALLABLE_PYTYPE(KosProcedureCallableType), wrapped);
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
   Demand-wrap the listed dunders in place. Only KosCallables are
   touched; plain Python callables are already synchronous and pass
   through untouched. Demand and Procedure construction are both
   idempotent, so re-running over an already-wrapped namespace is a
   no-op.
*/
static int demand_wrap_namespace(PyObject *ns_dict) {

  for (size_t i = 0; i < WRAP_SPEC_COUNT; i++) {
    PyObject *val = PyDict_GetItemWithError(ns_dict, wrap_names[i]);
    if (! val) {
      if (PyErr_Occurred())
        return -1;
      continue;
    }

    if (! KosCallable_Check(val))
      continue;

    PyObject *wrapped = wrap_specs[i].procedure
        ? KosProcedure_New(val)
        : KosDemand_New(val);
    if (! wrapped)
      return -1;

    if (wrapped != val
        && PyDict_SetItem(ns_dict, wrap_names[i], wrapped) < 0) {
      Py_DECREF(wrapped);
      return -1;
    }
    Py_DECREF(wrapped);
  }

  return 0;
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


int koskari_object_init(PyObject *mod) {

  (void) mod;

  str_call_name = PyUnicode_InternFromString("__call__");
  if (! str_call_name)
    return -1;

  for (size_t i = 0; i < WRAP_SPEC_COUNT; i++) {
    wrap_names[i] = PyUnicode_InternFromString(wrap_specs[i].name);
    if (! wrap_names[i])
      return -1;
  }

  /* Demand must be ready first: koskari_demand_init runs earlier in the
     unit order (see lib.c). */
  if (PyType_Ready(KOS_CALLABLE_PYTYPE(KosProcedureCallableType)) < 0)
    return -1;

  return 0;
}


int koskari_object_clear(PyObject *mod) {

  Py_CLEAR(str_call_name);
  for (size_t i = 0; i < WRAP_SPEC_COUNT; i++)
    Py_CLEAR(wrap_names[i]);
  (void) mod;
  return 0;
}


// The end.
