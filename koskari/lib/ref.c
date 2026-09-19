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
  koskari.lib - ref module

  Reference types for Koskari

  author: Christopher O'Brien <obriencj@gmail.com>
  license: GPL v.3
*/


#include "lib.h"
#include "hints.h"
#include <weakrefobject.h>


/* === String constants === */


static PyObject *_str_ref = NULL;        // "<Ref>"
static PyObject *_str_attr_ref = NULL;   // "<AttributeRef>"
static PyObject *_str_item_ref = NULL;   // "<ItemRef>"
static PyObject *_str_dyn_ref = NULL;    // "<DynamicRef>"
static PyObject *_str_const_ref = NULL;  // "<ConstantRef>"
static PyObject *_str_memo_ref = NULL;   // "<MemoizedRef>"
static PyObject *_str_param_ref = NULL;  // "<ParamRef>"
static PyObject *_str_weak_ref = NULL;   // "<WeakRef>"

static PyObject *_str_deref = NULL;  // "deref"
static PyObject *_str_store = NULL;  // "store"
static PyObject *_str_clear = NULL;  // "clear"
static PyObject *_str_empty = NULL;  // "empty"


static int ref_nb_bool(PyObject *self);


/* === Ref (base symbolic reference) === */


static int sym_ref_init(PyObject *self, PyObject *name, PyObject *value) {

  if ((! name) || Py_None == name) {
    name = _str_ref;
    Py_INCREF(name);
  } else {
    name = kos_key_to_str(name);
    if (! name)
      return -1;
  }
  KosRef_NAME(self) = name;

  Py_XINCREF(value);
  KosSymbolicRef_VALUE(self) = value;

  return 0;
}


static PyObject *sym_ref_get_name(PyObject *self, void *_unused) {
  PyObject *name = KosRef_NAME(self);

  if (! name)
    name = _str_ref;

  Py_INCREF(name);
  return name;
}


static PyObject *sym_ref_get_empty(PyObject *self, void *_unused) {
  return PyBool_FromLong(KosSymbolicRef_IS_EMPTY(self));
}


static PyGetSetDef sym_ref_getset[] = {
  { "empty", sym_ref_get_empty, NULL,
    "bool; whether this reference is empty",
    NULL },

  { "name", sym_ref_get_name, NULL,
    "str; name of this reference",
    NULL },

  { NULL, },
};


static PyObject *sym_ref_m_deref(PyObject *self) {
  PyObject *value = KosSymbolicRef_VALUE(self);
  if (! value) {
    PyErr_Format(PyExc_NameError, "Empty reference: %S", KosRef_NAME(self));
    return NULL;

  } else {
    Py_INCREF(value);
    return value;
  }
}


static PyObject *sym_ref_m_store(PyObject *self, PyObject *value) {
  Py_XINCREF(value);
  Py_XDECREF(KosSymbolicRef_VALUE(self));
  KosSymbolicRef_VALUE(self) = value;
  Py_RETURN_NONE;
}


static PyObject *sym_ref_m_clear(PyObject *self) {
  Py_CLEAR(KosSymbolicRef_VALUE(self));
  Py_RETURN_NONE;
}


static PyObject *m_deref(PyObject *mod, PyObject *ref) {
  (void) mod;
  return PyObject_CallMethod(ref, "deref", NULL);
}


static PyMethodDef ref_mod_methods[] = {
  { "deref", (PyCFunction) m_deref, METH_O,
    "deref(ref) => value\n"
    "Call ref.deref() to obtain the value held by the reference." },
  { NULL, },
};


static PyMethodDef sym_ref_methods[] = {
  { "deref", (PyCFunction) sym_ref_m_deref, METH_NOARGS,
    "R.deref() => V" },

  { "store", (PyCFunction) sym_ref_m_store, METH_O,
    "R.store(V) => None" },

  { "clear", (PyCFunction) sym_ref_m_clear, METH_NOARGS,
    "R.clear() => None" },

  { NULL, },
};


static int sym_ref_tp_init(PyObject *self, PyObject *args, PyObject *kwds) {

  PyObject *name = NULL;
  PyObject *val = NULL;

  if (kwds && PyDict_Size(kwds)) {
    PyErr_SetString(PyExc_TypeError, "Ref takes no named arguments");
    return -1;
  }

  if (! PyArg_ParseTuple(args, "|OO:Ref", &name, &val))
    return -1;

  return sym_ref_init(self, name, val);
}


static int sym_ref_traverse(PyObject *self, visitproc visit, void *arg) {
  Py_VISIT(KosRef_NAME(self));
  Py_VISIT(KosSymbolicRef_VALUE(self));
  return 0;
}


static int sym_ref_clear(PyObject *self) {
  Py_CLEAR(KosRef_NAME(self));
  Py_CLEAR(KosSymbolicRef_VALUE(self));
  return 0;
}


static void sym_ref_dealloc(PyObject *self) {
  PyObject_GC_UnTrack(self);
  Py_CLEAR(KosRef_NAME(self));
  Py_CLEAR(KosSymbolicRef_VALUE(self));
  Py_TYPE(self)->tp_free(self);
}


static PyNumberMethods ref_as_number = {
  .nb_bool = (inquiry) ref_nb_bool,
};


/**
   Shared repr for every ref kind: the type's short name and the
   ref's own name. The held value is never touched -- a deref can
   raise, and reprs stay safe.
*/
static PyObject *ref_generic_repr(PyObject *self) {

  const char *tn = Py_TYPE(self)->tp_name;
  const char *dot = strrchr(tn, '.');

  return PyUnicode_FromFormat("<koskari %s %R at %p>",
                              dot ? dot + 1 : tn,
                              KosRef_NAME(self), self);
}


PyTypeObject KosRefType = {
  PyVarObject_HEAD_INIT(NULL, 0)

  "koskari.lib.Ref",
  sizeof(KosRef),
  0,

  .tp_flags = (Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE
               | Py_TPFLAGS_HAVE_GC),
  .tp_repr = ref_generic_repr,
  .tp_free = PyObject_GC_Del,
  .tp_getset = sym_ref_getset,
  .tp_methods = sym_ref_methods,
  .tp_as_number = &ref_as_number,
  .tp_new = PyType_GenericNew,
  .tp_init = sym_ref_tp_init,
  .tp_traverse = sym_ref_traverse,
  .tp_clear = sym_ref_clear,
  .tp_dealloc = sym_ref_dealloc,
};


