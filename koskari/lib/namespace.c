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
   koskari.lib - namespace module

   Namespace types for Koskari

   author: Christopher O'Brien <obriencj@gmail.com>
   license: GPL v.3
*/


#include "lib.h"
#include "debug.h"


/* === String constants === */


static PyObject *_namespace_str = NULL;  // "<namespace>"
static PyObject *_module_str = NULL;     // "<module>"


// forward declaration to make it available for KosNamespace_REF
static PyObject *mdl_ns_borrow_ref(PyObject *self, PyObject *name);


/* === NamespaceSpec === */


static PyMemberDef nspec_members[] = {
  { "name", T_OBJECT,
    offsetof(KosNamespaceSpec, ns_name), READONLY,
    " " },

  { "local_syms", T_OBJECT,
    offsetof(KosNamespaceSpec, local_syms), READONLY,
    " " },

  { "closure_syms", T_OBJECT,
    offsetof(KosNamespaceSpec, closure_syms), READONLY,
    " " },

  { NULL },
};


PyObject *KosNamespace_FromSpec(PyObject *nspec, PyObject *parent) {

  PyObject *res = KosNamespace_NewFull(KosNamespaceSpec_NAME(nspec),
                                        parent,
                                        KosNamespaceSpec_LOCAL_SYMS(nspec),
                                        KosNamespaceSpec_CLOSURE_SYMS(nspec));

  if (PyErr_Occurred())
    Py_CLEAR(res);

  return res;
}


static PyObject *nspec_m_namespace(PyObject *self, PyObject *parent) {

  return KosNamespace_FromSpec(self, parent);
}


static PyMethodDef nspec_methods[] = {
  { "get_namespace", (PyCFunction) nspec_m_namespace, METH_O,
    "NSPEC.get_namespace(parent) -> NS" },

  { NULL },
};


static int nspec_contains(PyObject *self, PyObject *key) {
  return (PySequence_Contains(KosNamespaceSpec_LOCAL_SYMS(self), key) ||
          PySequence_Contains(KosNamespaceSpec_CLOSURE_SYMS(self), key));
}


static PySequenceMethods nspec_seq = {
  .sq_contains = nspec_contains,
};


static PyObject *nspec_new(PyTypeObject *type,
                           PyObject *args, PyObject *kwds) {

  PyObject *name = NULL;
  PyObject *local_syms = NULL;
  PyObject *closure_syms = NULL;

  if (! PyArg_ParseTuple(args, "OOO:NamespaceSpec", &name,
                         &local_syms, &closure_syms))
    return NULL;

  return KosNamespaceSpec_New(name, local_syms, closure_syms);
}


static void nspec_dealloc(PyObject *self) {
  Py_CLEAR(KosNamespaceSpec_NAME(self));
  Py_CLEAR(KosNamespaceSpec_LOCAL_SYMS(self));
  Py_CLEAR(KosNamespaceSpec_CLOSURE_SYMS(self));

  Py_TYPE(self)->tp_free(self);
}


static int nspec_traverse(PyObject *self, visitproc visit, void *arg) {
  Py_VISIT(KosNamespaceSpec_NAME(self));
  Py_VISIT(KosNamespaceSpec_LOCAL_SYMS(self));
  Py_VISIT(KosNamespaceSpec_CLOSURE_SYMS(self));
  return 0;
}


static int nspec_clear(PyObject *self) {
  Py_CLEAR(KosNamespaceSpec_NAME(self));
  Py_CLEAR(KosNamespaceSpec_LOCAL_SYMS(self));
  Py_CLEAR(KosNamespaceSpec_CLOSURE_SYMS(self));
  return 0;
}


PyTypeObject KosNamespaceSpecType = {
  PyVarObject_HEAD_INIT(NULL, 0)

  "NamespaceSpec",
  sizeof(KosNamespaceSpec),
  0,

  .tp_flags = Py_TPFLAGS_DEFAULT,
  .tp_members = nspec_members,
  .tp_methods = nspec_methods,
  .tp_as_sequence = &nspec_seq,
  .tp_new = nspec_new,
  .tp_dealloc = nspec_dealloc,
  .tp_traverse = nspec_traverse,
  .tp_clear = nspec_clear,
  // .tp_repr = ns_repr,
};


PyObject *KosNamespaceSpec_New(
    PyObject *name,
    PyObject *local_syms,
    PyObject *closure_syms) {

  // SAYF("KosNamespaceSpec_New(%R, %R, %R)",
  //      name, local_syms, closure_syms);

  if (name == Py_None)
    name = NULL;

  if (name && ! PyUnicode_Check(name)) {
    PyErr_Format(PyExc_TypeError, "name must be a str, not %R", name);
    return NULL;
  }

  if (local_syms == Py_None) {
    local_syms = NULL;

  } else if (local_syms) {
    local_syms = PySequence_Tuple(local_syms);
    if (! local_syms)
      return NULL;
  }

  if (closure_syms == Py_None) {
    closure_syms = NULL;

  } else if (closure_syms) {
    closure_syms = PySequence_Tuple(closure_syms);
    if (! closure_syms) {
      Py_XDECREF(local_syms);
      return NULL;
    }
  }

  PyObject *self = PyObject_New(PyObject, &KosNamespaceSpecType);
  if (! self)
    return NULL;

  Py_XINCREF(name);
  KosNamespaceSpec_NAME(self) = name;

  // both are new references or NULL
  KosNamespaceSpec_LOCAL_SYMS(self) = local_syms;
  KosNamespaceSpec_CLOSURE_SYMS(self) = closure_syms;

  // SAYF("KosNamespaceSpec_New -> %R", self);
  return self;
}


