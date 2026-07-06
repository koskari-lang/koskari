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

   KosType native metaclass: tp_call uses tp_new + demand_sync(__init__).
   (C ...) in Koskari bytecode uses the default PyCall path.

   :author: Christopher O'Brien <obriencj@preoccupied.net>
   :license: GNU General Public License v3
*/


#include "lib.h"
#include "debug.h"


/* === Dunder adapter (KoskariSyncViolation cache) === */


static PyObject *KoskariSyncViolation = NULL;
static PyObject *CompositeMetaCache = NULL;
static PyObject *str_init_name = NULL;
static PyObject *str_call_name = NULL;
static PyObject *object_init = NULL;

static void koskari_fixup_slot_dispatchers(PyTypeObject *type);


/* === Composite metaclass cache and class builder === */


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


static PyObject *build_composite_metaclass(PyObject *incoming_meta) {

  if (! CompositeMetaCache) {
    CompositeMetaCache = PyDict_New();
    if (! CompositeMetaCache)
      return NULL;
  }

  PyObject *cached = PyDict_GetItemWithError(CompositeMetaCache, incoming_meta);
  if (cached) {
    Py_INCREF(cached);
    return cached;
  }
  if (PyErr_Occurred())
    return NULL;

  PyObject *meta_name = PyObject_GetAttrString(incoming_meta, "__name__");
  if (! meta_name)
    return NULL;

  PyObject *combined_name = PyUnicode_FromFormat("KosCombinedMeta_%U", meta_name);
  Py_DECREF(meta_name);
  if (! combined_name)
    return NULL;

  PyObject *combined_bases = PyTuple_Pack(2, incoming_meta, (PyObject *) &KosTypeType);
  if (! combined_bases) {
    Py_DECREF(combined_name);
    return NULL;
  }

  PyObject *combined_ns = PyDict_New();
  if (! combined_ns) {
    Py_DECREF(combined_bases);
    Py_DECREF(combined_name);
    return NULL;
  }

  PyObject *composite = PyObject_CallFunctionObjArgs(
      (PyObject *) &PyType_Type,
      combined_name,
      combined_bases,
      combined_ns,
      NULL);

  Py_DECREF(combined_ns);
  Py_DECREF(combined_bases);
  Py_DECREF(combined_name);
  if (! composite)
    return NULL;

  if (PyDict_SetItem(CompositeMetaCache, incoming_meta, composite) < 0) {
    Py_DECREF(composite);
    return NULL;
  }

  return composite;
}


static PyObject *resolve_metaclass(
    PyObject *bases,
    PyObject *metaclass_or_null) {

  if (metaclass_or_null) {
    Py_INCREF(metaclass_or_null);
    return metaclass_or_null;
  }

  if (! PyTuple_Check(bases)) {
    PyErr_SetString(PyExc_TypeError, "bases must be a tuple");
    return NULL;
  }

  if (PyTuple_GET_SIZE(bases) < 1)
    return kos_get_default_metaclass();

  PyObject *first_base = PyTuple_GET_ITEM(bases, 0);
  if (! PyType_Check(first_base)) {
    PyErr_SetString(PyExc_TypeError, "base classes must be types");
    return NULL;
  }

  PyObject *incoming_meta = (PyObject *) Py_TYPE(first_base);
  if (! incoming_meta)
    return NULL;

  if (incoming_meta == (PyObject *) &PyType_Type)
    return kos_get_default_metaclass();

  if (PyType_IsSubtype((PyTypeObject *) incoming_meta, &KosTypeType)) {
    Py_INCREF(incoming_meta);
    return incoming_meta;
  }

  return build_composite_metaclass(incoming_meta);
}


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

  if (! cls) {
    Py_DECREF(prepared_ns);
    Py_DECREF(metaclass);
    Py_DECREF(bases_tuple);
    return NULL;
  }

  if (PyType_Check(cls)
      && PyType_IsSubtype((PyTypeObject *) Py_TYPE(cls), &KosTypeType)) {
    koskari_fixup_slot_dispatchers((PyTypeObject *) cls);
    PyType_Modified((PyTypeObject *) cls);
  }

  Py_DECREF(prepared_ns);
  Py_DECREF(metaclass);
  Py_DECREF(bases_tuple);
  return cls;
}


/* === kostype_tp_init === */


static int kostype_tp_init(PyObject *self, PyObject *args, PyObject *kwds) {

  /* type_call runs tp_init on the new class after type_new; vectorcall path
   * does the same. Install demand-aware slots here so we do not rely on
   * kostype_tp_call being used for KosType(name, bases, namespace). */
  if (PyType_Check(self)) {
    koskari_fixup_slot_dispatchers((PyTypeObject *) self);
    PyType_Modified((PyTypeObject *) self);
  }

  if (PyType_Type.tp_init)
    return PyType_Type.tp_init(self, args, kwds);

  (void) args;
  (void) kwds;
  return 0;
}