PyObject *KosSymbolicRef_New(PyObject *name, PyObject *value) {
  PyObject *self = PyObject_GC_New(PyObject, &KosRefType);
  if (! self)
    return NULL;

  /* GC_New does not zero, and init can fail before assigning either
     field; the failure path clears both */
  KosRef_NAME(self) = NULL;
  KosSymbolicRef_VALUE(self) = NULL;

  if (sym_ref_init(self, name, value))
    Py_CLEAR(self);

  if (self)
    PyObject_GC_Track(self);
  return self;
}


/* === AttributeRef === */


static int attr_ref_init(PyObject *self, PyObject *name,
                         PyObject *obj, PyObject *attr) {

  KosRef_NAME(self) = NULL;
  KosAttributeRef_OBJ(self) = NULL;
  KosAttributeRef_ATTR(self) = NULL;

  if (! PyUnicode_CheckExact(attr)) {
    PyErr_Format(PyExc_TypeError,
                 "attribute name must be string, not '%.200s'",
                 attr? Py_TYPE(attr)->tp_name: "NULL");
    return -1;
  }

  if (! name || name == Py_None) {
    name = _str_attr_ref;
    Py_INCREF(name);
  } else {
    name = kos_key_to_str(name);
    if (! name)
      return -1;
  }
  KosRef_NAME(self) = name;

  Py_INCREF(obj);
  KosAttributeRef_OBJ(self) = obj;

  Py_INCREF(attr);
  KosAttributeRef_ATTR(self) = attr;

  return 0;
}


static PyObject *attr_ref_get_obj(PyObject *self, void *_unused) {
  PyObject *obj = KosAttributeRef_OBJ(self);
  Py_INCREF(obj);
  return obj;
}


static PyObject *attr_ref_get_attr(PyObject *self, void *_unused) {
  PyObject *attr = KosAttributeRef_ATTR(self);
  Py_INCREF(attr);
  return attr;
}


static PyObject *attr_ref_get_empty(PyObject *self, void *_unused) {
  int empty = PyObject_HasAttr(KosAttributeRef_OBJ(self),
                               KosAttributeRef_ATTR(self));

  if (empty == -1) {
    return NULL;
  } else {
    return PyBool_FromLong(! empty);
  }
}


static PyGetSetDef attr_ref_getset[] = {
  { "empty", attr_ref_get_empty, NULL,
    "bool; whether this reference is empty",
    NULL },

  { "obj", attr_ref_get_obj, NULL,
    "object; object to reference attribute from",
    NULL },

  { "attr", attr_ref_get_attr, NULL,
    "str; attribute name",
    NULL },

  { NULL, },
};


static PyObject *attr_ref_m_deref(PyObject *self) {
  return PyObject_GetAttr(KosAttributeRef_OBJ(self),
                          KosAttributeRef_ATTR(self));
}


static PyObject *attr_ref_m_store(PyObject *self, PyObject *value) {
  int res = PyObject_SetAttr(KosAttributeRef_OBJ(self),
                             KosAttributeRef_ATTR(self),
                             value);

  if (res) {
    return NULL;
  } else {
    Py_RETURN_NONE;
  }
}


static PyObject *attr_ref_m_clear(PyObject *self) {
  int res = PyObject_DelAttr(KosAttributeRef_OBJ(self),
                             KosAttributeRef_ATTR(self));

  if (res) {
    return NULL;
  } else {
    Py_RETURN_NONE;
  }
}


static PyMethodDef attr_ref_methods[] = {
  { "deref", (PyCFunction) attr_ref_m_deref, METH_NOARGS,
    "R.deref() => V" },

  { "store", (PyCFunction) attr_ref_m_store, METH_O,
    "R.store(V) => None" },

  { "clear", (PyCFunction) attr_ref_m_clear, METH_NOARGS,
    "R.clear() => None" },

  { NULL, },
};


static int attr_ref_tp_init(PyObject *self, PyObject *args, PyObject *kwds) {

  PyObject *name = NULL;
  PyObject *obj = NULL;
  PyObject *attr = NULL;

  if (kwds && PyDict_Size(kwds)) {
    PyErr_SetString(PyExc_TypeError, "AttributeRef takes no named arguments");
    return -1;
  }

  if (! PyArg_ParseTuple(args, "OOO:AttributeRef", &name, &obj, &attr))
    return -1;

  return attr_ref_init(self, name, obj, attr);
}


static int attr_ref_traverse(PyObject *self, visitproc visit, void *arg) {
  Py_VISIT(KosRef_NAME(self));
  Py_VISIT(KosAttributeRef_OBJ(self));
  Py_VISIT(KosAttributeRef_ATTR(self));
  return 0;
}


static int attr_ref_clear(PyObject *self) {
  Py_CLEAR(KosRef_NAME(self));
  Py_CLEAR(KosAttributeRef_OBJ(self));
  Py_CLEAR(KosAttributeRef_ATTR(self));
  return 0;
}


static void attr_ref_dealloc(PyObject *self) {
  PyObject_GC_UnTrack(self);
  Py_CLEAR(KosRef_NAME(self));
  Py_CLEAR(KosAttributeRef_OBJ(self));
  Py_CLEAR(KosAttributeRef_ATTR(self));
  Py_TYPE(self)->tp_free(self);
}


PyTypeObject KosAttributeRefType = {
  PyVarObject_HEAD_INIT(NULL, 0)

  "koskari.lib.AttributeRef",
  sizeof(KosAttributeRef),
  0,

  .tp_base = &KosRefType,
  .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC,
  .tp_repr = ref_generic_repr,
  .tp_getset = attr_ref_getset,
  .tp_methods = attr_ref_methods,
  .tp_new = PyType_GenericNew,
  .tp_init = attr_ref_tp_init,
  .tp_traverse = attr_ref_traverse,
  .tp_clear = attr_ref_clear,
  .tp_dealloc = attr_ref_dealloc,
};


PyObject *KosAttributeRef_New(PyObject *name,
                              PyObject *obj, PyObject *attr) {

  PyObject *self = PyObject_GC_New(PyObject, &KosAttributeRefType);
  if (! self)
    return NULL;

  if(attr_ref_init(self, name, obj, attr))
    Py_CLEAR(self);

  if (self)
    PyObject_GC_Track(self);
  return self;
}