/* === Namespace === */


static PyObject *ns_borrow_loose_ref(PyObject *namespace, PyObject *key) {
  return PyDict_GetItemWithError(KosNamespace_BINDS(namespace), key);
}


static PyObject *ns_borrow_ref(PyObject *namespace, PyObject *key) {
  PyObject *binds = KosNamespace_BINDS(namespace);
  PyObject *ref = PyDict_GetItemWithError(binds, key);

  if (ref == Py_None) {
    // None is a sentinel value in our binds dict. It indicates that a
    // binding is currently fast-only and raw (not wrapped in a Ref
    // instance). Since we're being asked for a Ref, we'll convert
    // that raw value into one.

    SAYF("converting binding %S from raw to reference", key);
    ref = KosSymbolicRef_New(key, NULL);
    if (! ref) {
      return NULL;
    }

    if (PyDict_SetItem(binds, key, ref)) {
      Py_DECREF(ref);
      return NULL;
    }

    int index = KosNamespace_FastIndex(namespace, key);
    if (index >= 0) {
      KosNameBind *nb = KosNamespace_GET_FAST(namespace, index);

      if (! KosNameBind_RAW(nb)) {
        PyErr_Format(PyExc_RuntimeError, "reference style mistmatch, %S"
                     " is not raw, has value %R", key, KosNameBind_HELD(nb));
        return NULL;
      }

      KosSymbolicRef_VALUE(ref) = KosNameBind_HELD(nb);  // steal raw value
      KosNameBind_HELD(nb) = ref;  // steals reference to ref
      KosNameBind_RAW(nb) = 0;     // not raw anymore
    }
  }

  // NULL or a borrowed reference to a Ref instance
  return ref;
}


static int ns_init_syms(PyObject *self, PyObject *parent,
                        PyObject *local_syms, PyObject *closure_syms) {

  // SAYF("ns_init_syms(%R, %R, %R, %R",
  //      self, parent, local_syms, closure_syms);

  if (! self)
    return -1;

  size_t count = 0;
  PyObject *binds = KosNamespace_BINDS(self);

  // SAYF("  processing local_syms %R", local_syms);
  if (local_syms) {
    size_t target = PySequence_Fast_GET_SIZE(local_syms);
    for(size_t i = 0; i < target; i++) {
      PyObject *ref_name = PySequence_Fast_GET_ITEM(local_syms, i);

      int res = PyDict_SetItem(binds, ref_name, Py_None);
      if (res)
        return res;

      KosNameBind *nb = KosNamespace_GET_FAST(self, count++);
      Py_INCREF(ref_name);
      KosNameBind_NAME(nb) = ref_name;
      KosNameBind_HELD(nb) = NULL;
      KosNameBind_RAW(nb) = 1;
    }
  }

  // SAYF("  processing closure_syms %R", closure_syms);
  if(closure_syms) {
    size_t target = PySequence_Fast_GET_SIZE(closure_syms);
    for(size_t i = 0; i < target; i++) {
      PyObject *ref_name = PySequence_Fast_GET_ITEM(closure_syms, i);
      PyObject *ref = NULL;

      if (KosNamespace_FindRef(parent, ref_name, &ref)) {
        return -1;

      } else if (! ref) {
        PyErr_Format(PyExc_NameError, "So such reference: %S", ref_name);
        return -1;
      }

      int res = PyDict_SetItem(binds, ref_name, ref);
      if (res)
        return res;

      KosNameBind *nb = KosNamespace_GET_FAST(self, count++);
      Py_INCREF(ref_name);
      KosNameBind_NAME(nb) = ref_name;
      Py_INCREF(ref);
      KosNameBind_HELD(nb) = ref;
      KosNameBind_RAW(nb) = 0;
    }
  }

  return 0;
}


static Py_ssize_t ns_len(PyObject *self) {
  return PyDict_Size(KosNamespace_BINDS(self));
}


static int ns_fast_index(PyObject *self, PyObject *key) {
  size_t count = KosNamespace_FAST_COUNT(self);
  for (size_t index = 0; index < count; index++) {
    KosNameBind *nb = KosNamespace_GET_FAST(self, index);
    PyObject *name = KosNameBind_NAME(nb);
    if (name && Py_EQUALS(key, name))
      return index;
  }
  return -1;
}


static PyObject *ns_deref_symbol(PyObject *self, PyObject *key) {

  PyObject *ref = KosNamespace_LOOSE_REF(self, key);

  if (! ref) {
    if (! PyErr_Occurred())
      PyErr_Format(PyExc_NameError, "No such reference: %S", key);
    return NULL;

  } else if (ref == Py_None) {
    int index = ns_fast_index(self, key);
    if (index < 0) {
      PyErr_Format(PyExc_NameError, "No such reference: %S", key);
      return NULL;
    }

    KosNameBind *nb = KosNamespace_GET_FAST(self, index);
    PyObject *val = KosNameBind_HELD(nb);
    if (val) {
      Py_INCREF(val);
    } else {
      PyErr_Format(PyExc_NameError, "Empty reference: %S", key);
    }
    return val;

  } else {
    return KosRef_Deref(ref);
  }
}


