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
   Formals types for Koskari

   FormalsSpec defines the formal parameter structure for a function.
   Formals combines a FormalsSpec with default values and provides
   argument application to a namespace.

   author: Christopher O'Brien <obriencj@gmail.com>
   license: GPL v.3
*/


#include "lib.h"


#define TUPLE_OR_NULL(seq)                        \
  if (seq) {                                      \
    if (seq == Py_None) {                         \
      seq = NULL;                                 \
    } else if (PySequence_Size(seq) <= 0) {       \
      seq = NULL;                                 \
    } else {                                      \
      seq = PySequence_Tuple(seq);                \
      if (! seq) goto tuple_error;                \
    }                                             \
  }


/* === FormalsSpec === */


static PyMemberDef formals_spec_members[] = {
  { "pos", T_OBJECT, offsetof(KosFormalsSpec, pos), READONLY,
    "tuple of positional symbols" },

  { "pos_only",  T_OBJECT, offsetof(KosFormalsSpec, pos_only), READONLY,
    "tuple of positional-only symbols" },

  { "kwd", T_OBJECT, offsetof(KosFormalsSpec, kwd), READONLY,
    "tuple of keyword symbols" },

  { "kwd_only",  T_OBJECT, offsetof(KosFormalsSpec, kwd_only), READONLY,
    "tuple of keyword-only symbols" },

  { "star", T_OBJECT, offsetof(KosFormalsSpec, star), READONLY,
    "symbol for variadic positional values" },

  { "starstar",  T_OBJECT, offsetof(KosFormalsSpec, starstar), READONLY,
    "symbol for variadic keyword values" },

  { NULL, },
};


static PyObject *formals_spec_m_nspec(PyObject *self,
                                      PyObject *base_nspec) {
  if (base_nspec == Py_None)
    base_nspec = NULL;

  if (base_nspec && ! KosNamespaceSpec_CheckExact(base_nspec)) {
    PyErr_Format(PyExc_TypeError, "base_nspec must be a NamespaceSpec"
                 " or None, not %R", base_nspec);
    return NULL;
  }

  return KosFormalsSpec_NamespaceSpec(self, base_nspec);
}


static PyMethodDef formals_spec_methods[] = {
  { "namespace_spec", (PyCFunction) formals_spec_m_nspec, METH_O,
    "FS.namespace_spec(base_nspec) => NamespaceSpec" },

  { NULL, NULL, 0, NULL },
};


static PyObject *formals_spec_call(PyObject *self,
                                   PyObject *args, PyObject *kwds) {
  PyObject *defaults = NULL;

  if (kwds) {
    PyErr_SetString(PyExc_TypeError, "FormalsSpec.apply does not accept"
                    " keyword arguments");
    return NULL;
  }

  if (! PyArg_ParseTuple(args, "|O", &defaults))
    return NULL;

  return KosFormals_New(self, defaults);
}


static PyObject *formals_spec_new(PyTypeObject *type,
                                  PyObject *args, PyObject *kwds) {
  PyObject *pos = NULL;
  PyObject *pos_only = NULL;
  PyObject *kwd = NULL;
  PyObject *kwd_only = NULL;
  PyObject *star = NULL;
  PyObject *starstar = NULL;

  if (! PyArg_ParseTuple(args, "|OOOOOO:FormalsSpec",
                         &pos, &pos_only, &kwd, &kwd_only,
                         &star, &starstar))
    return NULL;

  return KosFormalsSpec_New(pos, pos_only, kwd, kwd_only,
                            star, starstar);
}


static void formals_spec_dealloc(PyObject *self) {
  Py_CLEAR(KosFormalsSpec_POS(self));
  Py_CLEAR(KosFormalsSpec_POS_ONLY(self));
  Py_CLEAR(KosFormalsSpec_KWD(self));
  Py_CLEAR(KosFormalsSpec_KWD_ONLY(self));
  Py_CLEAR(KosFormalsSpec_STAR(self));
  Py_CLEAR(KosFormalsSpec_STARSTAR(self));
  Py_TYPE(self)->tp_free(self);
}


static int formals_spec_traverse(PyObject *self, visitproc visit, void *arg) {
  Py_VISIT(KosFormalsSpec_POS(self));
  Py_VISIT(KosFormalsSpec_POS_ONLY(self));
  Py_VISIT(KosFormalsSpec_KWD(self));
  Py_VISIT(KosFormalsSpec_KWD_ONLY(self));
  Py_VISIT(KosFormalsSpec_STAR(self));
  Py_VISIT(KosFormalsSpec_STARSTAR(self));
  return 0;
}


