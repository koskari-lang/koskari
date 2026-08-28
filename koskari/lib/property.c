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
   koskari/lib/property.c

   KosProperty: subtype of the built-in property that auto-wraps KosCallable
   accessors in Demand at every registration point (constructor, .setter,
   .deleter). Authors can use ordinary @property syntax on Koskari functions
   without a separate @demand or define-demanding step.

   The type is dynamically rooted to builtins.property at init time so that
   isinstance(obj, property) continues to work and .setter / .deleter return
   KosProperty instances through property_copy's Py_TYPE(self) dispatch.

   :author: Christopher O'Brien <obriencj@gmail.com>
   :license: GPL v.3
*/


#include "lib.h"


/* === module-level state === */


/* owned reference to builtins.property; keeps base alive across clear */
static PyObject *property_type_ref = NULL;

/* borrowed from property_type_ref; used for tp_base and tp_init calls */
static PyTypeObject *base_property_type = NULL;

/* storage for runtime-discovered __doc__ member descriptor (filled at init) */
static PyMemberDef kosproperty_doc_member;


/* === accessor wrapping === */


/**
   Wrap fn in Demand if it is a KosCallable; return fn unchanged otherwise.
   Py_None passes through. Returns a new reference.

   fn must not be NULL; callers normalize missing optional args to Py_None
   before calling this function.
*/
static PyObject *kos_demand_wrap_accessor(PyObject *fn) {

  if (fn == Py_None) {
    Py_INCREF(Py_None);
    return Py_None;
  }

  if (KosCallable_Check(fn))
    return KosDemand_New(fn);

  Py_INCREF(fn);
  return fn;
}


/* === KosProperty type === */


static int kosproperty_init(
    PyObject *self,
    PyObject *args,
    PyObject *kwds) {

  static char *kwlist[] = {"fget", "fset", "fdel", "doc", NULL};

  PyObject *fget = NULL, *fset = NULL, *fdel = NULL, *doc = NULL;

  if (! PyArg_ParseTupleAndKeywords(args, kwds, "|OOOO:property",
                                     kwlist, &fget, &fset, &fdel, &doc))
    return -1;

  /* Wrap each accessor; normalize NULL (unset) to Py_None for the base call */
  PyObject *w_fget = kos_demand_wrap_accessor(fget ? fget : Py_None);
  PyObject *w_fset = kos_demand_wrap_accessor(fset ? fset : Py_None);
  PyObject *w_fdel = kos_demand_wrap_accessor(fdel ? fdel : Py_None);

  if (! w_fget || ! w_fset || ! w_fdel) {
    Py_XDECREF(w_fget);
    Py_XDECREF(w_fset);
    Py_XDECREF(w_fdel);
    return -1;
  }

  /* Rebuild positional args with wrapped accessors for base tp_init */
  PyObject *new_args = PyTuple_Pack(3, w_fget, w_fset, w_fdel);
  Py_DECREF(w_fget);
  Py_DECREF(w_fset);
  Py_DECREF(w_fdel);

  if (! new_args)
    return -1;

  /* Forward doc as keyword arg only when explicitly provided */
  PyObject *new_kwds = NULL;
  if (doc) {
    new_kwds = PyDict_New();
    if (! new_kwds) {
      Py_DECREF(new_args);
      return -1;
    }
    if (PyDict_SetItemString(new_kwds, "doc", doc) < 0) {
      Py_DECREF(new_kwds);
      Py_DECREF(new_args);
      return -1;
    }
  }

  int r = base_property_type->tp_init(self, new_args, new_kwds);
  Py_DECREF(new_args);
  Py_XDECREF(new_kwds);
  return r;
}


static PyObject *kosproperty_repr(PyObject *self) {

  PyObject *fget = PyObject_GetAttrString(self, "fget");
  PyObject *name = NULL;
  PyObject *out;

  if (fget) {
    if (fget != Py_None)
      name = PyObject_GetAttrString(fget, "__name__");
    Py_DECREF(fget);
  }
  PyErr_Clear();

  if (name) {
    out = PyUnicode_FromFormat("<koskari property %R at %p>", name, self);
    Py_DECREF(name);
  } else {
    out = PyUnicode_FromFormat("<koskari property object at %p>", self);
  }
  return out;
}


PyTypeObject KosPropertyType = {
  PyVarObject_HEAD_INIT(NULL, 0)

  .tp_name    = "koskari.lib.property",
  .tp_basicsize = 0,   /* inherited from builtins.property at PyType_Ready */
  .tp_flags   = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,
  .tp_repr    = kosproperty_repr,
  .tp_init    = kosproperty_init,
  /* tp_base set dynamically in koskari_property_init before PyType_Ready */
};


/* === module lifecycle === */


/**
   Inspect base_property_type->tp_members for a READONLY "__doc__" entry.
   If found, inject a readwrite member descriptor at the same struct offset
   into KosPropertyType.tp_dict, shadowing the inherited read-only one.

   Python 3.12 stores prop_doc as T_OBJECT | READONLY in property.tp_members.
   property_init_impl uses PyObject_SetAttr(self, "__doc__", ...) for subclass
   instances; without this override that call raises AttributeError.
*/
static int kosproperty_override_doc(void) {

  if (! base_property_type->tp_members)
    return 0;

  PyMemberDef *m;
  for (m = base_property_type->tp_members; m->name; m++) {
    if (strcmp(m->name, "__doc__") == 0)
      break;
  }

  if (! m->name)
    return 0;  /* not in base tp_members; nothing to shadow */

  /* Inject a readwrite member descriptor at the same offset into our own
     tp_dict.  This shadows whatever entry (readonly member or getter-only
     getset) property placed in its dict for __doc__, so that
     property_init_impl's PyObject_SetAttr(self, "__doc__", ...) call
     succeeds for KosProperty subclass instances. */
  kosproperty_doc_member.name   = "__doc__";
  kosproperty_doc_member.type   = T_OBJECT;
  kosproperty_doc_member.offset = m->offset;
  kosproperty_doc_member.flags  = 0;
  kosproperty_doc_member.doc    = NULL;

  PyObject *descr = PyDescr_NewMember(&KosPropertyType, &kosproperty_doc_member);
  if (! descr)
    return -1;

  int r = PyDict_SetItemString(KosPropertyType.tp_dict, "__doc__", descr);
  Py_DECREF(descr);
  return r;
}


int koskari_property_init(PyObject *mod) {

  PyObject *builtins = PyImport_ImportModule("builtins");
  if (! builtins)
    return -1;

  property_type_ref = PyObject_GetAttrString(builtins, "property");
  Py_DECREF(builtins);

  if (! property_type_ref)
    return -1;

  if (! PyType_Check(property_type_ref)) {
    PyErr_SetString(PyExc_TypeError, "builtins.property is not a type");
    return -1;
  }

  base_property_type = (PyTypeObject *) property_type_ref;
  KosPropertyType.tp_base = base_property_type;

  if (PyType_Ready(&KosPropertyType) < 0)
    return -1;

  if (kosproperty_override_doc() < 0)
    return -1;

  PyObject *dict = PyModule_GetDict(mod);
  if (! dict)
    return -1;

  if (PyDict_SetItemString(dict, "KosProperty",
                           (PyObject *) &KosPropertyType) < 0)
    return -1;

  /* short alias matching the class __name__ */
  if (PyDict_SetItemString(dict, "property",
                           (PyObject *) &KosPropertyType) < 0)
    return -1;

  return 0;
}


int koskari_property_clear(PyObject *mod) {

  base_property_type = NULL;
  Py_CLEAR(property_type_ref);
  (void) mod;
  return 0;
}


// The end.