static int ns_store_symbol(PyObject *self, PyObject *key, PyObject *val) {

  PyObject *ref = KosNamespace_LOOSE_REF(self, key);

  if (! ref) {
    if (! PyErr_Occurred())
      PyErr_Format(PyExc_NameError, "No such reference: %S", key);
    return -1;

  } else if (ref == Py_None) {
    int index = ns_fast_index(self, key);
    if (index < 0) {
      PyErr_Format(PyExc_NameError, "No such reference: %S", key);
      return -1;
    }

    KosNameBind *nb = KosNamespace_GET_FAST(self, index);
    Py_XINCREF(val);
    Py_XDECREF(KosNameBind_HELD(nb));
    KosNameBind_HELD(nb) = val;
    KosNameBind_RAW(nb) = 1;

    return 0;

  } else {
    return val? KosRef_Store(ref, val): KosRef_Clear(ref);
  }
}


static int ns_bind_ref(PyObject *self, PyObject *key, PyObject *ref) {
  // SAYF("ns_bind_ref(%R, %R, %R)", self, key, ref);

  if (! KosRef_Check(ref)) {
    PyErr_Format(PyExc_TypeError, "namespace binding must be a ref, not: %R",
                 ref);
    return -1;
  }

  int fast = KosNamespace_FastIndex(self, key);
  // SAYF(" fast index %i", fast);
  if (fast >= 0) {
    KosNameBind *nb = KosNamespace_GET_FAST(self, fast);

    Py_INCREF(key);
    Py_CLEAR(KosNameBind_NAME(nb));
    KosNameBind_NAME(nb) = key;

    Py_INCREF(ref);
    Py_CLEAR(KosNameBind_HELD(nb));
    KosNameBind_HELD(nb) = ref;

    KosNameBind_RAW(nb) = 0;
  }

  fast = PyDict_SetItem(KosNamespace_BINDS(self), key, ref);
  SAYF("ns_bind_ref -> %i", fast);
  return fast;
}


static PyMappingMethods ns_as_mapping = {
  .mp_length = ns_len,
  .mp_subscript = ns_deref_symbol,
  .mp_ass_subscript = ns_store_symbol,
};


static PyObject *ns_m_ref(PyObject *self, PyObject *key) {

  PyObject *result = KosNamespace_REF(self, key);
  if (! result && ! PyErr_Occurred())
    PyErr_Format(PyExc_NameError, "no such reference: %R", key);

  Py_XINCREF(result);
  return result;
}


static int ns_contains(PyObject *self, PyObject *key) {
  PyObject *ref = KosNamespace_REF(self, key);
  if (! ref && PyErr_Occurred()) {
    return -1;
  } else {
    return !!ref;
  }
}


static PySequenceMethods ns_seq = {
  .sq_contains = ns_contains,
};


static PyObject *ns_m_deref(PyObject *self, PyObject *key) {
  return ns_deref_symbol(self, key);
}


static PyObject *ns_m_store(PyObject *self, PyObject *args) {
  PyObject *key = NULL;
  PyObject *val = NULL;

  if (! PyArg_ParseTuple(args, "OO:Namespace.store", &key, &val))
    return NULL;

  if (ns_store_symbol(self, key, val))
    return NULL;

  Py_RETURN_NONE;
}


static PyObject *ns_m_clear(PyObject *self, PyObject *key) {
  if (ns_store_symbol(self, key, NULL))
    return NULL;

  Py_RETURN_NONE;
}


static PyObject *ns_m_bind(PyObject *self, PyObject *args) {
  PyObject *key = NULL;
  PyObject *ref = NULL;

  if (! PyArg_ParseTuple(args, "OO:Namespace.bind", &key, &ref))
    return NULL;

  if (ns_bind_ref(self, key, ref))
    return NULL;

  Py_RETURN_NONE;
}


/*
static PyObject *ns_m_reset(PyObject *self) {
  if (! KosNamespace_Reset(self))
    return NULL;

  Py_RETURN_NONE;
}
*/


/*
static PyObject *ns_m_get_name(PyObject *self) {
  PyObject *name = KosNamespace_NAME(self);
  if (name) {
    Py_XINCREF(name);
    return name;

  } else {
    Py_RETURN_NONE;
  }
}


static PyObject *ns_m_get_parent(PyObject *self) {
  PyObject *parent = KosNamespace_PARENT(self);
  if (parent) {
    Py_XINCREF(parent);
    return parent;

  } else {
    Py_RETURN_NONE;
  }
}
*/


static PyObject *ns_m_contains(PyObject *self, PyObject *key) {
  return PyLong_FromLong(KosNamespace_Contains(self, key));
}


static PyObject *ns_m_raw(PyObject *self, PyObject *key) {
  PyObject *ref = KosNamespace_LOOSE_REF(self, key);
  if (! ref) {
    PyErr_Format(PyExc_NameError, "No such reference: %S", key);
    return NULL;

  } else if (ref == Py_None) {
    Py_RETURN_TRUE;

  } else {
    Py_RETURN_FALSE;
  }
}


static PyObject *ns_m_keys(PyObject *self) {
  return PyDict_Keys(KosNamespace_BINDS(self));
}


static PyObject *ns_m_items(PyObject *self) {
  return PyDict_Items(KosNamespace_BINDS(self));
}


static PyObject *ns_m_find(PyObject *self, PyObject *key) {
  PyObject *ref = Py_None;

  if (KosNamespace_FindRef(self, key, &ref)) {
    // error
    return NULL;

  } else {
    // we either found a new ref, or left it None
    Py_INCREF(ref);
    return ref;
  }
}