static int formals_spec_clear(PyObject *self) {
  Py_CLEAR(KosFormalsSpec_POS(self));
  Py_CLEAR(KosFormalsSpec_POS_ONLY(self));
  Py_CLEAR(KosFormalsSpec_KWD(self));
  Py_CLEAR(KosFormalsSpec_KWD_ONLY(self));
  Py_CLEAR(KosFormalsSpec_STAR(self));
  Py_CLEAR(KosFormalsSpec_STARSTAR(self));
  return 0;
}


PyTypeObject KosFormalsSpecType = {
  PyVarObject_HEAD_INIT(NULL, 0)

  "FormalsSpec",
  sizeof(KosFormalsSpec),
  0,

  .tp_flags = Py_TPFLAGS_DEFAULT,
  .tp_members = formals_spec_members,
  .tp_methods = formals_spec_methods,
  .tp_call = formals_spec_call,
  .tp_new = formals_spec_new,
  .tp_dealloc = formals_spec_dealloc,
  .tp_traverse = formals_spec_traverse,
  .tp_clear = formals_spec_clear,
};


PyObject *KosFormalsSpec_New(
    PyObject *pos,
    PyObject *pos_only,
    PyObject *kwd,
    PyObject *kwd_only,
    PyObject *star,
    PyObject *starstar) {

  if (pos && (pos != Py_None) && (! PySequence_Check(pos))) {
    PyErr_Format(PyExc_TypeError, "pos must be a sequence,"
                 " not %R", pos);
    return NULL;
  }
  if (pos_only && (pos_only != Py_None) && (! PySequence_Check(pos_only))) {
    PyErr_Format(PyExc_TypeError, "pos_only must be a sequence,"
                 " not %R", pos_only);
    return NULL;
  }
  if (kwd && (kwd != Py_None) && (! PySequence_Check(kwd))) {
    PyErr_Format(PyExc_TypeError, "kwd must be a sequence,"
                 " not %R", kwd);
    return NULL;
  }
  if (kwd_only && (kwd_only != Py_None) && (! PySequence_Check(kwd_only))) {
    PyErr_Format(PyExc_TypeError, "kwd_only must be a sequence,"
                 " not %R", kwd_only);
    return NULL;
  }

  if (star == Py_None) {
    star = NULL;
  } else if (star) {
    if (! PyUnicode_Check(star)) {
      PyErr_Format(PyExc_TypeError, "star formal must be None or str,"
                   " not: %R", star);
      return NULL;
    }
  }

  if (starstar == Py_None) {
    starstar = NULL;
  } else if (starstar) {
    if (! PyUnicode_Check(starstar)) {
      PyErr_Format(PyExc_TypeError, "starstar formal must be None or str,"
                   " not: %R", starstar);
      return NULL;
    }
  }

  PyObject *self = PyObject_New(PyObject, &KosFormalsSpecType);
  if (! self)
    return NULL;

  // safety net pre-assignment
  KosFormalsSpec_POS(self) = NULL;
  KosFormalsSpec_POS_ONLY(self) = NULL;
  KosFormalsSpec_KWD(self) = NULL;
  KosFormalsSpec_KWD_ONLY(self) = NULL;
  KosFormalsSpec_STAR(self) = NULL;
  KosFormalsSpec_STARSTAR(self) = NULL;

  TUPLE_OR_NULL(pos);
  KosFormalsSpec_POS(self) = pos;

  TUPLE_OR_NULL(pos_only);
  KosFormalsSpec_POS_ONLY(self) = pos_only;

  TUPLE_OR_NULL(kwd);
  KosFormalsSpec_KWD(self) = kwd;

  TUPLE_OR_NULL(kwd_only);
  KosFormalsSpec_KWD_ONLY(self) = kwd_only;

  Py_XINCREF(star);
  KosFormalsSpec_STAR(self) = star;

  Py_XINCREF(starstar);
  KosFormalsSpec_STARSTAR(self) = starstar;

  return self;

 tuple_error:
  Py_DECREF(self);
  return NULL;
}


#define SAFE_SIZE(v)                            \
  ((v)? PySequence_Fast_GET_SIZE(v): 0)


#define COPY_OVER(lcls, src, offset)                    \
  do {                                                  \
    PyObject *_src = (src);                             \
    size_t l = SAFE_SIZE(_src);                         \
    for (size_t i = 0; i < l; i++) {                    \
      PyObject *_v = PySequence_Fast_GET_ITEM(_src, i); \
      Py_INCREF(_v);                                    \
      PyList_SET_ITEM(lcls, offset++, _v);              \
    }                                                   \
  } while (0)


static void list_unique_append(PyObject *dst, PyObject *val) {
  for (size_t i = SAFE_SIZE(dst); i--; ) {
    PyObject *obj = PyList_GET_ITEM(dst, i);
    if (Py_EQUALS(val, obj))
      return;
  }
  PyList_Append(dst, val);
}