/* === kostype_tp_call === */


static PyObject *kostype_tp_call(PyObject *cls, PyObject *args,
                                 PyObject *kwargs) {

  /* 3-arg form (name, bases, namespace) is class creation only when the
     callable is the metaclass (BUILD_CLASS path). Instance construction with
     three positional args would otherwise be misclassified. Delegate to
     type's tp_call. */
  if (args && PyTuple_GET_SIZE(args) == 3
      && PyUnicode_Check(PyTuple_GET_ITEM(args, 0))
      && PyTuple_Check(PyTuple_GET_ITEM(args, 1))
      && PyDict_Check(PyTuple_GET_ITEM(args, 2))
      && PyType_IsSubtype((PyTypeObject *)cls, &KosTypeType)) {
    return PyType_Type.tp_call(cls, args, kwargs);
  }

  SAYF("kostype_tp_call: cls = %R, args = %R, kwargs = %R", cls, args, kwargs);

  PyTypeObject *type = (PyTypeObject *) cls;
  PyObject *instance = type->tp_new(type, args, kwargs);
  if (! instance)
    return NULL;

  if (! PyType_IsSubtype(Py_TYPE(instance), type))
    return instance;

  SAYF("kostype_tp_call: instance = %R", instance);

  PyObject *init = NULL;

  if (str_init_name)
    init = PyType_Lookup(type, str_init_name);

  SAYF("kostype_tp_call: init = %R", init);

  if (! init || init == object_init)
    return instance;

  PyObject *init_args = PyTuple_Pack(1, instance);
  if (! init_args) {
    Py_DECREF(instance);
    return NULL;
  }

  if (args) {
    PyObject *full_args = PySequence_Concat(init_args, args);
    Py_DECREF(init_args);
    if (! full_args) {
      Py_DECREF(instance);
      return NULL;
    }
    init_args = full_args;
  }

  SAYF("kostype_tp_call: init_args = %R", init_args);

  PyObject *init_result = NULL;
  int r;

  Py_INCREF(init);
  r = Koskari_DemandSyncEx(init, init_args, kwargs, &init_result);
  Py_DECREF(init);
  Py_DECREF(init_args);

  SAYF("kostype_tp_call: r = %d, init_result = %R", r, init_result);

  if (r == 1) {
    Py_DECREF(instance);
    if (KoskariSyncViolation)
      PyErr_Format(KoskariSyncViolation,
          "async suspension is not allowed in Python __init__ for Koskari type");
    return NULL;
  }
  if (r < 0) {
    Py_DECREF(instance);
    return NULL;
  }

  Py_XDECREF(init_result);
  SAYF("kostype_tp_call -> %R", instance);
  return instance;
}


/* === Dunder adapter: slot table and shims === */


typedef enum {
  SLOT_UNARY,
  SLOT_BINARY,
  SLOT_RICHCMP,
  SLOT_LEN,
  SLOT_ITER,
  SLOT_CALL,
} slot_kind_t;


typedef struct {
  const char *dunder;
  slot_kind_t kind;
} slot_def_t;


static const slot_def_t slot_defs[] = {
  {"__add__", SLOT_BINARY},
  {"__repr__", SLOT_UNARY},
  {"__str__", SLOT_UNARY},
  {"__eq__", SLOT_RICHCMP},
  {"__ne__", SLOT_RICHCMP},
  {"__lt__", SLOT_RICHCMP},
  {"__le__", SLOT_RICHCMP},
  {"__gt__", SLOT_RICHCMP},
  {"__ge__", SLOT_RICHCMP},
  {"__len__", SLOT_LEN},
  {"__bool__", SLOT_UNARY},
  {"__iter__", SLOT_ITER},
  {"__call__", SLOT_CALL},
};


static PyObject *dunder_slot_unary(PyObject *self, const char *dunder) {

  PyObject *method = PyObject_GetAttrString(self, dunder);
  if (! method)
    return NULL;
  /* method is bound (self already); call with no args */
  PyObject *args = PyTuple_New(0);
  if (! args) {
    Py_DECREF(method);
    return NULL;
  }
  PyObject *result = NULL;
  int r = Koskari_DemandSyncEx(method, args, NULL, &result);
  Py_DECREF(args);
  Py_DECREF(method);
  if (r == 1) {
    if (KoskariSyncViolation)
      PyErr_Format(KoskariSyncViolation,
                   "async suspension is not allowed in Python %s for Koskari type",
                   dunder);
    return NULL;
  }
  if (r < 0)
    return NULL;
  return result;
}