/* === ItemRef === */


static int item_ref_init(PyObject *self, PyObject *name,
                         PyObject *obj, PyObject *key) {

  KosRef_NAME(self) = NULL;
  KosItemRef_OBJ(self) = NULL;
  KosItemRef_KEY(self) = NULL;

  if (! name || name == Py_None) {
    name = _str_item_ref;
    Py_INCREF(name);
  } else {
    name = kos_key_to_str(name);
    if (! name)
      return -1;
  }
  KosRef_NAME(self) = name;

  Py_INCREF(obj);
  KosItemRef_OBJ(self) = obj;

  Py_INCREF(key);
  KosItemRef_KEY(self) = key;

  return 0;
}


static PyObject *item_ref_get_obj(PyObject *self, void *_unused) {
  PyObject *obj = KosItemRef_OBJ(self);
  Py_INCREF(obj);
  return obj;
}


static PyObject *item_ref_get_key(PyObject *self, void *_unused) {
  PyObject *attr = KosItemRef_KEY(self);
  Py_INCREF(attr);
  return attr;
}


static PyObject *item_ref_get_empty(PyObject *self, void *_unused) {
  int haskey = -1;
  PyObject *obj = KosItemRef_OBJ(self);

  if (PyMapping_Check(obj)) {
    haskey = PyMapping_HasKeyWithError(obj, KosItemRef_KEY(self));

  } else {
    PyObject *res = PyObject_GetItem(obj, KosItemRef_KEY(self));
    if (res) {
      haskey = 1;
      Py_DECREF(res);
    } else {
      haskey = 0;
      PyErr_Clear();
    }
  }

  if (haskey == -1) {
    PyErr_Clear();
    haskey = 0;
  }

  return PyBool_FromLong(! haskey);
}


static PyGetSetDef item_ref_getset[] = {
  { "empty", item_ref_get_empty, NULL,
    "bool; whether this reference is empty",
    NULL },

  { "obj", item_ref_get_obj, NULL,
    "object; object to reference item from",
    NULL },

  { "key", item_ref_get_key, NULL,
    "object; item key",
    NULL },

  { NULL, },
};


static PyObject *item_ref_m_deref(PyObject *self) {
  return PyObject_GetItem(KosItemRef_OBJ(self),
                          KosItemRef_KEY(self));
}


static PyObject *item_ref_m_store(PyObject *self, PyObject *value) {
  int res = PyObject_SetItem(KosItemRef_OBJ(self),
                             KosItemRef_KEY(self),
                             value);

  if (res) {
    return NULL;
  } else {
    Py_RETURN_NONE;
  }
}


static PyObject *item_ref_m_clear(PyObject *self) {
  int res = PyObject_DelItem(KosItemRef_OBJ(self),
                             KosItemRef_KEY(self));

  if (res) {
    return NULL;
  } else {
    Py_RETURN_NONE;
  }
}


static PyMethodDef item_ref_methods[] = {
  { "deref", (PyCFunction) item_ref_m_deref, METH_NOARGS,
    "R.deref() => V" },

  { "store", (PyCFunction) item_ref_m_store, METH_O,
    "R.store(V) => None" },

  { "clear", (PyCFunction) item_ref_m_clear, METH_NOARGS,
    "R.clear() => None" },

  { NULL, },
};


static int item_ref_tp_init(PyObject *self, PyObject *args, PyObject *kwds) {

  PyObject *name = NULL;
  PyObject *obj = NULL;
  PyObject *key = NULL;

  if (kwds && PyDict_Size(kwds)) {
    PyErr_SetString(PyExc_TypeError, "ItemRef takes no named arguments");
    return -1;
  }

  if (! PyArg_ParseTuple(args, "OOO:ItemRef", &name, &obj, &key))
    return -1;

  return item_ref_init(self, name, obj, key);
}


static int item_ref_traverse(PyObject *self, visitproc visit, void *arg) {
  Py_VISIT(KosRef_NAME(self));
  Py_VISIT(KosItemRef_OBJ(self));
  Py_VISIT(KosItemRef_KEY(self));
  return 0;
}


static int item_ref_clear(PyObject *self) {
  Py_CLEAR(KosRef_NAME(self));
  Py_CLEAR(KosItemRef_OBJ(self));
  Py_CLEAR(KosItemRef_KEY(self));
  return 0;
}


static void item_ref_dealloc(PyObject *self) {
  PyObject_GC_UnTrack(self);
  Py_CLEAR(KosRef_NAME(self));
  Py_CLEAR(KosItemRef_OBJ(self));
  Py_CLEAR(KosItemRef_KEY(self));
  Py_TYPE(self)->tp_free(self);
}


PyTypeObject KosItemRefType = {
  PyVarObject_HEAD_INIT(NULL, 0)

  "koskari.lib.ItemRef",
  sizeof(KosItemRef),
  0,

  .tp_base = &KosRefType,
  .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC,
  .tp_repr = ref_generic_repr,
  .tp_getset = item_ref_getset,
  .tp_methods = item_ref_methods,
  .tp_new = PyType_GenericNew,
  .tp_init = item_ref_tp_init,
  .tp_traverse = item_ref_traverse,
  .tp_clear = item_ref_clear,
  .tp_dealloc = item_ref_dealloc,
};


PyObject *KosItemRef_New(PyObject *name, PyObject *obj, PyObject *key) {
  PyObject *self = PyObject_GC_New(PyObject, &KosItemRefType);
  if (! self)
    return NULL;

  if(item_ref_init(self, name, obj, key))
    Py_CLEAR(self);

  if (self)
    PyObject_GC_Track(self);
  return self;
}


/* === DynamicRef === */


static PyObject *ref_demand_wrap(PyObject *fn) {
  if (! fn || fn == Py_None)
    return NULL;
  if (KosCallable_Check(fn))
    return KosDemand_New(fn);
  Py_INCREF(fn);
  return fn;
}