static void list_unique_extend(PyObject *dst, PyObject *seq) {
  size_t l = SAFE_SIZE(seq);
  for (size_t i = 0; i < l; i++) {
    PyObject *obj = PySequence_Fast_GET_ITEM(seq, i);
    list_unique_append(dst, obj);
  }
}


PyObject *KosFormalsSpec_NamespaceSpec(PyObject *fspec,
                                       PyObject *nspec) {
  size_t l = 0;
  size_t c = 0;

  if (! KosFormalsSpec_CheckExact(fspec)) {
    PyErr_Format(PyExc_TypeError, "FormalsSpec required, got %R", fspec);
    return NULL;
  }

  if (nspec == Py_None)
    nspec = NULL;

  if (nspec && ! KosNamespaceSpec_CheckExact(nspec)) {
    PyErr_Format(PyExc_TypeError, "NamespaceSpec required, got %R", nspec);
    return NULL;
  }

  l = SAFE_SIZE(KosFormalsSpec_POS(fspec));
  if (l < 0)
    return NULL;
  c += l;

  l = SAFE_SIZE(KosFormalsSpec_POS_ONLY(fspec));
  if (l < 0)
    return NULL;
  c += l;

  l = SAFE_SIZE(KosFormalsSpec_KWD(fspec));
  if (l < 0)
    return NULL;
  c += l;

  l = SAFE_SIZE(KosFormalsSpec_KWD_ONLY(fspec));
  if (l < 0)
    return NULL;
  c += l;

  if (KosFormalsSpec_STAR(fspec))
    c++;
  if (KosFormalsSpec_STARSTAR(fspec))
    c++;

  // c is the count of local bindings we would be creating from our
  // formals. If there aren't any, return the namespace spec we've
  // been handed. If we weren't handed one, return a new empty one.
  if (! c) {
    if (nspec) {
      Py_INCREF(nspec);
    } else {
      nspec = KosNamespaceSpec_New(NULL, NULL, NULL);
    }
    return nspec;
  }

  // pre-allocate a list of the right length for our formals, then
  // copy them in.

  PyObject *lcls = PyList_New(c);
  size_t offset = 0;

  COPY_OVER(lcls, KosFormalsSpec_POS(fspec), offset);
  COPY_OVER(lcls, KosFormalsSpec_POS_ONLY(fspec), offset);
  COPY_OVER(lcls, KosFormalsSpec_KWD(fspec), offset);
  COPY_OVER(lcls, KosFormalsSpec_KWD_ONLY(fspec), offset);

  PyObject *name;

  name = KosFormalsSpec_STAR(fspec);
  if (name) {
    Py_INCREF(name);
    PyList_SET_ITEM(lcls, offset++, name);
  }
  name = KosFormalsSpec_STARSTAR(fspec);
  if (name) {
    Py_INCREF(name);
    PyList_SET_ITEM(lcls, offset++, name);
  }

  if (c != offset) {
    PyErr_Format(PyExc_RuntimeError, "c (%i) != offset (%i)", c, offset);
    Py_DECREF(lcls);
    return NULL;
  }

  name = NULL;
  PyObject *clsr = NULL;

  if (nspec) {
    list_unique_extend(lcls, KosNamespaceSpec_LOCAL_SYMS(nspec));

    name = KosNamespaceSpec_NAME(nspec);
    clsr = KosNamespaceSpec_CLOSURE_SYMS(nspec);
  }

  PyObject *result = KosNamespaceSpec_New(name, lcls, clsr);
  Py_DECREF(lcls);
  return result;
}


#undef SAFE_SIZE
#undef COPY_OVER


/* === Formals === */


static PyMemberDef formals_members[] = {
  { "pos", T_OBJECT, offsetof(KosFormals, pos), READONLY,
    "tuple of positional symbols" },

  { "pos_only",  T_OBJECT, offsetof(KosFormals, pos_only), READONLY,
    "tuple of positional-only symbols" },

  { "kwd", T_OBJECT, offsetof(KosFormals, kwd), READONLY,
    "tuple of keyword symbols" },

  { "kwd_only",  T_OBJECT, offsetof(KosFormals, kwd_only), READONLY,
    "tuple of keyword-only symbols" },

  { "star", T_OBJECT, offsetof(KosFormals, star), READONLY,
    "symbol for variadic positional values" },

  { "starstar",  T_OBJECT, offsetof(KosFormals, starstar), READONLY,
    "symbol for variadic keyword values" },

  { "defaults",  T_OBJECT, offsetof(KosFormals, defaults), READONLY,
    "dict of default values" },

  { NULL, },
};