static PyObject *dunder_slot_binary(PyObject *a, PyObject *b,
                                    const char *dunder) {

  SAYF("dunder_slot_binary: a = %R, b = %R, dunder = %s", a, b, dunder);

  PyObject *method = PyObject_GetAttrString(a, dunder);
  if (! method)
    return NULL;

  /* method is bound to a; call with (b,) only */
  PyObject *args = PyTuple_Pack(1, b);
  if (! args) {
    Py_DECREF(method);
    return NULL;
  }

  PyObject *result = NULL;
  int r = Koskari_DemandSyncEx(method, args, NULL, &result);
  Py_DECREF(args);
  Py_DECREF(method);

  if (r == 1) {
    if (KoskariSyncViolation)
      PyErr_Format(KoskariSyncViolation,
                   "async suspension is not allowed in Python %s for Koskari type",
                   dunder);
    // fall-through to return result
  }
  SAYF("dunder_slot_binary -> %R", result);
  return result;
}


static PyObject *dunder_slot_repr_shim(PyObject *self) {

  return dunder_slot_unary(self, "__repr__");
}


static PyObject *dunder_slot_str_shim(PyObject *self) {

  return dunder_slot_unary(self, "__str__");
}


static PyObject *dunder_slot_nb_add_shim(PyObject *a, PyObject *b) {

  return dunder_slot_binary(a, b, "__add__");
}


static const char *richcmp_op_to_dunder(int op) {

  switch (op) {
    case Py_EQ: return "__eq__";
    case Py_NE: return "__ne__";
    case Py_LT: return "__lt__";
    case Py_LE: return "__le__";
    case Py_GT: return "__gt__";
    case Py_GE: return "__ge__";
    default: return NULL;
  }
}


static PyObject *dunder_slot_richcompare(PyObject *self, PyObject *other,
                                         int op) {

  const char *dunder = richcmp_op_to_dunder(op);
  if (! dunder)
    return Py_NotImplemented;
  PyObject *result = dunder_slot_binary(self, other, dunder);
  if (! result)
    return NULL;
  return result;
}


static Py_ssize_t dunder_slot_sq_length(PyObject *self) {

  PyObject *result = dunder_slot_unary(self, "__len__");
  if (! result) {
    return -1;
  }
  Py_ssize_t len = PyLong_AsSsize_t(result);
  Py_DECREF(result);
  return len;
}


static int dunder_slot_nb_bool_shim(PyObject *self) {

  PyObject *result = dunder_slot_unary(self, "__bool__");
  if (! result)
    return -1;
  int truth = PyObject_IsTrue(result);
  Py_DECREF(result);
  return truth;
}


static PyObject *dunder_slot_iter_shim(PyObject *self) {

  return dunder_slot_unary(self, "__iter__");
}


/**
   tp_call shim installed on Koskari types whose __call__ resolves to a
   KosCallable. It doubles as a sentinel: the evaluator recognizes it via
   KosInstanceCall_Check and inlines the call through the install slot instead
   of starting a fresh eval loop (see KosEvalState_Call).

   Unlike the demand-wrapped dunder shims this preserves async semantics -- a
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


static PyObject *dunder_lookup_in_mro(const PyTypeObject *type,
                                      const char *dunder) {

  PyObject *mro = type->tp_mro;
  if (! mro || ! PyTuple_Check(mro))
    return NULL;

  Py_ssize_t n = PyTuple_GET_SIZE(mro);
  for (Py_ssize_t i = 0; i < n; i++) {
    PyObject *base = PyTuple_GET_ITEM(mro, i);
    if (! PyType_Check(base))
      continue;

    PyTypeObject *base_type = (PyTypeObject *) base;
    if (! base_type->tp_dict)
      continue;

    PyObject *attr = PyDict_GetItemString(
      base_type->tp_dict,
      dunder);

    if (attr) {
      Py_INCREF(attr);
      return attr;
    }
  }

  return NULL;
}


static int koskari_update_one_slot(
    PyTypeObject *type,
    const char *dunder,
    slot_kind_t kind) {

  SAYF("koskari_update_one_slot: type = %R, dunder = %s, kind = %d", type, dunder, kind);

  PyObject *attr = dunder_lookup_in_mro(type, dunder);
  if (! attr)
    return 0;

  int is_callable = PyCallable_Check(attr);
  int is_koscallable = KosCallable_Check(attr);
  Py_DECREF(attr);
  if (! is_callable)
    return 0;

  if (kind == SLOT_CALL) {
    /* Install the inlining sentinel only when __call__ is a KosCallable; a
       plain Python __call__ keeps CPython's slot_tp_call. */
    if (is_koscallable)
      type->tp_call = kos_instance_call;
    return 0;
  }

  if (kind == SLOT_UNARY) {
    if (strcmp(dunder, "__repr__") == 0) {
      type->tp_repr = dunder_slot_repr_shim;
      return 0;
    }
    if (strcmp(dunder, "__str__") == 0) {
      type->tp_str = dunder_slot_str_shim;
      return 0;
    }
    if (strcmp(dunder, "__bool__") == 0) {
      static PyNumberMethods number_methods_both = {
        .nb_add = dunder_slot_nb_add_shim,
        .nb_bool = dunder_slot_nb_bool_shim,
      };
      type->tp_as_number = &number_methods_both;
      return 0;
    }
    return 0;
  }

  if (kind == SLOT_BINARY) {
    if (strcmp(dunder, "__add__") == 0) {
      static PyNumberMethods number_methods_add = {
        .nb_add = dunder_slot_nb_add_shim,
      };
      type->tp_as_number = &number_methods_add;
      return 0;
    }
    return 0;
  }

  if (kind == SLOT_RICHCMP) {
    type->tp_richcompare = dunder_slot_richcompare;
    return 0;
  }

  if (kind == SLOT_LEN) {
    static PySequenceMethods seq_methods = {
      .sq_length = dunder_slot_sq_length,
    };
    type->tp_as_sequence = &seq_methods;
    return 0;
  }

  if (kind == SLOT_ITER) {
    type->tp_iter = dunder_slot_iter_shim;
    return 0;
  }

  return 0;
}