static int dyn_ref_init(
    PyObject *self,
    PyObject *name,
    PyObject *getfn,
    PyObject *setfn,
    PyObject *delfn,
    PyObject *emptyfn) {

  KosRef_NAME(self) = NULL;
  KosDynamicRef_GETFN(self) = NULL;
  KosDynamicRef_SETFN(self) = NULL;
  KosDynamicRef_DELFN(self) = NULL;
  KosDynamicRef_EMPTYFN(self) = NULL;

  if (! name || name == Py_None) {
    name = _str_dyn_ref;
    Py_INCREF(name);
  } else {
    name = kos_key_to_str(name);
    if (! name)
      return -1;
  }
  KosRef_NAME(self) = name;

  KosDynamicRef_GETFN(self) = ref_demand_wrap(getfn);
  if (! KosDynamicRef_GETFN(self) && PyErr_Occurred())
    return -1;

  KosDynamicRef_SETFN(self) = ref_demand_wrap(setfn);
  if (! KosDynamicRef_SETFN(self) && PyErr_Occurred())
    return -1;

  KosDynamicRef_DELFN(self) = ref_demand_wrap(delfn);
  if (! KosDynamicRef_DELFN(self) && PyErr_Occurred())
    return -1;

  KosDynamicRef_EMPTYFN(self) = ref_demand_wrap(emptyfn);
  if (! KosDynamicRef_EMPTYFN(self) && PyErr_Occurred())
    return -1;

  return 0;
}


static PyObject *dyn_ref_get_empty(PyObject *self, void *_unused) {
  PyObject *fn = KosDynamicRef_EMPTYFN(self);
  if (fn) {
    if (PyCallable_Check(fn))
      return PyObject_CallObject(fn, NULL);
    Py_INCREF(fn);
    return fn;
  }
  Py_RETURN_FALSE;
}


static PyGetSetDef dyn_ref_getset[] = {
  { "empty", dyn_ref_get_empty, NULL,
    "bool; whether this reference is empty",
    NULL },

  { NULL, },
};


static PyObject *dyn_ref_m_deref(PyObject *self) {
  PyObject *fn = KosDynamicRef_GETFN(self);
  if (fn)
    return PyObject_CallObject(fn, NULL);
  PyErr_Format(PyExc_AttributeError,
               "Dynamic reference %S has no deref handler",
               KosRef_NAME(self));
  return NULL;
}


static PyObject *dyn_ref_m_store(PyObject *self, PyObject *value) {
  PyObject *fn = KosDynamicRef_SETFN(self);
  if (fn)
    return PyObject_CallFunctionObjArgs(fn, value, NULL);
  PyErr_Format(PyExc_AttributeError,
               "Dynamic reference %S has no store handler",
               KosRef_NAME(self));
  return NULL;
}


static PyObject *dyn_ref_m_clear(PyObject *self) {
  PyObject *fn = KosDynamicRef_DELFN(self);
  if (fn)
    return PyObject_CallObject(fn, NULL);
  PyErr_Format(PyExc_AttributeError,
               "Dynamic reference %S has no clear handler",
               KosRef_NAME(self));
  return NULL;
}


static PyMethodDef dyn_ref_methods[] = {
  { "deref", (PyCFunction) dyn_ref_m_deref, METH_NOARGS,
    "R.deref() => V" },

  { "store", (PyCFunction) dyn_ref_m_store, METH_O,
    "R.store(V) => None" },

  { "clear", (PyCFunction) dyn_ref_m_clear, METH_NOARGS,
    "R.clear() => None" },

  { NULL, },
};


static int dyn_ref_tp_init(PyObject *self, PyObject *args, PyObject *kwds) {

  PyObject *name = NULL;
  PyObject *getfn = NULL;
  PyObject *setfn = NULL;
  PyObject *delfn = NULL;
  PyObject *emptyfn = NULL;

  if (kwds && PyDict_Size(kwds)) {
    PyErr_SetString(PyExc_TypeError, "DynamicRef takes no named arguments");
    return -1;
  }

  if (! PyArg_ParseTuple(args, "OO|OOO:DynamicRef", &name,
                         &getfn, &setfn, &delfn, &emptyfn))
    return -1;

  return dyn_ref_init(self, name, getfn, setfn, delfn, emptyfn);
}


static int dyn_ref_traverse(PyObject *self, visitproc visit, void *arg) {
  Py_VISIT(KosRef_NAME(self));
  Py_VISIT(KosDynamicRef_GETFN(self));
  Py_VISIT(KosDynamicRef_SETFN(self));
  Py_VISIT(KosDynamicRef_DELFN(self));
  Py_VISIT(KosDynamicRef_EMPTYFN(self));
  return 0;
}


static int dyn_ref_clear(PyObject *self) {
  Py_CLEAR(KosRef_NAME(self));
  Py_CLEAR(KosDynamicRef_GETFN(self));
  Py_CLEAR(KosDynamicRef_SETFN(self));
  Py_CLEAR(KosDynamicRef_DELFN(self));
  Py_CLEAR(KosDynamicRef_EMPTYFN(self));
  return 0;
}


static void dyn_ref_dealloc(PyObject *self) {
  PyObject_GC_UnTrack(self);
  Py_CLEAR(KosRef_NAME(self));
  Py_CLEAR(KosDynamicRef_GETFN(self));
  Py_CLEAR(KosDynamicRef_SETFN(self));
  Py_CLEAR(KosDynamicRef_DELFN(self));
  Py_CLEAR(KosDynamicRef_EMPTYFN(self));
  Py_TYPE(self)->tp_free(self);
}


PyTypeObject KosDynamicRefType = {
  PyVarObject_HEAD_INIT(NULL, 0)

  "koskari.lib.DynamicRef",
  sizeof(KosDynamicRef),
  0,

  .tp_base = &KosRefType,
  .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC,
  .tp_repr = ref_generic_repr,
  .tp_getset = dyn_ref_getset,
  .tp_methods = dyn_ref_methods,
  .tp_new = PyType_GenericNew,
  .tp_init = dyn_ref_tp_init,
  .tp_traverse = dyn_ref_traverse,
  .tp_clear = dyn_ref_clear,
  .tp_dealloc = dyn_ref_dealloc,
};


/* === ConstantRef === */