static PyMethodDef ns_methods[] = {
  { "ref", (PyCFunction) ns_m_ref, METH_O,
    "NS.ref(K) -> R\n"
    "returns the reference for a binding in this namespace" },

  { "deref", (PyCFunction) ns_m_deref, METH_O,
    "NS.deref(K) -> V\n"
    "dereferences a binding in this namespace" },

  { "store", (PyCFunction) ns_m_store, METH_VARARGS,
    "NS.store(K, V) => None\n"
    "assigns a value to a binding in this namespace" },

  { "clear", (PyCFunction) ns_m_clear, METH_O,
    "NS.clear(K) => None\n"
    "removes the value from a binding in this namespace" },

  { "bind", (PyCFunction) ns_m_bind, METH_VARARGS,
    "NS.bind(K, R) => None\n"
    "sets the reference for a binding in this namespace" },

  { "bound", (PyCFunction) ns_m_contains, METH_O,
    "NS.bound(K) => bool\n"
    "True if the given name has a binding in this namespace" },

  { "raw", (PyCFunction) ns_m_raw, METH_O,
    "NS.raw(K) => bool\n"
    "True if the given binding is currently raw in this namespace" },

  /*
  { "reset", (PyCFunction) ns_m_reset, METH_NOARGS,
    "NS.reset() => None" },
  */

  { "keys", (PyCFunction) ns_m_keys, METH_NOARGS,
    "NS.keys() => iter" },

  { "items", (PyCFunction) ns_m_items, METH_NOARGS,
    "NS.items() => iter" },

  { "__contains__", (PyCFunction) ns_m_contains, METH_O,
    "(K in NS) => bool" },

  { "find", (PyCFunction) ns_m_find, METH_O,
    "NS.find(K) => R\n"
    "traverses this namespace and its parents to find and return a"
    " reference, or returns None if no binding is found" },

  { NULL },
};


static PyMemberDef ns_members[] = {
  { "name", T_OBJECT, offsetof(KosNamespace, name), READONLY,
    "str; namespace name" },

  { "parent", T_OBJECT, offsetof(KosNamespace, parent), READONLY,
    "Namespace; parent namespace" },

  { NULL },
};


static PyObject *ns_newfull_type(
    PyTypeObject *type,
    PyObject *name,
    PyObject *parent,
    PyObject *local_syms,
    PyObject *closure_syms) {

  if (! name || name == Py_None)
    name = _namespace_str;

  if (! PyUnicode_Check(name)) {
    PyErr_Format(PyExc_TypeError, "Namespace name must be str,"
                 " not %R", name);
    return NULL;
  }

  if (parent == Py_None)
    parent = NULL;

  if (parent && ! KosNamespace_Check(parent)) {
    PyErr_Format(PyExc_TypeError, "Namespace parent must be Namespace,"
                 " not %R", name);
    return NULL;
  }

  size_t count = 0;

  if (local_syms) {
    local_syms = PySequence_Fast(local_syms, "local_syms must"
                                 " be a sequence");
    if (! local_syms)
      return NULL;

    count += PySequence_Fast_GET_SIZE(local_syms);
  }

  if (closure_syms) {
    closure_syms = PySequence_Fast(closure_syms, "closure_syms must"
                                   " be a sequence");
    if (! closure_syms) {
      Py_XDECREF(local_syms);
      return NULL;
    }

    count += PySequence_Fast_GET_SIZE(closure_syms);
  }

  PyObject *binds = PyDict_New();
  if (! binds) {
    Py_XDECREF(local_syms);
    Py_XDECREF(closure_syms);
    return NULL;
  }

  PyObject *self = PyObject_NewVar(PyObject, type, count);
  if (! self) {
    Py_XDECREF(local_syms);
    Py_XDECREF(closure_syms);
    Py_DECREF(binds);
    return NULL;
  }

  Py_INCREF(name);
  KosNamespace_NAME(self) = name;

  Py_XINCREF(parent);
  KosNamespace_PARENT(self) = parent;

  // stolen ref
  KosNamespace_BINDS(self) = binds;

  KosNamespace_FAST_COUNT(self) = count;

  // safety zero the fast namebinds
  for (size_t i = count; i--; ) {
    KosNameBind *nb = KosNamespace_GET_FAST(self, i);
    KosNameBind_NAME(nb) = NULL;
    KosNameBind_HELD(nb) = NULL;
    KosNameBind_RAW(nb) = 1;
  }

  if(ns_init_syms(self, parent, local_syms, closure_syms))
    Py_CLEAR(self);

  Py_XDECREF(local_syms);
  Py_XDECREF(closure_syms);

  return self;
}


static PyObject *ns_new(PyTypeObject *type,
                        PyObject *args, PyObject *kwds) {

  PyObject *name = NULL;
  PyObject *parent = NULL;
  PyObject *local_syms = NULL;
  PyObject *closure_syms = NULL;

  if (kwds && PyDict_Size(kwds)) {
    PyErr_SetString(PyExc_TypeError, "Namespace takes no named arguments");
    return NULL;
  }

  if (! PyArg_ParseTuple(args, "|OOOO:Namespace",
                         &name, &parent, &local_syms, &closure_syms))
    return NULL;

  if (!name || name == Py_None) {
    name = NULL;

  } else if (! PyUnicode_Check(name)) {
    PyErr_Format(PyExc_TypeError, "Namespace name must be str, not %R", name);
    return NULL;
  }

  if (!parent || parent == Py_None) {
    parent = NULL;

  } else if (! KosNamespace_Check(parent)) {
    PyErr_Format(PyExc_TypeError, "Namespace parent must be Namespace, not %R",
                 parent);
    return NULL;
  }

  return ns_newfull_type(type, name, parent, local_syms, closure_syms);
}


