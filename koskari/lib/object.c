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


#define CURRENT_FRAME(ctx)  KosContext_FRAME_TOP(ctx)
#define CURRENT_STACK(ctx)  KosDynFrame_STACK(CURRENT_FRAME(ctx))


/* === Dunder adapter (KoskariSyncViolation cache) === */


static PyObject *KoskariSyncViolation = NULL;


/* === kostype_tp_call === */


static PyObject *kostype_tp_call(PyObject *cls, PyObject *args,
                                 PyObject *kwargs) {

  /* 3-arg form (name, bases, namespace) is class creation; delegate
     to type's tp_call. Instance construction: alloc + demand_sync(__init__). */
  if (args && PyTuple_GET_SIZE(args) == 3
      && PyUnicode_Check(PyTuple_GET_ITEM(args, 0))
      && PyTuple_Check(PyTuple_GET_ITEM(args, 1))
      && PyDict_Check(PyTuple_GET_ITEM(args, 2))) {
    return PyType_Type.tp_call(cls, args, kwargs);
  }

  PyTypeObject *type = (PyTypeObject *) cls;
  PyObject *instance = type->tp_alloc(type, 0);
  if (! instance)
    return NULL;

  PyObject *init = NULL;
  if (type->tp_dict)
    init = PyDict_GetItemString(type->tp_dict, "__init__");

  if (! init)
    return instance;

  PyObject *init_args = PyTuple_Pack(1, instance);
  if (! init_args) {
    Py_DECREF(instance);
    return NULL;
  }
  PyObject *full_args = PySequence_Concat(init_args,
      args ? args : PyTuple_New(0));
  Py_DECREF(init_args);
  if (! full_args) {
    Py_DECREF(instance);
    return NULL;
  }

  PyObject *init_result = NULL;
  int r = Koskari_DemandSyncEx(init, full_args, kwargs, &init_result);
  Py_DECREF(full_args);
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
  return instance;
}


/* === Dunder adapter: slot table and shims === */


typedef enum {
  SLOT_UNARY,
  SLOT_BINARY,
  SLOT_RICHCMP,
} slot_kind_t;


typedef struct {
  const char *dunder;
  slot_kind_t kind;
} slot_def_t;


static const slot_def_t slot_defs[] = {
  {"__add__", SLOT_BINARY},
  {"__repr__", SLOT_UNARY},
  {"__str__", SLOT_UNARY},
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
    return NULL;
  }
  if (r < 0)
    return NULL;
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
    PyObject *attr = PyDict_GetItemString(base_type->tp_dict, dunder);
    if (attr) {
      Py_INCREF(attr);
      return attr;
    }
  }
  return NULL;
}


static int koskari_update_one_slot(PyTypeObject *type,
                                   const char *dunder,
                                   slot_kind_t kind) {

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
    return 0;
  }

  if (kind == SLOT_BINARY) {
    if (strcmp(dunder, "__add__") == 0) {
      static PyNumberMethods number_methods = { .nb_add = dunder_slot_nb_add_shim };
      type->tp_as_number = &number_methods;
      return 0;
    }
    return 0;
  }

  return 0;
}


static void koskari_fixup_slot_dispatchers(PyTypeObject *type) {

  for (size_t i = 0; i < sizeof(slot_defs) / sizeof(slot_defs[0]); i++) {
    const slot_def_t *def = &slot_defs[i];
    koskari_update_one_slot(type, def->dunder, def->kind);
  }
}


/* === kostype_tp_new === */


static PyObject *kostype_tp_new(PyTypeObject *metaclass, PyObject *args,
                                PyObject *kwds) {

  PyObject *cls = PyType_Type.tp_new(metaclass, args, kwds);
  if (! cls)
    return NULL;

  koskari_fixup_slot_dispatchers((PyTypeObject *) cls);
  return cls;
}


/* === kos_get_default_metaclass === */


PyObject *kos_get_default_metaclass(void) {
  PyObject *meta = (PyObject *) KOS_TYPETYPE_PYTYPE(KosTypeType);
  Py_INCREF(meta);
  return meta;
}


/* === KosTypeType === */


KosTypeTypeObject KosTypeType = {
  .type = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "koskari.lib.KosType",
    .tp_basicsize = sizeof(PyHeapTypeObject),
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_doc = "Metaclass for Koskari classes. Instances are classes. "
              "Calling C(...) returns the instance directly.",
    .tp_new = kostype_tp_new,
    .tp_call = kostype_tp_call,
    .tp_base = &PyType_Type,
    .tp_bases = NULL,
  },
};


/* === Unit lifecycle === */


int koskari_object_init(PyObject *mod) {

  PyObject *bases = PyTuple_Pack(1, (PyObject *) &PyType_Type);
  if (! bases)
    return -1;
  PyTypeObject *kt = KOS_TYPETYPE_PYTYPE(KosTypeType);
  kt->tp_base = &PyType_Type;
  kt->tp_bases = bases;
  if (PyType_Ready(kt) < 0) {
    Py_DECREF(bases);
    return -1;
  }
  PyObject *dict = PyModule_GetDict(mod);
  if (! dict)
    return -1;
  if (PyDict_SetItemString(dict, "KosType",
                           (PyObject *) KOS_TYPETYPE_PYTYPE(KosTypeType)) < 0)
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