static int const_ref_init(PyObject *self, PyObject *name, PyObject *value) {

  KosRef_NAME(self) = NULL;
  KosSymbolicRef_VALUE(self) = NULL;

  if (! value) {
    PyErr_SetString(PyExc_ValueError,
                    "ConstantRef requires a non-NULL value");
    return -1;
  }

  if ((! name) || Py_None == name) {
    name = _str_const_ref;
    Py_INCREF(name);
  } else {
    name = kos_key_to_str(name);
    if (! name)
      return -1;
  }
  KosRef_NAME(self) = name;

  Py_INCREF(value);
  KosSymbolicRef_VALUE(self) = value;

  return 0;
}


static PyObject *const_ref_m_store(PyObject *self, PyObject *value) {
  PyErr_Format(PyExc_ValueError, "Cannot modify value of %s %S",
               Py_TYPE(self)->tp_name,
               KosRef_NAME(self));
  return NULL;
}


static PyObject *const_ref_m_clear(PyObject *self) {
  PyErr_Format(PyExc_ValueError, "Cannot modify value of ConstantRef %S",
               KosRef_NAME(self));
  return NULL;
}


static PyMethodDef const_ref_methods[] = {
  { "store", (PyCFunction) const_ref_m_store, METH_O,
    "R.store(V) => <ValueError>" },

  { "clear", (PyCFunction) const_ref_m_clear, METH_NOARGS,
    "R.clear() => <ValueError>" },

  { NULL, },
};


static int const_ref_tp_init(PyObject *self, PyObject *args, PyObject *kwds) {
  PyObject *name = NULL;
  PyObject *val = NULL;

  if (kwds && PyDict_Size(kwds)) {
    PyErr_SetString(PyExc_TypeError, "ConstantRef takes no named arguments");
    return -1;
  }

  if (! PyArg_ParseTuple(args, "OO:ConstantRef", &name, &val))
    return -1;

  return const_ref_init(self, name, val);
}


PyTypeObject KosConstantRefType = {
  PyVarObject_HEAD_INIT(NULL, 0)

  "koskari.lib.ConstantRef",
  sizeof(KosConstantRef),
  0,

  .tp_base = &KosRefType,
  .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC,
  .tp_repr = ref_generic_repr,
  .tp_getset = sym_ref_getset,
  .tp_methods = const_ref_methods,
  .tp_new = PyType_GenericNew,
  .tp_init = const_ref_tp_init,
  .tp_traverse = sym_ref_traverse,
  .tp_clear = sym_ref_clear,
  .tp_dealloc = sym_ref_dealloc,
};


/* === MemoizedRef === */


static int memo_ref_init(PyObject *self, PyObject *name, PyObject *getfn) {
  KosRef_NAME(self) = NULL;
  KosMemoizedRef_VALUE(self) = NULL;
  KosMemoizedRef_GETFN(self) = NULL;

  if (! getfn || getfn == Py_None) {
    PyErr_SetString(PyExc_ValueError,
                    "MemoizedRef requires a callable");
    return -1;
  }

  if (! name || name == Py_None) {
    name = _str_memo_ref;
    Py_INCREF(name);
  } else {
    name = kos_key_to_str(name);
    if (! name)
      return -1;
  }
  KosRef_NAME(self) = name;

  KosMemoizedRef_GETFN(self) = ref_demand_wrap(getfn);
  if (! KosMemoizedRef_GETFN(self))
    return -1;

  return 0;
}


static PyObject *memo_ref_m_deref(PyObject *self) {
  PyObject *val = KosMemoizedRef_VALUE(self);

  if (! val) {
    PyObject *fn = KosMemoizedRef_GETFN(self);
    if (fn) {
      val = PyObject_CallObject(fn, NULL);
      KosMemoizedRef_VALUE(self) = val;

    } else {
      PyErr_Format(PyExc_AttributeError,
                   "Memoize reference %S has no deref handler",
                   KosRef_NAME(self));
      return NULL;
    }
  }

  Py_XINCREF(val);
  return val;
}


static PyMethodDef memo_ref_methods[] = {
  { "deref", (PyCFunction) memo_ref_m_deref, METH_NOARGS,
    "R.deref() => V" },

  { "store", (PyCFunction) const_ref_m_store, METH_O,
    "R.store(V)" },

  { NULL, },
};


static int memo_ref_tp_init(PyObject *self, PyObject *args, PyObject *kwds) {
  PyObject *name = NULL;
  PyObject *val = NULL;

  if (kwds && PyDict_Size(kwds)) {
    PyErr_SetString(PyExc_TypeError, "MemoizedRef takes no named arguments");
    return -1;
  }

  if (! PyArg_ParseTuple(args, "OO:MemoizedRef", &name, &val))
    return -1;

  return memo_ref_init(self, name, val);
}


static int memo_ref_traverse(PyObject *self, visitproc visit, void *arg) {
  Py_VISIT(KosRef_NAME(self));
  Py_VISIT(KosSymbolicRef_VALUE(self));
  Py_VISIT(KosMemoizedRef_GETFN(self));
  return 0;
}


static int memo_ref_clear(PyObject *self) {
  Py_CLEAR(KosRef_NAME(self));
  Py_CLEAR(KosSymbolicRef_VALUE(self));
  Py_CLEAR(KosMemoizedRef_GETFN(self));
  return 0;
}


static void memo_ref_dealloc(PyObject *self) {
  PyObject_GC_UnTrack(self);
  Py_CLEAR(KosRef_NAME(self));
  Py_CLEAR(KosSymbolicRef_VALUE(self));
  Py_CLEAR(KosMemoizedRef_GETFN(self));
  Py_TYPE(self)->tp_free(self);
}


PyTypeObject KosMemoizedRefType = {
  PyVarObject_HEAD_INIT(NULL, 0)

  "koskari.lib.MemoizedRef",
  sizeof(KosMemoizedRef),
  0,

  .tp_base = &KosRefType,
  .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC,
  .tp_repr = ref_generic_repr,
  .tp_getset = sym_ref_getset,
  .tp_methods = memo_ref_methods,
  .tp_new = PyType_GenericNew,
  .tp_init = memo_ref_tp_init,
  .tp_traverse = memo_ref_traverse,
  .tp_clear = memo_ref_clear,
  .tp_dealloc = memo_ref_dealloc,
};


/* === WeakRef === */