static void ns_dealloc(PyObject *self) {
  // SAYF("ns_dealloc(%R)", self);

  Py_CLEAR(KosNamespace_NAME(self));
  Py_CLEAR(KosNamespace_PARENT(self));
  Py_CLEAR(KosNamespace_BINDS(self));

  for (size_t i = KosNamespace_FAST_COUNT(self); i--; ) {
    KosNameBind *nb = KosNamespace_GET_FAST(self, i);
    Py_CLEAR(KosNameBind_NAME(nb));
    Py_CLEAR(KosNameBind_HELD(nb));
    KosNameBind_RAW(nb) = 1;
  }
  KosNamespace_FAST_COUNT(self) = 0;

  Py_TYPE(self)->tp_free(self);
}


static int ns_traverse(PyObject *self, visitproc visit, void *arg) {
  Py_VISIT(KosNamespace_NAME(self));
  Py_VISIT(KosNamespace_PARENT(self));
  Py_VISIT(KosNamespace_BINDS(self));

  for (size_t i = KosNamespace_FAST_COUNT(self); i--; ) {
    KosNameBind *nb = KosNamespace_GET_FAST(self, i);
    Py_VISIT(KosNameBind_NAME(nb));
    Py_VISIT(KosNameBind_HELD(nb));
  }

  return 0;
}


static int ns_clear(PyObject *self) {
  Py_CLEAR(KosNamespace_NAME(self));
  Py_CLEAR(KosNamespace_PARENT(self));
  Py_CLEAR(KosNamespace_BINDS(self));

  for (size_t i = KosNamespace_FAST_COUNT(self); i--; ) {
    KosNameBind *nb = KosNamespace_GET_FAST(self, i);
    Py_CLEAR(KosNameBind_NAME(nb));
    Py_CLEAR(KosNameBind_HELD(nb));
    KosNameBind_RAW(nb) = 1;
  }
  KosNamespace_FAST_COUNT(self) = 0;

  return 0;
}


PyTypeObject KosNamespaceType = {
  PyVarObject_HEAD_INIT(NULL, 0)

  "Namespace",
  sizeof(KosNamespace),
  sizeof(KosNameBind),

  .tp_flags = Py_TPFLAGS_DEFAULT,
  .tp_methods = ns_methods,
  .tp_members = ns_members,
  .tp_new = ns_new,
  .tp_dealloc = ns_dealloc,
  .tp_traverse = ns_traverse,
  .tp_clear = ns_clear,
  .tp_as_mapping = &ns_as_mapping,
  .tp_as_sequence = &ns_seq,
  // .tp_repr = ns_repr,
};


PyObject *KosNamespace_New(PyObject *parent, PyObject *spec) {
  return KosNamespace_NewFull(KosNamespaceSpec_NAME(spec),
                              parent,
                              KosNamespaceSpec_LOCAL_SYMS(spec),
                              KosNamespaceSpec_CLOSURE_SYMS(spec));
}


PyObject *KosNamespace_NewFull(
    PyObject *name,
    PyObject *parent,
    PyObject *local_syms,
    PyObject *closure_syms) {

  return ns_newfull_type(&KosNamespaceType,
                         name, parent,
                         local_syms, closure_syms);
}


PyObject *KosNamespace_LOOSE_REF(PyObject *namespace, PyObject *key) {
  if (KosNamespace_CheckExact(namespace) ||
      KosCollectorNamespace_CheckExact(namespace)) {
    return ns_borrow_loose_ref(namespace, key);

  } else if (KosModuleNamespace_CheckExact(namespace)) {
    return mdl_ns_borrow_ref(namespace, key);

  } else {
    return NULL;
  }
}


PyObject *KosNamespace_REF(PyObject *namespace, PyObject *key) {
  if (KosNamespace_CheckExact(namespace) ||
      KosCollectorNamespace_CheckExact(namespace)) {
    return ns_borrow_ref(namespace, key);

  } else if (KosModuleNamespace_CheckExact(namespace)) {
    return mdl_ns_borrow_ref(namespace, key);

  } else {
    return NULL;
  }
}


PyObject *KosNamespace_Ref(PyObject *namespace, PyObject *key) {
  PyObject *result = KosNamespace_REF(namespace, key);

  if (! result && ! PyErr_Occurred())
    PyErr_Format(PyExc_NameError, "no such reference: %R", key);

  return result;
}


PyObject *KosNamespace_Deref(PyObject *namespace, PyObject *key) {
  PyObject *ref = KosNamespace_Ref(namespace, key);
  return ref? KosRef_Deref(ref): NULL;
}


int KosNamespace_FindRef(PyObject *namespace, PyObject *key,
                         PyObject **found) {

  // SAYF("KosNamespace_FindRef(%R, %R)", namespace, key);

  while (namespace) {
    if (! KosNamespace_Check(namespace)) {
      PyErr_Format(PyExc_TypeError, "expected Namespace instance,"
                   " found %R", namespace);
      return -1;
    }

    if (! KosCollectorNamespace_Check(namespace)) {
      // the collectornamespace doesn't allow you to take a closure
      // over its references. The rest can be searched though.

      PyObject *seek = KosNamespace_REF(namespace, key);
      if (! seek) {
        if (PyErr_Occurred())
          return -1;
      } else {
        *found = seek;
        return 0;
      }
    }

    namespace = KosNamespace_PARENT(namespace);
  }

  // SAYF("KosNamespace_FindRef -> %R", result);
  return 0;
}