static PyObject *formals_m_apply(PyObject *self, PyObject *args) {
  PyObject *ns = NULL;
  PyObject *pos = NULL;
  PyObject *kwd = NULL;

  if (! PyArg_ParseTuple(args, "OO|O:apply", &ns, &pos, &kwd))
    return NULL;

  if (KosFormals_Apply(self, ns, pos, kwd)) {
    return NULL;
  } else {
    Py_RETURN_NONE;
  }
}


static PyMethodDef formals_methods[] = {
  { "apply", (PyCFunction) formals_m_apply, METH_VARARGS,
    "F.apply(ns, args, kwargs) => None" },

  { NULL, NULL, 0, NULL },
};


static PyObject *formals_call(PyObject *self,
                              PyObject *args, PyObject *kwds) {

  if (kwds) {
    PyErr_SetString(PyExc_TypeError, "Formals.apply does not accept"
                    " keyword arguments");
    return NULL;
  }

  return formals_m_apply(self, args);
}


static PyObject *formals_new(PyTypeObject *type,
                             PyObject *args, PyObject *kwds) {
  PyObject *pos = NULL;
  PyObject *pos_only = NULL;
  PyObject *kwd = NULL;
  PyObject *kwd_only = NULL;
  PyObject *star = NULL;
  PyObject *starstar = NULL;
  PyObject *defaults = NULL;

  if (! PyArg_ParseTuple(args, "OOOOOO|O:Formals",
                         &pos, &pos_only, &kwd, &kwd_only,
                         &star, &starstar, &defaults))
    return NULL;

  return KosFormals_NewFull(pos, pos_only, kwd, kwd_only,
                            star, starstar, defaults);
}


static void formals_dealloc(PyObject *self) {
  Py_CLEAR(KosFormals_POS(self));
  Py_CLEAR(KosFormals_POS_ONLY(self));
  Py_CLEAR(KosFormals_KWD(self));
  Py_CLEAR(KosFormals_KWD_ONLY(self));
  Py_CLEAR(KosFormals_STAR(self));
  Py_CLEAR(KosFormals_STARSTAR(self));
  Py_CLEAR(KosFormals_DEFAULTS(self));
  Py_TYPE(self)->tp_free(self);
}


static int formals_traverse(PyObject *self, visitproc visit, void *arg) {
  Py_VISIT(KosFormals_POS(self));
  Py_VISIT(KosFormals_POS_ONLY(self));
  Py_VISIT(KosFormals_KWD(self));
  Py_VISIT(KosFormals_KWD_ONLY(self));
  Py_VISIT(KosFormals_STAR(self));
  Py_VISIT(KosFormals_STARSTAR(self));
  Py_VISIT(KosFormals_DEFAULTS(self));
  return 0;
}


static int formals_clear(PyObject *self) {
  Py_CLEAR(KosFormals_POS(self));
  Py_CLEAR(KosFormals_POS_ONLY(self));
  Py_CLEAR(KosFormals_KWD(self));
  Py_CLEAR(KosFormals_KWD_ONLY(self));
  Py_CLEAR(KosFormals_STAR(self));
  Py_CLEAR(KosFormals_STARSTAR(self));
  Py_CLEAR(KosFormals_DEFAULTS(self));
  return 0;
}


PyTypeObject KosFormalsType = {
  PyVarObject_HEAD_INIT(NULL, 0)

  "Formals",
  sizeof(KosFormals),
  0,

  .tp_flags = Py_TPFLAGS_DEFAULT|Py_TPFLAGS_BASETYPE,
  .tp_members = formals_members,
  .tp_methods = formals_methods,
  .tp_call = formals_call,
  .tp_new = formals_new,
  .tp_dealloc = formals_dealloc,
  .tp_traverse = formals_traverse,
  .tp_clear = formals_clear,
};


PyObject *KosFormals_New(PyObject *spec, PyObject *defaults) {
  SAYF("KosFormals_New(%R, %R)", spec, defaults);

  if (! KosFormalsSpec_CheckExact(spec)) {
    PyErr_Format(PyExc_TypeError, "expected FormalsSpec, got %R", spec);
    return NULL;
  }

  return KosFormals_NewFull(
    KosFormalsSpec_POS(spec),
    KosFormalsSpec_POS_ONLY(spec),
    KosFormalsSpec_KWD(spec),
    KosFormalsSpec_KWD_ONLY(spec),
    KosFormalsSpec_STAR(spec),
    KosFormalsSpec_STARSTAR(spec),
    defaults);
}