static int weak_ref_init(PyObject *self, PyObject *name, PyObject *value) {

  KosRef_NAME(self) = NULL;
  KosSymbolicRef_VALUE(self) = NULL;

  if (! name || name == Py_None) {
    name = _str_weak_ref;
    Py_INCREF(name);
  } else {
    name = kos_key_to_str(name);
    if (! name)
      return -1;
  }
  KosRef_NAME(self) = name;

  if (value) {
    PyObject *wr = PyWeakref_NewRef(value, NULL);
    if (! wr)
      return -1;
    KosSymbolicRef_VALUE(self) = wr;
  }

  return 0;
}


static PyObject *weak_ref_get_empty(PyObject *self, void *_unused) {

  PyObject *wr = KosSymbolicRef_VALUE(self);
  if (! wr)
    Py_RETURN_TRUE;

#if PY_MINOR_VERSION >= 13
  {
    PyObject *obj = NULL;
    int res = PyWeakref_GetRef(wr, &obj);
    if (! res) {
      Py_CLEAR(KosSymbolicRef_VALUE(self));
      Py_RETURN_TRUE;
    } else if (res < 0) {
      // error mode
      return NULL;
    } else {
      Py_XDECREF(obj);
      Py_RETURN_FALSE;
    }
  }
#else
  {
    PyObject *const obj = PyWeakref_GetObject(wr);
    if (obj == Py_None) {
      Py_CLEAR(KosSymbolicRef_VALUE(self));
      Py_RETURN_TRUE;
    } else if (! obj) {
      // error mode
      return NULL;
    } else {
      Py_RETURN_FALSE;
    }
  }
#endif
}


static PyObject *weak_ref_m_deref(PyObject *self) {
  PyObject *obj = NULL;
  PyObject *wr = KosSymbolicRef_VALUE(self);

#if PY_MINOR_VERSION >= 13
  if (wr) {
    int res = PyWeakref_GetRef(wr, &obj);
    if (unlikely(! res)) {
      Py_CLEAR(KosSymbolicRef_VALUE(self));
    } else if (unlikely(res < 0)) {
      // error mode
      return NULL;
    }
  }
#else
  if (wr) {
    obj = PyWeakref_GetObject(wr);
    if (unlikely(obj == Py_None)) {
      Py_CLEAR(KosSymbolicRef_VALUE(self));
      obj = NULL;
    } else if (unlikely(! obj)) {
      // error mode
      return NULL;
    } else {
      Py_INCREF(obj);
    }
  }
#endif

  if (! obj)
    PyErr_Format(PyExc_NameError, "Empty reference: %S", KosRef_NAME(self));
  return obj;
}


static PyObject *weak_ref_m_store(PyObject *self, PyObject *value) {

  PyObject *wr = PyWeakref_NewRef(value, NULL);
  if (! wr)
    return NULL;
  Py_XDECREF(KosSymbolicRef_VALUE(self));
  KosSymbolicRef_VALUE(self) = wr;
  Py_RETURN_NONE;
}


static PyMethodDef weak_ref_methods[] = {
  { "deref", (PyCFunction) weak_ref_m_deref, METH_NOARGS,
    "R.deref() => V" },

  { "store", (PyCFunction) weak_ref_m_store, METH_O,
    "R.store(V) => None" },

  { "clear", (PyCFunction) sym_ref_m_clear, METH_NOARGS,
    "R.clear() => None" },

  { NULL, },
};


static PyGetSetDef weak_ref_getset[] = {
  { "empty", weak_ref_get_empty, NULL,
    "bool; whether this reference is empty",
    NULL },

  { "name", sym_ref_get_name, NULL,
    "str; name of this reference",
    NULL },

  { NULL, },
};


static int weak_ref_tp_init(PyObject *self, PyObject *args, PyObject *kwds) {

  PyObject *name = NULL;
  PyObject *val = NULL;

  if (kwds && PyDict_Size(kwds)) {
    PyErr_SetString(PyExc_TypeError, "WeakRef takes no named arguments");
    return -1;
  }

  if (! PyArg_ParseTuple(args, "|OO:WeakRef", &name, &val))
    return -1;

  return weak_ref_init(self, name, val);
}


PyTypeObject KosWeakRefType = {
  PyVarObject_HEAD_INIT(NULL, 0)

  "koskari.lib.WeakRef",
  sizeof(KosWeakRef),
  0,

  .tp_base = &KosRefType,
  .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC,
  .tp_repr = ref_generic_repr,
  .tp_getset = weak_ref_getset,
  .tp_methods = weak_ref_methods,
  .tp_new = PyType_GenericNew,
  .tp_init = weak_ref_tp_init,
  .tp_traverse = sym_ref_traverse,
  .tp_clear = sym_ref_clear,
  .tp_dealloc = sym_ref_dealloc,
};


/* === ParamRef === */


static int param_ref_init(PyObject *self, PyObject *key, PyObject *default_value) {
  KosRef_NAME(self) = NULL;
  KosParamRef_DEFAULT(self) = NULL;

  if (! key || key == Py_None) {
    PyErr_SetString(PyExc_ValueError, "ParamRef requires a key (name)");
    return -1;
  }
  key = kos_key_to_str(key);
  if (! key)
    return -1;
  KosRef_NAME(self) = key;

  Py_XINCREF(default_value);
  KosParamRef_DEFAULT(self) = default_value;

  return 0;
}


/* True when neither a koskari context nor a python Domain scope is
   ambient, so there is no store to resolve against at all. */
static int param_ref_no_domain(void) {
  return (! kos_ctx_current()) && (! kos_domain_current());
}


static PyObject *param_ref_m_deref(PyObject *self) {
  PyObject *cell;
  PyObject *value;
  PyObject *default_val = KosParamRef_DEFAULT(self);

  if (param_ref_no_domain()) {
    if (default_val) {
      Py_INCREF(default_val);
      return default_val;
    }
    PyErr_SetString(PyExc_RuntimeError,
                    "ParamRef.deref outside active domain and no default");
    return NULL;
  }

  /* a read creates nothing: extent shadow, then root scope, then the
     default this ref declares */
  cell = kos_param_lookup_ambient(KosParamRef_KEY(self));
  value = cell ? KosSymbolicRef_VALUE(cell) : NULL;
  if (value) {
    Py_INCREF(value);
    return value;
  }

  if (default_val) {
    Py_INCREF(default_val);
    return default_val;
  }

  PyErr_Format(PyExc_NameError, "Param %S has no binding and no default",
               KosParamRef_KEY(self));
  return NULL;
}


