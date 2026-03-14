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

   KosType native metaclass: tp_call uses alloc + demand_sync(__init__).
   (C ...) in Koskari bytecode uses the default PyCall path.

   :author: Christopher O'Brien <obriencj@preoccupied.net>
   :license: GNU General Public License v3
   :ai-assistant: Auto via Cursor
*/


#include "lib.h"
#include "debug.h"


#define CURRENT_FRAME(ctx)  KosContext_FRAME_TOP(ctx)
#define CURRENT_STACK(ctx)  KosDynFrame_STACK(CURRENT_FRAME(ctx))


/* === Dunder adapter (KoskariSyncViolation cache) === */


static PyObject *KoskariSyncViolation = NULL;


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
  PyObject *instance = type->tp_alloc(type, 0);
  if (! instance)
    return NULL;

  SAYF("kostype_tp_call: instance = %R", instance);

  PyObject *init = NULL;
  if (type->tp_dict)
    init = PyDict_GetItemString(type->tp_dict, "__init__");

  SAYF("kostype_tp_call: init = %R", init);

  if (! init)
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
  int r = Koskari_DemandSyncEx(init, init_args, kwargs, &init_result);
  /* demand_sync_ex consumes init_args */

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
  /* demand_sync_ex consumes args */
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


static PyObject *dunder_lookup_in_mro(PyTypeObject *type,
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
  Py_DECREF(attr);
  if (! is_callable)
    return 0;

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


/* === kostype_tp_new === */


static PyObject *kostype_tp_new(
    PyTypeObject *metaclass,
    PyObject *args,
    PyObject *kwds) {

  SAYF("kostype_tp_new: metaclass = %R, args = %R, kwds = %R", metaclass, args, kwds);
  PyObject *cls = PyType_Type.tp_new(metaclass, args, kwds);
  SAYF("kostype_tp_new: cls = %R", cls);
  if (! cls)
    return NULL;

  koskari_fixup_slot_dispatchers((PyTypeObject *) cls);
  PyType_Modified((PyTypeObject *) cls);

  SAYF("kostype_tp_new -> %R", cls);
  return cls;
}


/* === kos_get_default_metaclass === */


PyObject *kos_get_default_metaclass(void) {
  PyObject *meta = (PyObject *) &KosTypeType;
  Py_INCREF(meta);
  return meta;
}


/* === kostype_tp_dealloc === */


static void kostype_tp_dealloc(PyObject *self) {
  const char *name = ((PyTypeObject *) self)->tp_name;
  SAYF("[KosType] dealloc: %s", name ? name : "<unknown>");
  PyType_Type.tp_dealloc(self);
}


/* === KosTypeType === */


PyTypeObject KosTypeType = {
  PyVarObject_HEAD_INIT(NULL, 0)
  .tp_name = "koskari.lib.KosType",
  .tp_basicsize = sizeof(PyHeapTypeObject),
  .tp_flags = Py_TPFLAGS_DEFAULT,

  .tp_doc = "Metaclass for Koskari classes. Instances are classes. "
            "Calling C(...) returns the instance directly.",

  .tp_new = kostype_tp_new,
  .tp_call = kostype_tp_call,
  .tp_dealloc = kostype_tp_dealloc,
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

  /* Cache KoskariSyncViolation for dunder slot shims (sync_init runs first) */
  KoskariSyncViolation = PyDict_GetItemString(dict, "KoskariSyncViolation");
  Py_XINCREF(KoskariSyncViolation);

  return 0;
}


int koskari_object_clear(PyObject *mod) {

  Py_CLEAR(KoskariSyncViolation);
  (void) mod;
  return 0;
}


// The end.