PyObject *KosFormals_NewFull(
    PyObject *pos,
    PyObject *pos_only,
    PyObject *kwd,
    PyObject *kwd_only,
    PyObject *star,
    PyObject *starstar,
    PyObject *defaults) {

  PyObject *self = NULL;

  SAYF("KosFormals_NewFull(%R, %R,\n"
       "                   %R, %R,\n"
       "                   %R, %R,\n"
       "                   %R)",
       pos, pos_only, kwd, kwd_only, star, starstar, defaults);

  if (pos && (pos != Py_None) && (! PySequence_Check(pos))) {
    PyErr_Format(PyExc_TypeError, "pos must be a sequence,"
                 " not %R", pos);
    return NULL;
  }
  if (pos_only && (pos_only != Py_None) && (! PySequence_Check(pos_only))) {
    PyErr_Format(PyExc_TypeError, "pos_only must be a sequence,"
                 " not %R", pos_only);
    return NULL;
  }
  if (kwd && (kwd != Py_None) && (! PySequence_Check(kwd))) {
    PyErr_Format(PyExc_TypeError, "kwd must be a sequence,"
                 " not %R", kwd);
    return NULL;
  }
  if (kwd_only && (kwd_only != Py_None) && (! PySequence_Check(kwd_only))) {
    PyErr_Format(PyExc_TypeError, "kwd_only must be a sequence,"
                 " not %R", kwd_only);
    return NULL;
  }

  if (star == Py_None) {
    star = NULL;
  } else if (star) {
    if (! PyUnicode_Check(star)) {
      PyErr_Format(PyExc_TypeError, "star formal must be None or str,"
                   " not: %R", star);
      return NULL;
    }
  }

  if (starstar == Py_None) {
    starstar = NULL;
  } else if (starstar) {
    if (! PyUnicode_Check(starstar)) {
      PyErr_Format(PyExc_TypeError, "starstar formal must be None or str,"
                   " not: %R", starstar);
      return NULL;
    }
  }

  if (defaults == Py_None) {
    defaults = NULL;
  } else if (defaults) {
    if (! PyDict_Check(defaults)) {
      PyErr_Format(PyExc_TypeError, "defaults must be None or a dict,"
                   " not: %R", defaults);
      return NULL;
    }
  }

  self = PyObject_New(PyObject, &KosFormalsType);
  if (! self)
    return NULL;

  KosFormals_POS(self) = NULL;
  KosFormals_POS_ONLY(self) = NULL;
  KosFormals_KWD(self) = NULL;
  KosFormals_KWD_ONLY(self) = NULL;
  KosFormals_STAR(self) = NULL;
  KosFormals_STARSTAR(self) = NULL;
  KosFormals_DEFAULTS(self) = NULL;

  TUPLE_OR_NULL(pos);
  KosFormals_POS(self) = pos;

  TUPLE_OR_NULL(pos_only);
  KosFormals_POS_ONLY(self) = pos_only;

  TUPLE_OR_NULL(kwd);
  KosFormals_KWD(self) = kwd;

  TUPLE_OR_NULL(kwd_only);
  KosFormals_KWD_ONLY(self) = kwd_only;

  Py_XINCREF(star);
  KosFormals_STAR(self) = star;

  Py_XINCREF(starstar);
  KosFormals_STARSTAR(self) = starstar;

  if (defaults)
    defaults = PyDict_Copy(defaults);
  KosFormals_DEFAULTS(self) = defaults;

  return self;

 tuple_error:
  Py_DECREF(self);
  return NULL;
}