static PyObject *param_ref_m_store(PyObject *self, PyObject *value) {
  PyObject *cell;

  /* an existing binding is written in place, wherever it lives; with no
     binding the value lands in the root scope */
  cell = kos_param_lookup_ambient(KosParamRef_KEY(self));
  if (cell) {
    Py_INCREF(value);
    Py_XSETREF(KosSymbolicRef_VALUE(cell), value);
    Py_RETURN_NONE;
  }

  cell = kos_param_root_resolve_ambient(KosParamRef_KEY(self));
  if (! cell)
    return NULL;
  Py_INCREF(value);
  Py_XSETREF(KosSymbolicRef_VALUE(cell), value);
  Py_DECREF(cell);
  Py_RETURN_NONE;
}


static PyObject *param_ref_m_clear(PyObject *self) {
  PyObject *cell;

  if (param_ref_no_domain()) {
    PyErr_SetString(PyExc_RuntimeError,
                    "ParamRef.clear outside active domain");
    return NULL;
  }

  cell = kos_param_lookup_ambient(KosParamRef_KEY(self));
  if (cell)
    Py_CLEAR(KosSymbolicRef_VALUE(cell));
  Py_RETURN_NONE;
}


static PyObject *param_ref_m_is_empty(PyObject *self) {
  PyObject *cell;

  if (param_ref_no_domain())
    return PyBool_FromLong(1);

  cell = kos_param_lookup_ambient(KosParamRef_KEY(self));
  return PyBool_FromLong(! (cell && KosSymbolicRef_VALUE(cell)));
}


static PyGetSetDef param_ref_getset[] = {
  { "empty", (getter) param_ref_m_is_empty, NULL,
    "bool; whether this reference is empty in the active domain",
    NULL },

  { "name", sym_ref_get_name, NULL,
    "str; name of this reference",
    NULL },

  { NULL, },
};


static PyMethodDef param_ref_methods[] = {
  { "deref", (PyCFunction) param_ref_m_deref, METH_NOARGS,
    "R.deref() => V" },

  { "store", (PyCFunction) param_ref_m_store, METH_O,
    "R.store(V) => None" },

  { "clear", (PyCFunction) param_ref_m_clear, METH_NOARGS,
    "R.clear() => None" },

  { NULL, },
};


static int param_ref_tp_init(PyObject *self, PyObject *args, PyObject *kwds) {
  PyObject *key = NULL;
  PyObject *default_value = NULL;

  if (kwds && PyDict_Size(kwds)) {
    PyErr_SetString(PyExc_TypeError, "ParamRef takes no named arguments");
    return -1;
  }

  if (! PyArg_ParseTuple(args, "O|O:ParamRef", &key, &default_value))
    return -1;

  return param_ref_init(self, key, default_value);
}


static int param_ref_traverse(PyObject *self, visitproc visit, void *arg) {
  Py_VISIT(KosRef_NAME(self));
  Py_VISIT(KosParamRef_DEFAULT(self));
  return 0;
}


static int param_ref_clear(PyObject *self) {
  Py_CLEAR(KosRef_NAME(self));
  Py_CLEAR(KosParamRef_DEFAULT(self));
  return 0;
}


static void param_ref_dealloc(PyObject *self) {
  PyObject_GC_UnTrack(self);
  Py_CLEAR(KosRef_NAME(self));
  Py_CLEAR(KosParamRef_DEFAULT(self));
  Py_TYPE(self)->tp_free(self);
}


PyTypeObject KosParamRefType = {
  PyVarObject_HEAD_INIT(NULL, 0)

  "koskari.lib.ParamRef",
  sizeof(KosParamRef),
  0,

  .tp_base = &KosRefType,
  .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC,
  .tp_repr = ref_generic_repr,
  .tp_getset = param_ref_getset,
  .tp_methods = param_ref_methods,
  .tp_new = PyType_GenericNew,
  .tp_init = param_ref_tp_init,
  .tp_traverse = param_ref_traverse,
  .tp_clear = param_ref_clear,
  .tp_dealloc = param_ref_dealloc,
};


PyObject *KosParamRef_New(PyObject *key, PyObject *default_value) {
  PyObject *self = PyObject_GC_New(PyObject, &KosParamRefType);
  if (! self)
    return NULL;

  if (param_ref_init(self, key, default_value)) {
    Py_CLEAR(self);
    return NULL;
  }

  PyObject_GC_Track(self);
  return self;
}


/* === Generic ref functions === */


PyObject *KosRef_Deref(PyObject *self) {
  PyTypeObject *const tp = self->ob_type;

  if (likely(tp == &KosRefType ||
             tp == &KosConstantRefType)) {
    // Inline of sym_ref_m_deref: both kinds read their value from KosRef.obj.
    PyObject *value = KosSymbolicRef_VALUE(self);
    if (likely(value)) {
      Py_INCREF(value);
      return value;
    }
    PyErr_Format(PyExc_NameError, "Empty reference: %S", KosRef_NAME(self));
    return NULL;

  } else if (tp == &KosMemoizedRefType) {
    return memo_ref_m_deref(self);

  } else if (tp == &KosAttributeRefType) {
    return attr_ref_m_deref(self);

  } else if (tp == &KosItemRefType) {
    return item_ref_m_deref(self);

  } else if (tp == &KosDynamicRefType) {
    return dyn_ref_m_deref(self);

  } else if (tp == &KosWeakRefType) {
    return weak_ref_m_deref(self);

  } else if (tp == &KosParamRefType) {
    return param_ref_m_deref(self);

  } else {
    return PyObject_CallMethodObjArgs(self, _str_deref, NULL);
  }
}