int KosNamespace_Contains(PyObject *namespace, PyObject *key) {
  PyObject *ref = NULL;

  if (KosNamespace_Check(namespace)) {
    ref = KosNamespace_REF(namespace, key);

  } else {
    PyErr_Format(PyExc_TypeError, "expected Namespace instance,"
                 " found %R", namespace);
    return -1;
  }

  if (! ref && PyErr_Occurred()) {
    return -1;
  } else {
    return !!ref;
  }
}


int KosNamespace_Store(
    PyObject *namespace,
    PyObject *key,
    PyObject *val) {

  return ns_store_symbol(namespace, key, val);
}


int KosNamespace_Clear(PyObject *namespace, PyObject *key) {

  return ns_store_symbol(namespace, key, NULL);
}


int KosNamespace_Bind(
    PyObject *namespace,
    PyObject *key,
    PyObject *ref) {

  if (! KosNamespace_Check(namespace)) {
    PyErr_Format(PyExc_TypeError, "expected Namespace instance,"
                 " found %R", namespace);
    return -1;
  }

  return ns_bind_ref(namespace, key, ref);
}


int KosNamespace_FastIndex(PyObject *namespace, PyObject *key) {
  if (! PyUnicode_Check(key)) {
    PyErr_Format(PyExc_TypeError, "expected str, got %R",
                 key);
    return -1;
  }

  return ns_fast_index(namespace, key);
}


PyObject *KosNamespace_FastDeref(PyObject *namespace, size_t index) {
  if (index >= KosNamespace_FAST_COUNT(namespace)) {
    PyErr_Format(PyExc_NameError, "Namespace fast index out of bounds, %i",
                 index);
    return NULL;
  }

  KosNameBind *nb = KosNamespace_GET_FAST(namespace, index);

  if (KosNameBind_RAW(nb)) {
    PyObject *value = KosNameBind_HELD(nb);
    if (! value) {
      PyErr_Format(PyExc_NameError, "Empty reference: %S", KosNameBind_NAME(nb));
    } else {
      Py_INCREF(value);
    }
    return value;

  } else {
    return KosRef_Deref(KosNameBind_HELD(nb));
  }
}


PyObject *KosNamespace_FastRef(PyObject *namespace, size_t index) {
  if (index >= KosNamespace_FAST_COUNT(namespace)) {
    PyErr_Format(PyExc_NameError, "Namespace fast index out of bounds, %zu",
                 index);
    return NULL;
  }

  KosNameBind *nb = KosNamespace_GET_FAST(namespace, index);
  if (KosNameBind_RAW(nb)) {
    PyObject *ref = KosSymbolicRef_New(KosNameBind_NAME(nb), NULL);
    if (! ref)
      return NULL;

    KosSymbolicRef_VALUE(ref) = KosNameBind_HELD(nb);
    KosNameBind_HELD(nb) = ref;
    KosNameBind_RAW(nb) = 0;

    if (PyDict_SetItem(KosNamespace_BINDS(namespace), KosNameBind_NAME(nb), ref)) {
      return NULL;
    }
  }

  return KosNameBind_HELD(nb);
}


int KosNamespace_FastStore(
    PyObject *namespace,
    size_t index,
    PyObject *value) {

  if (index >= KosNamespace_FAST_COUNT(namespace)) {
    PyErr_Format(PyExc_NameError, "Namespace fast index out of bounds, %i",
                 index);
    return -1;
  }

  KosNameBind *nb = KosNamespace_GET_FAST(namespace, index);
  if (KosNameBind_RAW(nb)) {
    Py_INCREF(value);
    Py_CLEAR(KosNameBind_HELD(nb));
    KosNameBind_HELD(nb) = value;
    return 0;

  } else {
    return KosRef_Store(KosNameBind_HELD(nb), value);
  }
}


int KosNamespace_FastClear(PyObject *namespace, size_t index) {
  if (index >= KosNamespace_FAST_COUNT(namespace)) {
    PyErr_Format(PyExc_NameError, "Namespace fast index out of bounds, %i",
                 index);
    return -1;
  }

  KosNameBind *nb = KosNamespace_GET_FAST(namespace, index);
  if (KosNameBind_RAW(nb)) {
    Py_CLEAR(KosNameBind_HELD(nb));
    return 0;

  } else {
    return KosRef_Clear(KosNameBind_HELD(nb));
  }
}


int KosNamespace_FastBind(
    PyObject *namespace,
    size_t index,
    PyObject *ref) {

  if (index >= KosNamespace_FAST_COUNT(namespace)) {
    PyErr_Format(PyExc_NameError, "Namespace fast index out of bounds, %i",
                 index);
    return -1;
  }

  if (! KosRef_Check(ref)) {
    PyErr_Format(PyExc_TypeError, "Namespace bindings must be reference,"
                 " not %R", ref);
    return -1;
  }

  KosNameBind *nb = KosNamespace_GET_FAST(namespace, index);

  Py_INCREF(ref);
  Py_CLEAR(KosNameBind_HELD(nb));
  KosNameBind_HELD(nb) = ref;
  KosNameBind_RAW(nb) = 0;

  return PyDict_SetItem(KosNamespace_BINDS(namespace), KosNameBind_NAME(nb), ref);
}


/* === CollectorNamespace === */