int KosFormals_Apply(
    PyObject *formals,
    PyObject *namespace,
    PyObject *args,
    PyObject *kwargs) {

  SAYF("in KosFormals_Apply(%R, %R, %R, %R)",
       formals, namespace, args, kwargs);

  if (! KosNamespace_Check(namespace)) {
    PyErr_Format(PyExc_TypeError, "namespace must be a Namespace instance,"
                 " not %R", namespace);
    return -1;
  }

  PyObject *pos = KosFormals_POS(formals);
  PyObject *pos_only = KosFormals_POS_ONLY(formals);
  PyObject *kwd = KosFormals_KWD(formals);
  PyObject *kwd_only = KosFormals_KWD_ONLY(formals);
  PyObject *star = KosFormals_STAR(formals);
  PyObject *starstar = KosFormals_STARSTAR(formals);
  PyObject *defaults = KosFormals_DEFAULTS(formals);

  SAYF("  pos      : %R", pos);
  SAYF("  pos_only : %R", pos_only);
  SAYF("  kwd      : %R", kwd);
  SAYF("  kwd_only : %R", kwd_only);
  SAYF("  star     : %R", star);
  SAYF("  starstar : %R", starstar);
  SAYF("  defaults : %R", defaults);

  ssize_t i, ipos = 0, ipos_only = 0, ikwd = 0, ikwd_only = 0;
  ssize_t l = args? PyTuple_GET_SIZE(args): 0;

  for (i = 0; i < l; i++) {
    SAYF("applying positional %i", i);

    if (pos) {
      if (KosNamespace_Store(namespace,
                             PyTuple_GET_ITEM(pos, ipos++),
                             PyTuple_GET_ITEM(args, i)))
        return -1;
      if (ipos >= PyTuple_GET_SIZE(pos))
        pos = NULL;

    } else if (pos_only) {
      if (KosNamespace_Store(namespace,
                             PyTuple_GET_ITEM(pos_only, ipos_only++),
                             PyTuple_GET_ITEM(args, i)))
        return -1;
      if (ipos_only >= PyTuple_GET_SIZE(pos_only))
        pos_only = NULL;

    } else if (kwd) {
      if (KosNamespace_Store(namespace,
                             PyTuple_GET_ITEM(kwd, ikwd++),
                             PyTuple_GET_ITEM(args, i)))
        return -1;
      if (ikwd >= PyTuple_GET_SIZE(kwd))
        kwd = NULL;

    } else if (star) {
      PyObject *sl = PyTuple_GetSlice(args, i, l);
      if (KosNamespace_Store(namespace, star, sl)) {
        Py_DECREF(sl);
        return -1;
      }
      Py_DECREF(sl);
      star = NULL;
      break;

    } else {
      PyErr_SetString(PyExc_TypeError, "too many positional arguments");
      return -1;
    }
  }

  if (pos_only) {
    PyObject *key = PyTuple_GET_ITEM(pos_only, ipos_only);
    PyErr_Format(PyExc_TypeError, "missing positional-only argument: %S",
                 key);
    return -1;
  }

  if (star) {
    // we've run out of positional args but are expecting a star arg,
    // so let's provide an empty one
    PyObject *sl = PyTuple_New(0);
    if (KosNamespace_Store(namespace, star, sl)) {
      Py_DECREF(sl);
      return -1;
    }
    Py_DECREF(sl);
    star = NULL;
  }

  if (! kwargs) {
    if (pos) {
      PyObject *key = PyTuple_GET_ITEM(pos, ipos);
      PyErr_Format(PyExc_TypeError, "missing positional argument: %S", key);
      return -1;

    } else if (kwd && ! defaults) {
      PyObject *key = PyTuple_GET_ITEM(kwd, ikwd);
      PyErr_Format(PyExc_TypeError, "missing keyword argument: %S", key);
      return -1;

    } else if (kwd_only && ! defaults) {
      PyObject *key = PyTuple_GET_ITEM(kwd_only, ikwd_only);
      PyErr_Format(PyExc_TypeError, "missing keyword-only argument: %S", key);
      return -1;
    }
  }

  if (kwargs) {
    kwargs = PyDict_Copy(kwargs);
    if (! kwargs)
      return -1;
  }

  if (pos) {
    // we're missing positional arguments, so let's see if we can dig
    // them up from kwargs
    PyObject *key = NULL;
    PyObject *val = NULL;

    l = PyTuple_GET_SIZE(pos);
    for (; ipos < l; ipos++) {
      key = PyTuple_GET_ITEM(pos, ipos);
      val = kwargs? PyDict_GetItem(kwargs, key): NULL;

      if (val)
        PyDict_DelItem(kwargs, key);

      if (! val) {
        PyErr_Format(PyExc_TypeError, "missing positional argument %S", key);
        goto fail_kwargs;
      }

      if (KosNamespace_Store(namespace, key, val)) {
        goto fail_kwargs;
      }
    }

    pos = NULL;
  }

  if (kwd) {
    // we're missing keyword arguments, so let's see if we can find
    // those in kwargs
    PyObject *key = NULL;
    PyObject *val = NULL;

    l = PyTuple_GET_SIZE(kwd);
    for (; ikwd < l; ikwd++) {
      key = PyTuple_GET_ITEM(kwd, ikwd);
      val = kwargs? PyDict_GetItem(kwargs, key): NULL;

      if (val) {
        PyDict_DelItem(kwargs, key);
      } else if (defaults) {
        val = PyDict_GetItem(defaults, key);
      }

      if (! val) {
        PyErr_Format(PyExc_TypeError, "missing keyword argument %S", key);
        goto fail_kwargs;
      }

      if (KosNamespace_Store(namespace, key, val)) {
        goto fail_kwargs;
      }
    }

    kwd = NULL;
  }

  if (kwd_only) {
    // we're missing keyword-only arguments, so let's see if we can
    // find those in kwargs
    PyObject *key = NULL;
    PyObject *val = NULL;

    l = PyTuple_GET_SIZE(kwd_only);
    for(; ikwd_only < l; ikwd_only++) {
      key = PyTuple_GET_ITEM(kwd_only, ikwd_only);
      val = kwargs? PyDict_GetItem(kwargs, key): NULL;

      if (val) {
        PyDict_DelItem(kwargs, key);
      } else if (defaults) {
        val = PyDict_GetItem(defaults, key);
      }

      if (! val) {
        PyErr_Format(PyExc_TypeError, "missing keyword-only argument %S", key);
        goto fail_kwargs;
      }

      if (KosNamespace_Store(namespace, key, val)) {
        goto fail_kwargs;
      }
    }

    kwd_only = NULL;
  }

  if (starstar) {
    if (! kwargs)
      kwargs = PyDict_NewPresized(0);

    KosNamespace_Store(namespace, starstar, kwargs);
    starstar = NULL;

  } else if (kwargs && PyDict_Size(kwargs)) {
    PyErr_Format(PyExc_TypeError, "unexpected keyword arguments %R", kwargs);
    goto fail_kwargs;
  }

  SAY("KosFormals_Apply -> 0");
  Py_XDECREF(kwargs);
  return 0;

 fail_kwargs:
  SAY("KosFormals_Apply -> fail_kwargs (-1)");
  Py_XDECREF(kwargs);
  return -1;
}