int KosRef_Store(PyObject *self, PyObject *value) {
  PyObject *res = NULL;
  PyTypeObject *const tp = self->ob_type;

  // Fast path: symbolic (mutable) ref — store in place, skipping the
  // Py_None materialization the PyObject-returning method convention needs.
  if (likely(tp == &KosRefType)) {
    Py_XINCREF(value);
    Py_XDECREF(KosSymbolicRef_VALUE(self));
    KosSymbolicRef_VALUE(self) = value;
    return 0;
  }

  if (tp == &KosConstantRefType ||
             tp == &KosMemoizedRefType) {
    res = const_ref_m_store(self, value);

  } else if (tp == &KosAttributeRefType) {
    res = attr_ref_m_store(self, value);

  } else if (tp == &KosItemRefType) {
    res = item_ref_m_store(self, value);

  } else if (tp == &KosDynamicRefType) {
    res = dyn_ref_m_store(self, value);

  } else if (tp == &KosWeakRefType) {
    res = weak_ref_m_store(self, value);

  } else if (tp == &KosParamRefType) {
    res = param_ref_m_store(self, value);

  } else {
    res = PyObject_CallMethodObjArgs(self, _str_store, value, NULL);
  }

  if (res) {
    Py_DECREF(res);
    return 0;

  } else {
    return -1;
  }
}


int KosRef_Clear(PyObject *self) {
  PyObject *res = NULL;
  PyTypeObject *const tp = self->ob_type;

  if (tp == &KosRefType ||
      tp == &KosWeakRefType ||
      tp == &KosMemoizedRefType) {
    res = sym_ref_m_clear(self);

  } else if (tp == &KosConstantRefType) {
    res = const_ref_m_clear(self);

  } else if (tp == &KosAttributeRefType) {
    res = attr_ref_m_clear(self);

  } else if (tp == &KosItemRefType) {
    res = item_ref_m_clear(self);

  } else if (tp == &KosDynamicRefType) {
    res = dyn_ref_m_clear(self);

  } else if (tp == &KosParamRefType) {
    res = param_ref_m_clear(self);

  } else {
    res = PyObject_CallMethodObjArgs(self, _str_clear, NULL);
  }

  if (res) {
    Py_DECREF(res);
    return 0;

  } else {
    return -1;
  }
}


int KosRef_IsEmpty(PyObject *self) {
  PyObject *res = NULL;
  PyTypeObject *const tp = self->ob_type;

  if (tp == &KosRefType ||
      tp == &KosConstantRefType ||
      tp == &KosMemoizedRefType) {
    res = sym_ref_get_empty(self, NULL);

  } else if (tp == &KosWeakRefType) {
    res = weak_ref_get_empty(self, NULL);

  } else if (tp == &KosAttributeRefType) {
    res = attr_ref_get_empty(self, NULL);

  } else if (tp == &KosItemRefType) {
    res = item_ref_get_empty(self, NULL);

  } else if (tp == &KosDynamicRefType) {
    res = dyn_ref_get_empty(self, NULL);

  } else if (tp == &KosParamRefType) {
    res = param_ref_m_is_empty(self);

  } else {
    res = PyObject_GetAttr(self, _str_empty);
  }

  int ret = -1;
  if (res) {
    ret = PyObject_IsTrue(res);
    Py_DECREF(res);
  }
  return ret;
}


static int ref_nb_bool(PyObject *self) {
  int empty = KosRef_IsEmpty(self);

  if (empty < 0)
    return -1;

  return empty ? 0 : 1;
}


/* === Unit lifecycle === */


int koskari_ref_init(PyObject *mod) {
  if (! mod)
    return -1;

  PyObject *dict = PyModule_GetDict(mod);
  if (! dict)
    return -1;

  if (PyType_Ready(&KosRefType) ||
      PyType_Ready(&KosAttributeRefType) ||
      PyType_Ready(&KosItemRefType) ||
      PyType_Ready(&KosDynamicRefType) ||
      PyType_Ready(&KosConstantRefType) ||
      PyType_Ready(&KosMemoizedRefType) ||
      PyType_Ready(&KosWeakRefType) ||
      PyType_Ready(&KosParamRefType))
    return -1;

  STR_CONST(_str_ref, "<Ref>");
  STR_CONST(_str_item_ref, "<ItemRef>");
  STR_CONST(_str_attr_ref, "<AttributeRef>");
  STR_CONST(_str_dyn_ref, "<DynamicRef>");
  STR_CONST(_str_const_ref, "<ConstantRef>");
  STR_CONST(_str_memo_ref, "<MemoizedRef>");
  STR_CONST(_str_param_ref, "<ParamRef>");
  STR_CONST(_str_weak_ref, "<WeakRef>");
  STR_CONST(_str_deref, "deref");
  STR_CONST(_str_store, "store");
  STR_CONST(_str_clear, "clear");
  STR_CONST(_str_empty, "empty");

  PyDict_SetItemString(dict, "Ref",
                       (PyObject *) &KosRefType);
  PyDict_SetItemString(dict, "AttributeRef",
                       (PyObject *) &KosAttributeRefType);
  PyDict_SetItemString(dict, "ItemRef",
                       (PyObject *) &KosItemRefType);
  PyDict_SetItemString(dict, "DynamicRef",
                       (PyObject *) &KosDynamicRefType);
  PyDict_SetItemString(dict, "ConstantRef",
                       (PyObject *) &KosConstantRefType);
  PyDict_SetItemString(dict, "MemoizedRef",
                       (PyObject *) &KosMemoizedRefType);
  PyDict_SetItemString(dict, "WeakRef",
                       (PyObject *) &KosWeakRefType);
  PyDict_SetItemString(dict, "ParamRef",
                       (PyObject *) &KosParamRefType);

  for (PyMethodDef *mdef = ref_mod_methods; mdef->ml_name; mdef++) {
    PyObject *func = PyCFunction_New(mdef, mod);
    if (! func)
      return -1;

    if (PyDict_SetItemString(dict, mdef->ml_name, func) < 0) {
      Py_DECREF(func);
      return -1;
    }
    Py_DECREF(func);
  }

  return 0;
}


int koskari_ref_clear(PyObject *mod) {

  Py_CLEAR(_str_ref);
  Py_CLEAR(_str_item_ref);
  Py_CLEAR(_str_attr_ref);
  Py_CLEAR(_str_dyn_ref);
  Py_CLEAR(_str_const_ref);
  Py_CLEAR(_str_memo_ref);
  Py_CLEAR(_str_param_ref);
  Py_CLEAR(_str_weak_ref);
  Py_CLEAR(_str_deref);
  Py_CLEAR(_str_store);
  Py_CLEAR(_str_clear);
  Py_CLEAR(_str_empty);

  return 0;
}


// The end.