static void koskari_fixup_slot_dispatchers(PyTypeObject *type) {
  SAYF("koskari_fixup_slot_dispatchers: type = %R", type);
  for (size_t i = 0; i < sizeof(slot_defs) / sizeof(slot_defs[0]); i++) {
    const slot_def_t *def = &slot_defs[i];
    SAYF("koskari_fixup_slot_dispatchers: def = %s, %d", def->dunder, def->kind);
    koskari_update_one_slot(type, def->dunder, def->kind);
  }
}


/* === kos_get_default_metaclass === */


PyObject *kos_get_default_metaclass(void) {
  PyObject *meta = (PyObject *) &KosTypeType;
  Py_INCREF(meta);
  return meta;
}


/* === KosTypeType === */


PyTypeObject KosTypeType = {
  PyVarObject_HEAD_INIT(NULL, 0)
  .tp_name = "koskari.lib.KosType",
  .tp_basicsize = sizeof(PyHeapTypeObject),
  .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,

  .tp_doc = "Metaclass for Koskari classes. Instances are classes. "
            "Calling C(...) returns the instance directly.",

  /* Leave tp_new NULL: PyType_Ready copies type.__new__ from tp_base so this
   * matches PyType_Type.tp_new. That satisfies tp_new_wrapper in typeobject.c
   * when a foreign metaclass super()s through KosType. Slot fixup runs in
   * kostype_tp_init (after type_new) and kos_build_class. */
  .tp_new = NULL,
  .tp_init = kostype_tp_init,
  .tp_call = kostype_tp_call,
  // .tp_dealloc = kostype_tp_dealloc,
  .tp_base = &PyType_Type,
  .tp_bases = NULL,
};


/* === Unit lifecycle === */


int koskari_object_init(PyObject *mod) {
  if (PyType_Ready(&KosTypeType)) {
    return -1;
  }

  PyObject *dict = PyModule_GetDict(mod);
  if (! dict)
    return -1;

  if (PyDict_SetItemString(dict, "KosType",
                           (PyObject *) &KosTypeType) < 0)
    return -1;

  str_init_name = PyUnicode_InternFromString("__init__");
  if (! str_init_name)
    return -1;

  str_call_name = PyUnicode_InternFromString("__call__");
  if (! str_call_name)
    return -1;

  object_init = PyObject_GetAttrString(
      (PyObject *) &PyBaseObject_Type, "__init__");
  if (! object_init)
    return -1;

  /* Cache KoskariSyncViolation for dunder slot shims (sync_init runs first) */
  KoskariSyncViolation = PyDict_GetItemString(dict, "KoskariSyncViolation");
  Py_XINCREF(KoskariSyncViolation);

  return 0;
}


int koskari_object_clear(PyObject *mod) {

  Py_CLEAR(KoskariSyncViolation);
  Py_CLEAR(CompositeMetaCache);
  Py_CLEAR(str_init_name);
  Py_CLEAR(str_call_name);
  Py_CLEAR(object_init);
  (void) mod;
  return 0;
}


// The end.