int KosFormals_VectorApply(
    PyObject *formals,
    PyObject *namespace,
    PyObject *const *args,
    size_t nargs,
    PyObject *names) {

  SAYF("in KosFormals_VectorApply(%R, %R, %p, %i, %R)",
       formals, namespace, args, nargs, names);

  if (! KosNamespace_Check(namespace)) {
    PyErr_Format(PyExc_TypeError, "namespace must be a Namespace instance,"
                 " not %R", namespace);
    return -1;
  }

  PyObject *pos = KosFormals_POS(formals);
  PyObject *pos_only = KosFormals_POS_ONLY(formals);
  PyObject *kwd = KosFormals_KWD(formals);
  PyObject *kwd_only = KosFormals_KWD_ONLY(formals);
  PyObject *star = KosFormals_STAR(formals);
  PyObject *starstar = KosFormals_STARSTAR(formals);
  PyObject *defaults = KosFormals_DEFAULTS(formals);

  SAYF("  pos      : %R", pos);
  SAYF("  pos_only : %R", pos_only);
  SAYF("  kwd      : %R", kwd);
  SAYF("  kwd_only : %R", kwd_only);
  SAYF("  star     : %R", star);
  SAYF("  starstar : %R", starstar);
  SAYF("  defaults : %R", defaults);

  ssize_t i, ipos = 0, ipos_only = 0, ikwd = 0, ikwd_only = 0;
  ssize_t l = PyVectorcall_NARGS(nargs);

  for (i = 0; i < l; i++) {
    SAYF("applying positional %i", i);

    if (pos) {
      if (KosNamespace_Store(namespace,
                             PyTuple_GET_ITEM(pos, ipos++),
                             args[i]))
        return -1;
      if (ipos >= PyTuple_GET_SIZE(pos))
        pos = NULL;

    } else if (pos_only) {
      if (KosNamespace_Store(namespace,
                             PyTuple_GET_ITEM(pos_only, ipos_only++),
                             args[i]))
        return -1;
      if (ipos_only >= PyTuple_GET_SIZE(pos_only))
        pos_only = NULL;

    } else if (kwd) {
      if (KosNamespace_Store(namespace,
                             PyTuple_GET_ITEM(kwd, ikwd++),
                             args[i]))
        return -1;
      if (ikwd >= PyTuple_GET_SIZE(kwd))
        kwd = NULL;

    } else if (star) {
      size_t nstar = l - i;
      PyObject *sl = PyTuple_New(nstar);
      for (size_t istar = 0; istar < nstar; istar++) {
        PyObject *a = args[i++];
        Py_INCREF(a);
        PyTuple_SET_ITEM(sl, istar, a);
      }

      if (KosNamespace_Store(namespace, star, sl)) {
        Py_DECREF(sl);
        return -1;
      }
      Py_DECREF(sl);
      star = NULL;
      break;

    } else {
      PyErr_SetString(PyExc_TypeError, "too many positional arguments");
      return -1;
    }
  }

  if (pos_only) {
    PyObject *key = PyTuple_GET_ITEM(pos_only, ipos_only);
    PyErr_Format(PyExc_TypeError, "missing positional-only argument: %S",
                 key);
    return -1;
  }

  if (star) {
    // we've run out of positional args but are expecting a star arg,
    // so let's provide an empty one
    PyObject *sl = PyTuple_New(0);
    if (KosNamespace_Store(namespace, star, sl)) {
      Py_DECREF(sl);
      return -1;
    }
    Py_DECREF(sl);
    star = NULL;
  }

  ssize_t k = names? PyTuple_GET_SIZE(names): 0;
  if (! k)
    names = NULL;

  if (! names) {
    if (pos) {
      PyObject *key = PyTuple_GET_ITEM(pos, ipos);
      PyErr_Format(PyExc_TypeError, "missing positional argument: %S", key);
      return -1;

    } else if (kwd && ! defaults) {
      PyObject *key = PyTuple_GET_ITEM(kwd, ikwd);
      PyErr_Format(PyExc_TypeError, "missing keyword argument: %S", key);
      return -1;

    } else if (kwd_only && ! defaults) {
      PyObject *key = PyTuple_GET_ITEM(kwd_only, ikwd_only);
      PyErr_Format(PyExc_TypeError, "missing keyword-only argument: %S", key);
      return -1;
    }
  }

  // TODO: optimize this to not create the intermediary kwargs dict except
  // when there's a star arg
  PyObject *kwargs = NULL;
  if (names) {
    kwargs = PyDict_NewPresized(k);
    for (i = 0; i < k; i++) {
      PyObject *k = PyTuple_GET_ITEM(names, i);
      PyObject *v = args[l + i];
      SAYF(" VEC NAME %i, %i, %R, %R", i, l + i, k, v);
      PyDict_SetItem(kwargs, k, v);
    }
  }

  if (pos) {
    // we're missing positional arguments, so let's see if we can dig
    // them up from kwargs
    PyObject *key = NULL;
    PyObject *val = NULL;

    l = PyTuple_GET_SIZE(pos);
    for (; ipos < l; ipos++) {
      key = PyTuple_GET_ITEM(pos, ipos);
      val = kwargs? PyDict_GetItem(kwargs, key): NULL;

      if (val)
        PyDict_DelItem(kwargs, key);

      if (! val) {
        PyErr_Format(PyExc_TypeError, "missing positional argument %S", key);
        goto fail_kwargs;
      }

      if (KosNamespace_Store(namespace, key, val))
        goto fail_kwargs;
    }

    pos = NULL;
  }

  if (kwd) {
    // we're missing keyword arguments, so let's see if we can find
    // those in kwargs
    PyObject *key = NULL;
    PyObject *val = NULL;

    l = PyTuple_GET_SIZE(kwd);
    for (; ikwd < l; ikwd++) {
      key = PyTuple_GET_ITEM(kwd, ikwd);
      val = kwargs? PyDict_GetItem(kwargs, key): NULL;

      if (val) {
        PyDict_DelItem(kwargs, key);
      } else if (defaults) {
        val = PyDict_GetItem(defaults, key);
      }

      if (! val) {
        PyErr_Format(PyExc_TypeError, "missing keyword argument %S", key);
        goto fail_kwargs;
      }

      if (KosNamespace_Store(namespace, key, val))
        goto fail_kwargs;
    }

    kwd = NULL;
  }

  if (kwd_only) {
    // we're missing keyword-only arguments, so let's see if we can
    // find those in kwargs
    PyObject *key = NULL;
    PyObject *val = NULL;

    l = PyTuple_GET_SIZE(kwd_only);
    for(; ikwd_only < l; ikwd_only++) {
      key = PyTuple_GET_ITEM(kwd_only, ikwd_only);
      val = kwargs? PyDict_GetItem(kwargs, key): NULL;

      if (val) {
        PyDict_DelItem(kwargs, key);
      } else if (defaults) {
        val = PyDict_GetItem(defaults, key);
      }

      if (! val) {
        PyErr_Format(PyExc_TypeError, "missing keyword-only argument %S", key);
        goto fail_kwargs;
      }

      if (KosNamespace_Store(namespace, key, val))
        goto fail_kwargs;
    }

    kwd_only = NULL;
  }

  if (starstar) {
    if (! kwargs)
      kwargs = PyDict_NewPresized(0);

    KosNamespace_Store(namespace, starstar, kwargs);
    starstar = NULL;

  } else if (kwargs && PyDict_Size(kwargs)) {
    PyErr_Format(PyExc_TypeError, "unexpected keyword arguments %R", kwargs);
    goto fail_kwargs;
  }

  SAY(" -> 0");
  Py_XDECREF(kwargs);
  return 0;

 fail_kwargs:
  SAY(" -> -1");
  Py_XDECREF(kwargs);
  return -1;
}


/* === Unit lifecycle === */


int koskari_formals_init(PyObject *mod) {
  if (! mod)
    return -1;

  PyObject *dict = PyModule_GetDict(mod);
  if (! dict)
    return -1;

  if (PyType_Ready(&KosFormalsType) ||
      PyType_Ready(&KosFormalsSpecType))
    return -1;

  PyDict_SetItemString(dict, "Formals",
                       (PyObject *) &KosFormalsType);

  PyDict_SetItemString(dict, "FormalsSpec",
                       (PyObject *) &KosFormalsSpecType);

  return 0;
}


int koskari_formals_clear(PyObject *mod) {

  return 0;
}


// The end.