PyTypeObject KosCollectorNamespaceType = {
  PyVarObject_HEAD_INIT(NULL, 0)

  "CollectorNamespace",
  sizeof(KosCollectorNamespace),
  0,

  .tp_base = &KosNamespaceType,
  .tp_flags = Py_TPFLAGS_DEFAULT,
  // .tp_repr = ns_repr,
};



PyObject *KosCollectorNamespace_New(
    PyObject *parent,
    PyObject *spec) {

  return KosCollectorNamespace_NewFull(KosNamespaceSpec_NAME(spec),
                                       parent,
                                       KosNamespaceSpec_LOCAL_SYMS(spec),
                                       KosNamespaceSpec_CLOSURE_SYMS(spec));
}


PyObject *KosCollectorNamespace_NewFull(
    PyObject *name,
    PyObject *parent,
    PyObject *local_syms,
    PyObject *closure_syms) {

  return ns_newfull_type(&KosCollectorNamespaceType,
                         name, parent,
                         local_syms, closure_syms);
}


/* === ModuleNamespace === */


static PyObject *mdl_ns_borrow_ref(PyObject *self, PyObject *name) {

  // SAYF("mdl_ns_borrow_ref(%R, %R)", self, name);
  PyObject *res = PyDict_GetItem(KosNamespace_BINDS(self), name);

  if (! res) {
    PyObject *blt = KosModuleNamespace_BUILTINS(self);
    PyObject *val = NULL;
    if (blt)
      val = PyDict_GetItem(blt, name);

    res = KosRef_New(name, val);

    PyDict_SetItem(KosNamespace_BINDS(self), name, res);
    Py_DECREF(res);
  }

  // SAYF("mdl_ns_borrow_ref -> %R", res);
  return res;
}


static PyObject *mdl_ns_new(
    PyTypeObject *type,
    PyObject *args,
    PyObject *kwds) {

  PyObject *name = NULL;
  PyObject *builtins = NULL;

  if (kwds && PyDict_Size(kwds)) {
    PyErr_SetString(PyExc_TypeError, "ModuleNamespace takes no"
                    " named arguments");
    return NULL;
  }

  if (! PyArg_ParseTuple(args, "OO:ModuleNamespace", &name, &builtins))
    return NULL;

  return KosModuleNamespace_New(name, builtins);
}


static void mdl_ns_dealloc(PyObject *self) {
  // SAYF("mdl_ns_dealloc(%R)", self);
  Py_CLEAR(KosModuleNamespace_BUILTINS(self));
  ns_dealloc(self);
}


static int mdl_ns_traverse(PyObject *self, visitproc visit, void *arg) {
  Py_VISIT(KosModuleNamespace_BUILTINS(self));
  return ns_traverse(self, visit, arg);
}


static int mdl_ns_clear(PyObject *self) {
  Py_CLEAR(KosModuleNamespace_BUILTINS(self));
  return ns_clear(self);
}


PyTypeObject KosModuleNamespaceType = {
  PyVarObject_HEAD_INIT(NULL, 0)

  "ModuleNamespace",
  sizeof(KosModuleNamespace),
  0,

  .tp_base = &KosNamespaceType,
  .tp_flags = Py_TPFLAGS_DEFAULT,
  .tp_new = mdl_ns_new,
  .tp_dealloc = mdl_ns_dealloc,
  .tp_traverse = mdl_ns_traverse,
  .tp_clear = mdl_ns_clear,
  // .tp_repr = ns_repr,
};


PyObject *KosModuleNamespace_New(PyObject *name, PyObject *builtins) {
  // SAYF("KosModuleNamespace_New(%R, %R)", name, builtins);

  if (! name || name == Py_None)
    name = _module_str;

  if (! PyUnicode_Check(name)) {
    PyErr_Format(PyExc_TypeError, "Namespace name must be str,"
                 " not %R", name);
    return NULL;
  }

  if (! PyDict_Check(builtins)) {
    PyErr_Format(PyExc_TypeError, "ModuleNamespace builtins must be a"
                 " mapping, not %R", builtins);
    return NULL;
  }

  PyObject *binds = PyDict_New();
  if (! binds)
    return NULL;

  PyObject *self = PyObject_New(PyObject, &KosModuleNamespaceType);

  Py_INCREF(name);
  KosNamespace_NAME(self) = name;

  KosNamespace_PARENT(self) = NULL;
  KosNamespace_BINDS(self) = binds;
  KosNamespace_FAST_COUNT(self) = 0;

  Py_INCREF(builtins);
  KosModuleNamespace_BUILTINS(self) = builtins;

  // SAYF("KosModuleNamespace_New -> %R", self);
  return self;
}


/* === Module === */


static PyObject *mdl_get_name(PyObject *self, void *_unused) {
  PyObject *ns = KosModule_NAMESPACE(self);
  PyObject *name = KosNamespace_NAME(ns);
  Py_INCREF(name);
  return name;
}


static PyObject *mdl_get_namespace(PyObject *self, void *_unused) {
  PyObject *ns = KosModule_NAMESPACE(self);
  Py_INCREF(ns);
  return ns;
}


static PyGetSetDef mdl_getset[] = {

  { "__name__", mdl_get_name, NULL,
    "str; name of the underlying namespace",
    NULL },

  { "__dict__",  mdl_get_namespace, NULL,
    "Namespace; the namespace backing this module",
    NULL },

  { NULL, },
};


static PyObject *mdl_m_dir(PyObject *self) {
  PyObject *ns = KosModule_NAMESPACE(self);
  return PyDict_Keys(KosNamespace_BINDS(ns));
}


static PyMethodDef mdl_methods[] = {

  { "__dir__", (PyCFunction) mdl_m_dir, METH_NOARGS,
    "M.__dir__() => List[str]", },

  { NULL, },
};


static PyObject *mdl_getattro(PyObject *self, PyObject *name) {
  if (! PyUnicode_CheckExact(name)) {
    PyErr_Format(PyExc_TypeError, "attribute name must be a str, not %R",
                 name);
    return NULL;
  }

  PyObject *descr = PyType_Lookup(&KosModuleType, name);
  if (descr) {
    descrgetfunc fun = Py_TYPE(descr)->tp_descr_get;
    if (fun)
      return fun(descr, self, (PyObject *) &KosModuleType);
  }

  PyObject *ref = KosNamespace_REF(KosModule_NAMESPACE(self), name);
  if (! ref || KosRef_IsEmpty(ref)) {
    if (! PyErr_Occurred())
      PyErr_Format(PyExc_AttributeError, "%R object has no attribute %R",
                   self, name);
    return NULL;
  }

  return KosRef_Deref(ref);
}


static int mdl_setattro(PyObject *self, PyObject *name, PyObject *value) {
  if (! PyUnicode_CheckExact(name)) {
    PyErr_Format(PyExc_TypeError, "attribute name must be a str, not %R",
                 name);
    return -1;
  }

  PyObject *descr = PyType_Lookup(&KosModuleType, name);
  if (descr) {
    descrsetfunc fun = Py_TYPE(descr)->tp_descr_set;
    if (fun) {
      return fun(descr, self, value);

    } else {
      PyErr_Format(PyExc_AttributeError, "%R object attribute %R is read-only",
                   self, value);
      return -1;
    }
  }

  PyObject *ref = KosNamespace_REF(KosModule_NAMESPACE(self), name);
  if (! ref) {
    if (! PyErr_Occurred())
      PyErr_Format(PyExc_AttributeError, "%R object has no attribute %R",
                   self, name);
    return -1;
  }

  return value? KosRef_Store(ref, value): KosRef_Clear(ref);
}


static PyObject *mdl_new(PyTypeObject *type,
                         PyObject *args, PyObject *kwds) {

  PyObject *namespace = NULL;

  if (kwds && PyDict_Size(kwds)) {
    PyErr_SetString(PyExc_TypeError, "Module takes no named arguments");
    return NULL;
  }

  if (! PyArg_ParseTuple(args, "O:Module", &namespace))
    return NULL;

  return KosModule_New(namespace);
}


static void mdl_dealloc(PyObject *self) {
  Py_CLEAR(KosModule_NAMESPACE(self));
  Py_TYPE(self)->tp_free(self);
}


static int mdl_traverse(PyObject *self, visitproc visit, void *arg) {
  Py_VISIT(KosModule_NAMESPACE(self));
  return 0;
}


static int mdl_clear(PyObject *self) {
  Py_CLEAR(KosModule_NAMESPACE(self));
  return 0;
}


PyTypeObject KosModuleType = {
  PyVarObject_HEAD_INIT(NULL, 0)

  "Module",
  sizeof(KosModule),
  0,

  .tp_flags = Py_TPFLAGS_DEFAULT,
  .tp_new = mdl_new,
  .tp_dealloc = mdl_dealloc,
  .tp_traverse = mdl_traverse,
  .tp_clear = mdl_clear,
  .tp_getset = mdl_getset,
  .tp_methods = mdl_methods,
  .tp_getattro = mdl_getattro,
  .tp_setattro = mdl_setattro,
  // .tp_repr = ns_repr,
};


PyObject *KosModule_New(PyObject *namespace) {

  // SAYF("KosModule_New(%R)", namespace);

  if (! KosNamespace_Check(namespace)) {
    PyErr_Format(PyExc_TypeError, "Module's namespace must be a Namespace,"
                 " not %R", namespace);
    return NULL;
  }

  PyObject *self = PyObject_New(PyObject, &KosModuleType);
  if (! self)
    return NULL;

  Py_INCREF(namespace);
  KosModule_NAMESPACE(self) = namespace;

  return self;
}


/* === Unit lifecycle === */


int koskari_namespace_init(PyObject *mod) {
  if (! mod)
    return -1;

  PyObject *dict = PyModule_GetDict(mod);
  if (! dict)
    return -1;

  if (PyType_Ready(&KosNamespaceType) ||
      PyType_Ready(&KosNamespaceSpecType) ||
      PyType_Ready(&KosCollectorNamespaceType) ||
      PyType_Ready(&KosModuleNamespaceType) ||
      PyType_Ready(&KosModuleType))
    return -1;

  STR_CONST(_namespace_str, "<namespace>");
  STR_CONST(_module_str, "<module>");

  PyDict_SetItemString(dict, "Namespace",
                       (PyObject *) &KosNamespaceType);

  PyDict_SetItemString(dict, "NamespaceSpec",
                       (PyObject *) &KosNamespaceSpecType);

  PyDict_SetItemString(dict, "CollectorNamespace",
                       (PyObject *) &KosCollectorNamespaceType);

  PyDict_SetItemString(dict, "ModuleNamespace",
                       (PyObject *) &KosModuleNamespaceType);

  PyDict_SetItemString(dict, "Module",
                       (PyObject *) &KosModuleType);

  return 0;
}


int koskari_namespace_clear(PyObject *mod) {

  Py_CLEAR(_namespace_str);
  Py_CLEAR(_module_str);

  return 0;
}


// The end.
