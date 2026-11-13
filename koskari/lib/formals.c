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

   :author: Christopher O'Brien <obriencj@gmail.com>
   :license: GNU General Public License v3
*/


#include "lib.h"
#include "debug.h"


/* seq becomes the tuple of its items, or NULL for None or an empty
   sequence; a failed size or tuple read jumps to tuple_error with the
   error set */
#define TUPLE_OR_NULL(seq)                        \
  if (seq) {                                      \
    if (seq == Py_None) {                         \
      seq = NULL;                                 \
    } else {                                      \
      Py_ssize_t _n = PySequence_Size(seq);       \
      if (_n < 0) goto tuple_error;               \
      if (_n == 0) {                              \
        seq = NULL;                               \
      } else {                                    \
        seq = PySequence_Tuple(seq);              \
        if (! seq) goto tuple_error;              \
      }                                           \
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

  { "by_refs",  T_OBJECT, offsetof(KosFormalsSpec, by_refs), READONLY,
    "tuple of names of by-ref formals" },

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
                                   PyObject *args, const PyObject *kwds) {
  PyObject *defaults = NULL;

  if (kwds) {
    PyErr_SetString(PyExc_TypeError, "FormalsSpec.apply does not accept"
                    " keyword arguments");
    return NULL;
  }

  if (! PyArg_ParseTuple(args, "|O", &defaults))
    return NULL;

  if (defaults == Py_None)
    defaults = NULL;

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
  PyObject *by_refs = NULL;

  if (! PyArg_ParseTuple(args, "|OOOOOOO:FormalsSpec",
                         &pos, &pos_only, &kwd, &kwd_only,
                         &star, &starstar, &by_refs))
    return NULL;

  return KosFormalsSpec_New(pos, pos_only, kwd, kwd_only,
                            star, starstar, by_refs);
}


static void formals_spec_dealloc(PyObject *self) {
  PyObject_GC_UnTrack(self);
  Py_CLEAR(KosFormalsSpec_POS(self));
  Py_CLEAR(KosFormalsSpec_POS_ONLY(self));
  Py_CLEAR(KosFormalsSpec_KWD(self));
  Py_CLEAR(KosFormalsSpec_KWD_ONLY(self));
  Py_CLEAR(KosFormalsSpec_STAR(self));
  Py_CLEAR(KosFormalsSpec_STARSTAR(self));
  Py_CLEAR(KosFormalsSpec_BY_REFS(self));
  Py_CLEAR(KosFormalsSpec_FORMALS(self));
  Py_TYPE(self)->tp_free(self);
}


static int formals_spec_traverse(PyObject *self, visitproc visit, void *arg) {
  Py_VISIT(KosFormalsSpec_POS(self));
  Py_VISIT(KosFormalsSpec_POS_ONLY(self));
  Py_VISIT(KosFormalsSpec_KWD(self));
  Py_VISIT(KosFormalsSpec_KWD_ONLY(self));
  Py_VISIT(KosFormalsSpec_STAR(self));
  Py_VISIT(KosFormalsSpec_STARSTAR(self));
  Py_VISIT(KosFormalsSpec_BY_REFS(self));
  Py_VISIT(KosFormalsSpec_FORMALS(self));
  return 0;
}


static int formals_spec_clear(PyObject *self) {
  Py_CLEAR(KosFormalsSpec_POS(self));
  Py_CLEAR(KosFormalsSpec_POS_ONLY(self));
  Py_CLEAR(KosFormalsSpec_KWD(self));
  Py_CLEAR(KosFormalsSpec_KWD_ONLY(self));
  Py_CLEAR(KosFormalsSpec_STAR(self));
  Py_CLEAR(KosFormalsSpec_STARSTAR(self));
  Py_CLEAR(KosFormalsSpec_BY_REFS(self));
  Py_CLEAR(KosFormalsSpec_FORMALS(self));
  return 0;
}


PyTypeObject KosFormalsSpecType = {
  PyVarObject_HEAD_INIT(NULL, 0)

  "FormalsSpec",
  sizeof(KosFormalsSpec),
  0,

  .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC,
  .tp_members = formals_spec_members,
  .tp_methods = formals_spec_methods,
  .tp_call = (ternaryfunc) formals_spec_call,
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
    PyObject *starstar,
    PyObject *by_refs) {

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

  if (by_refs && (by_refs != Py_None) && (! PySequence_Check(by_refs))) {
    PyErr_Format(PyExc_TypeError, "by_refs must be a sequence,"
                 " not %R", by_refs);
    return NULL;
  }

  PyObject *self = PyObject_GC_New(PyObject, &KosFormalsSpecType);
  if (! self)
    return NULL;

  // safety net pre-assignment
  KosFormalsSpec_POS(self) = NULL;
  KosFormalsSpec_POS_ONLY(self) = NULL;
  KosFormalsSpec_KWD(self) = NULL;
  KosFormalsSpec_KWD_ONLY(self) = NULL;
  KosFormalsSpec_STAR(self) = NULL;
  KosFormalsSpec_STARSTAR(self) = NULL;
  KosFormalsSpec_BY_REFS(self) = NULL;
  KosFormalsSpec_FORMALS(self) = NULL;

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

  TUPLE_OR_NULL(by_refs);
  KosFormalsSpec_BY_REFS(self) = by_refs;

  /* the Formals every defaults-free function of this spec shares */
  KosFormalsSpec_FORMALS(self) = KosFormals_NewFull(
      KosFormalsSpec_POS(self),
      KosFormalsSpec_POS_ONLY(self),
      KosFormalsSpec_KWD(self),
      KosFormalsSpec_KWD_ONLY(self),
      KosFormalsSpec_STAR(self),
      KosFormalsSpec_STARSTAR(self),
      NULL,
      KosFormalsSpec_BY_REFS(self));
  if (! KosFormalsSpec_FORMALS(self))
    goto tuple_error;

  /* fully built: reveal to the collector */
  PyObject_GC_Track(self);
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


/* Compare the names of src against names from offset on, advancing
   offset; same becomes 0 at a difference and -1 when a comparison
   raised. */
#define MATCH_OVER(names, src, offset, same)                            \
  do {                                                                  \
    PyObject *_src = (src);                                             \
    size_t l = SAFE_SIZE(_src);                                         \
    for (size_t i = 0; same > 0 && i < l; i++) {                        \
      PyObject *_v = PySequence_Fast_GET_ITEM(_src, i);                 \
      PyObject *_n = PyTuple_GET_ITEM(names, offset++);                 \
      same = Py_EQUALS(_v, _n);                                         \
    }                                                                   \
  } while (0)


/* Whether the c formal names, in their slot order, are the leading
   locals of nspec: 1 or 0, or -1 with an error set when a comparison
   raised. */
static int formals_lead_nspec(PyObject *pos, PyObject *pos_only,
                              PyObject *kwd, PyObject *kwd_only,
                              PyObject *star, PyObject *starstar,
                              size_t c, PyObject *nspec) {

  PyObject *names = KosNamespaceSpec_LOCAL_SYMS(nspec);
  size_t offset = 0;
  int same = 1;

  if ((size_t) PyTuple_GET_SIZE(names) < c)
    return 0;

  MATCH_OVER(names, pos, offset, same);
  MATCH_OVER(names, pos_only, offset, same);
  MATCH_OVER(names, kwd, offset, same);
  if (same > 0 && star) {
    PyObject *name = PyTuple_GET_ITEM(names, offset++);
    same = Py_EQUALS(star, name);
  }
  MATCH_OVER(names, kwd_only, offset, same);
  if (same > 0 && starstar) {
    PyObject *name = PyTuple_GET_ITEM(names, offset++);
    same = Py_EQUALS(starstar, name);
  }
  return same;
}


/* Append each item of seq that dst does not already hold. 0, or -1
   with an error set when a comparison raised or the append failed. */
static int list_unique_extend(PyObject *dst, PyObject *seq) {
  size_t l = SAFE_SIZE(seq);
  for (size_t i = 0; i < l; i++) {
    PyObject *val = PySequence_Fast_GET_ITEM(seq, i);
    int exists = 0;

    for (size_t j = SAFE_SIZE(dst); j--; ) {
      PyObject *obj = PyList_GET_ITEM(dst, j);
      int same = Py_EQUALS(val, obj);
      if (same < 0)
        return -1;
      if (same) {
        exists = 1;
        break;
      }
    }

    if (! exists && PyList_Append(dst, val) < 0)
      return -1;
  }
  return 0;
}


static PyObject *formals_nspec_impl(PyObject *pos, PyObject *pos_only,
                                   PyObject *kwd, PyObject *kwd_only,
                                   PyObject *star, PyObject *starstar,
                                   PyObject *base_nspec) {

  size_t c = SAFE_SIZE(pos) + SAFE_SIZE(pos_only)
           + SAFE_SIZE(kwd) + SAFE_SIZE(kwd_only);

  if (star)
    c++;
  if (starstar)
    c++;

  if (! c) {
    if (base_nspec) {
      Py_INCREF(base_nspec);
      return base_nspec;
    }
    return KosNamespaceSpec_New(NULL, NULL, NULL, NULL);
  }

  /* a code's own spec lists its function's formals first, and is then
     the function's layout as it stands */
  if (base_nspec) {
    int leads = formals_lead_nspec(pos, pos_only, kwd, kwd_only,
                                   star, starstar, c, base_nspec);
    if (leads < 0)
      return NULL;
    if (leads) {
      Py_INCREF(base_nspec);
      return base_nspec;
    }
  }

  PyObject *lcls = PyList_New(c);
  if (! lcls)
    return NULL;

  /* note: order of copying is important -- the KosFormals_ApplyFast and
  KosFormals_VectorApplyFast functions will use positional storage for pos,
  pos_only, kwd, and star -- kwd_only and starstar are stored at the end */
  size_t offset = 0;
  COPY_OVER(lcls, pos, offset);
  COPY_OVER(lcls, pos_only, offset);
  COPY_OVER(lcls, kwd, offset);

  if (star) {
    Py_INCREF(star);
    PyList_SET_ITEM(lcls, offset++, star);
  }

  COPY_OVER(lcls, kwd_only, offset);

  if (starstar) {
    Py_INCREF(starstar);
    PyList_SET_ITEM(lcls, offset++, starstar);
  }

  if (c != offset) {
    Py_DECREF(lcls);
    PyErr_Format(PyExc_RuntimeError, "c (%i) != offset (%i)",
                 (int) c, (int) offset);
    return NULL;
  }

  PyObject *name = NULL;
  PyObject *clsr = NULL;
  PyObject *glbl = NULL;

  if (base_nspec) {
    if (list_unique_extend(lcls, KosNamespaceSpec_LOCAL_SYMS(base_nspec)) < 0) {
      Py_DECREF(lcls);
      return NULL;
    }
    name = KosNamespaceSpec_NAME(base_nspec);
    clsr = KosNamespaceSpec_CLOSURE_SYMS(base_nspec);
    glbl = KosNamespaceSpec_GLOBAL_SYMS(base_nspec);
  }

  PyObject *result = KosNamespaceSpec_New(name, lcls, clsr, glbl);
  Py_DECREF(lcls);

  /* the closure syms are the base's own, so its recorded slots hold */
  if (result && base_nspec
      && KosNamespaceSpec_CopyClosureSlots(result, base_nspec) < 0)
    Py_CLEAR(result);
  return result;
}


PyObject *KosFormalsSpec_NamespaceSpec(PyObject *fspec,
                                       PyObject *nspec) {

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

  return formals_nspec_impl(
    KosFormalsSpec_POS(fspec),
    KosFormalsSpec_POS_ONLY(fspec),
    KosFormalsSpec_KWD(fspec),
    KosFormalsSpec_KWD_ONLY(fspec),
    KosFormalsSpec_STAR(fspec),
    KosFormalsSpec_STARSTAR(fspec),
    nspec);
}


PyObject *KosFormals_NamespaceSpec(PyObject *formals,
                                   PyObject *base_nspec) {

  if (! KosFormals_Check(formals)) {
    PyErr_Format(PyExc_TypeError, "Formals required, got %R", formals);
    return NULL;
  }

  if (base_nspec == Py_None)
    base_nspec = NULL;

  if (base_nspec && ! KosNamespaceSpec_CheckExact(base_nspec)) {
    PyErr_Format(PyExc_TypeError, "NamespaceSpec required, got %R",
                 base_nspec);
    return NULL;
  }

  return formals_nspec_impl(
    KosFormals_POS(formals),
    KosFormals_POS_ONLY(formals),
    KosFormals_KWD(formals),
    KosFormals_KWD_ONLY(formals),
    KosFormals_STAR(formals),
    KosFormals_STARSTAR(formals),
    base_nspec);
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

  { "by_refs",  T_OBJECT, offsetof(KosFormals, by_refs), READONLY,
    "tuple of names of by-ref formals copied from FormalsSpec" },

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
                              PyObject *args, const PyObject *kwds) {

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
  PyObject *by_refs = NULL;

  if (! PyArg_ParseTuple(args, "OOOOOO|OO:Formals",
                         &pos, &pos_only, &kwd, &kwd_only,
                         &star, &starstar, &defaults, &by_refs))
    return NULL;

  return KosFormals_NewFull(pos, pos_only, kwd, kwd_only,
                            star, starstar, defaults, by_refs);
}


static void formals_dealloc(PyObject *self) {
  PyObject_GC_UnTrack(self);
  Py_CLEAR(KosFormals_POS(self));
  Py_CLEAR(KosFormals_POS_ONLY(self));
  Py_CLEAR(KosFormals_KWD(self));
  Py_CLEAR(KosFormals_KWD_ONLY(self));
  Py_CLEAR(KosFormals_STAR(self));
  Py_CLEAR(KosFormals_STARSTAR(self));
  Py_CLEAR(KosFormals_DEFAULTS(self));
  Py_CLEAR(KosFormals_BY_REFS(self));
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
  Py_VISIT(KosFormals_BY_REFS(self));
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
  Py_CLEAR(KosFormals_BY_REFS(self));
  return 0;
}


PyTypeObject KosFormalsType = {
  PyVarObject_HEAD_INIT(NULL, 0)

  "Formals",
  sizeof(KosFormals),
  0,

  .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE | Py_TPFLAGS_HAVE_GC,
  .tp_members = formals_members,
  .tp_methods = formals_methods,
  .tp_call = (ternaryfunc) formals_call,
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

  if (! defaults)
    return Py_NewRef(KosFormalsSpec_FORMALS(spec));

  return KosFormals_NewFull(
    KosFormalsSpec_POS(spec),
    KosFormalsSpec_POS_ONLY(spec),
    KosFormalsSpec_KWD(spec),
    KosFormalsSpec_KWD_ONLY(spec),
    KosFormalsSpec_STAR(spec),
    KosFormalsSpec_STARSTAR(spec),
    defaults,
    KosFormalsSpec_BY_REFS(spec));
}


PyObject *KosFormals_NewFull(
    PyObject *pos,
    PyObject *pos_only,
    PyObject *kwd,
    PyObject *kwd_only,
    PyObject *star,
    PyObject *starstar,
    PyObject *defaults,
    PyObject *by_refs) {

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

  if (by_refs && (by_refs != Py_None) && (! PySequence_Check(by_refs))) {
    PyErr_Format(PyExc_TypeError, "by_refs must be a sequence,"
                 " not %R", by_refs);
    return NULL;
  }

  self = PyObject_GC_New(PyObject, &KosFormalsType);
  if (! self)
    return NULL;

  KosFormals_POS(self) = NULL;
  KosFormals_POS_ONLY(self) = NULL;
  KosFormals_KWD(self) = NULL;
  KosFormals_KWD_ONLY(self) = NULL;
  KosFormals_STAR(self) = NULL;
  KosFormals_STARSTAR(self) = NULL;
  KosFormals_DEFAULTS(self) = NULL;
  KosFormals_BY_REFS(self) = NULL;

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

  if (defaults) {
    defaults = PyDict_Copy(defaults);
    if (! defaults)
      goto tuple_error;
  }
  KosFormals_DEFAULTS(self) = defaults;

  TUPLE_OR_NULL(by_refs);
  KosFormals_BY_REFS(self) = by_refs;

  /* fully built: reveal to the collector */
  PyObject_GC_Track(self);
  return self;

 tuple_error:
  Py_DECREF(self);
  return NULL;
}


/* === Application errors === */


/*
  Raise a TypeError for a formals-application failure. When the call frame
  namespace carries a name -- the function name, for an ordinary call -- the
  message is prefixed with "<name>() " so the error identifies the callable;
  an anonymous frame degrades to the bare detail. fmt uses the
  PyUnicode_FromFormat conversions (%S, %R, %U, ...). Always returns -1, so
  call sites can `return formals_type_error(...)`.
*/
static int formals_type_error(PyObject *namespace, const char *fmt, ...) {
  va_list vargs;
  va_start(vargs, fmt);
  PyObject *detail = PyUnicode_FromFormatV(fmt, vargs);
  va_end(vargs);
  if (! detail)
    return -1;

  PyObject *name = namespace? KosNamespace_NAME(namespace): NULL;
  if (name && name != Py_None)
    PyErr_Format(PyExc_TypeError, "%S() %U", name, detail);
  else
    PyErr_SetObject(PyExc_TypeError, detail);

  Py_DECREF(detail);
  return -1;
}


/* === By-ref binding === */


/*
  Check that a caller-supplied by-ref argument is a Ref and install it as the
  slot's storage, so the local shares the caller's cell. A non-Ref is a bare
  value passed to a by-ref slot and raises TypeError. Expands a `return -1` on
  failure, so it is used both inline in the positional apply loops (no floor)
  and inside bind_supplied_by_ref (keyword by-ref, which adds the floor).
*/
#define BIND_SUPPLIED_BY_REF(ns, name, val)                          \
  do {                                                               \
    if (! KosRef_Check(val))                                         \
      return formals_type_error((ns),                               \
          "by-ref formal %S requires a Ref, not %R", (name), (val)); \
    if (KosNamespace_Bind((ns), (name), (val)))                      \
      return -1;                                                     \
  } while (0)


/*
  Bind a caller-supplied keyword by-ref argument: alias the caller's Ref as
  the slot, then -- if the cell is empty and the formal has a floor default --
  pre-fill the floor through it. Positional by-ref uses BIND_SUPPLIED_BY_REF
  directly; only keyword space carries a floor.
*/
static int bind_supplied_by_ref(PyObject *namespace, PyObject *name,
                                PyObject *val, PyObject *defaults) {
  BIND_SUPPLIED_BY_REF(namespace, name, val);

  if (defaults) {
    int empty = KosRef_IsEmpty(val);
    if (empty < 0)
      return -1;
    if (empty) {
      PyObject *floor = PyDict_GetItem(defaults, name);
      if (floor && KosRef_Store(val, floor))
        return -1;
    }
  }
  return 0;
}


/*
  Bind an omitted by-ref keyword slot to a fresh private Ref seeded with the
  floor (its default). The caller is not involved.
*/
static int bind_default_by_ref(PyObject *namespace, PyObject *name,
                               PyObject *floor) {
  PyObject *ref = KosSymbolicRef_New(name, floor);
  if (! ref)
    return -1;
  int res = KosNamespace_Bind(namespace, name, ref);
  Py_DECREF(ref);
  return res;
}


/* === Keyword source === */


#define KWSOURCE_SMALL 8

/**
   The keyword arguments of one call, viewed as parallel name and value
   vectors with a consumed mark per entry. A vectorcall supplies the
   vectors directly; a kwargs dict is unpacked into them. Nothing is
   copied or allocated for the common case of a few keywords and no
   starstar formal: the keyword phase takes entries by name, and a dict
   is only built at the end for the starstar binding or the error.
*/
typedef struct {
  PyObject *const *names;
  PyObject *const *values;
  unsigned char *used;
  Py_ssize_t count;
  Py_ssize_t remaining;
  PyObject **heap;
  PyObject *small_names[KWSOURCE_SMALL];
  PyObject *small_values[KWSOURCE_SMALL];
  unsigned char small_used[KWSOURCE_SMALL];
} KwSource;


static void kwsource_none(KwSource *ks) {
  ks->names = NULL;
  ks->values = NULL;
  ks->used = NULL;
  ks->count = 0;
  ks->remaining = 0;
  ks->heap = NULL;
}


/**
   View a vectorcall's names tuple and the values following the
   positionals. Borrows both; count entries.
*/
static int kwsource_from_vector(
    KwSource *ks,
    PyObject *names,
    PyObject *const *values) {

  Py_ssize_t k = names ? PyTuple_GET_SIZE(names) : 0;
  if (! k) {
    kwsource_none(ks);
    return 0;
  }

  ks->names = &PyTuple_GET_ITEM(names, 0);
  ks->values = values;
  ks->count = k;
  ks->remaining = k;
  ks->heap = NULL;
  if (k <= KWSOURCE_SMALL) {
    ks->used = ks->small_used;
  } else {
    ks->heap = PyMem_Calloc((size_t) k, 1);
    if (! ks->heap) {
      PyErr_NoMemory();
      return -1;
    }
    ks->used = (unsigned char *) ks->heap;
    return 0;
  }
  memset(ks->used, 0, (size_t) k);
  return 0;
}


/**
   Unpack a kwargs dict into borrowed name and value vectors. The dict
   must outlive the source; it is not modified.
*/
static int kwsource_from_dict(KwSource *ks, PyObject *kwds) {

  Py_ssize_t k = kwds ? PyDict_GET_SIZE(kwds) : 0;
  if (! k) {
    kwsource_none(ks);
    return 0;
  }

  PyObject **names;
  PyObject **values;
  ks->heap = NULL;
  if (k <= KWSOURCE_SMALL) {
    names = ks->small_names;
    values = ks->small_values;
    ks->used = ks->small_used;
  } else {
    /* one block: names, values, then the used bytes */
    size_t ptrs = (size_t) k * 2 * sizeof(PyObject *);
    ks->heap = PyMem_Malloc(ptrs + (size_t) k);
    if (! ks->heap) {
      PyErr_NoMemory();
      return -1;
    }
    names = ks->heap;
    values = ks->heap + k;
    ks->used = (unsigned char *) (ks->heap + 2 * k);
  }

  Py_ssize_t pos = 0, j = 0;
  PyObject *key, *val;
  while (PyDict_Next(kwds, &pos, &key, &val)) {
    names[j] = key;
    values[j] = val;
    j++;
  }
  memset(ks->used, 0, (size_t) k);

  ks->names = names;
  ks->values = values;
  ks->count = k;
  ks->remaining = k;
  return 0;
}


static void kwsource_fini(KwSource *ks) {
  if (ks->heap) {
    PyMem_Free(ks->heap);
    ks->heap = NULL;
  }
}


/**
   Take the entry named key: on a hit store its borrowed value in *out,
   mark it consumed, and return 1; return 0 when absent, -1 on a
   comparison error. Identity is tried before equality since keyword
   names are almost always interned.
*/
static int kwsource_take(KwSource *ks, PyObject *key, PyObject **out) {

  if (! ks->remaining)
    return 0;

  for (Py_ssize_t j = 0; j < ks->count; j++) {
    if (ks->used[j])
      continue;
    PyObject *nm = ks->names[j];
    int hit = (nm == key);
    if (! hit) {
      hit = PyObject_RichCompareBool(nm, key, Py_EQ);
      if (hit < 0)
        return -1;
    }
    if (hit) {
      ks->used[j] = 1;
      ks->remaining--;
      *out = ks->values[j];
      return 1;
    }
  }
  return 0;
}


/**
   Whether an entry named key remains, without taking it: 1, 0, or -1
   with an error set
*/
static int kwsource_has(KwSource *ks, PyObject *key) {

  if (! ks->remaining)
    return 0;

  for (Py_ssize_t j = 0; j < ks->count; j++) {
    if (ks->used[j])
      continue;
    PyObject *nm = ks->names[j];
    if (nm == key)
      return 1;
    int hit = PyObject_RichCompareBool(nm, key, Py_EQ);
    if (hit)
      return hit;
  }
  return 0;
}


/**
   A new dict of the entries not yet taken; empty when none remain
*/
static PyObject *kwsource_leftovers(KwSource *ks) {

  PyObject *d = PyDict_NewPresized(ks->remaining);
  if (! d)
    return NULL;
  for (Py_ssize_t j = 0; j < ks->count && ks->remaining; j++) {
    if (ks->used[j])
      continue;
    if (PyDict_SetItem(d, ks->names[j], ks->values[j]) < 0) {
      Py_DECREF(d);
      return NULL;
    }
  }
  return d;
}


/**
   Shared keyword phase: fill the pos, kwd, and kwd_only formals the
   positional phase did not reach from the keyword source and the
   defaults, then bind the leftovers to starstar or reject them.

   Under fast every plain store goes to the formal's layout slot: pos
   formal i at slot i, kwd formal i at kwd_base + i, kwd_only formal i
   at kwd_only_base + i, starstar at starstar_slot. By-ref formals bind
   a Ref by name on either path.
*/
static inline int apply_formals_kwargs_phase(
    PyObject *namespace,
    PyObject *pos,
    PyObject *kwd,
    PyObject *kwd_only,
    PyObject *starstar,
    PyObject *defaults,
    PyObject *by_refs,
    KwSource *ks,
    Py_ssize_t ipos,
    Py_ssize_t ikwd,
    Py_ssize_t ikwd_only,
    Py_ssize_t kwd_base,
    Py_ssize_t kwd_only_base,
    Py_ssize_t starstar_slot,
    const int fast) {

  Py_ssize_t l;
  int br, found;
  Py_ssize_t by_ref_count = by_refs ? PyTuple_GET_SIZE(by_refs) : 0;
  PyObject *key = NULL;
  PyObject *val = NULL;

#define FORMALS_KW_STORE(slot, name, val)                           \
  (fast ? KosNamespace_FastStore(namespace, (slot), (val))          \
        : KosNamespace_Store(namespace, (name), (val)))

  if (pos) {
    l = PyTuple_GET_SIZE(pos);
    for (; ipos < l; ipos++) {
      key = PyTuple_GET_ITEM(pos, ipos);
      found = kwsource_take(ks, key, &val);
      if (found < 0)
        return -1;
      if (! found)
        return formals_type_error(namespace,
                                  "missing positional argument %S", key);

      if (by_ref_count) {
        br = PySequence_Contains(by_refs, key);
        if (br < 0)
          return -1;
        if (br) {
          /* a by-ref positional supplied by keyword: required, no floor */
          if (KosNamespace_Bind(namespace, key, val))
            return -1;
          continue;
        }
      }
      if (FORMALS_KW_STORE(ipos, key, val))
        return -1;
    }
  }

  /* kwd and kwd_only bind the same way: a supplied value, else the
     default, else an error; by-ref formals alias the caller's Ref or
     take a private one seeded with the floor. */
  PyObject *runs[2] = { kwd, kwd_only };
  Py_ssize_t starts[2] = { ikwd, ikwd_only };
  Py_ssize_t bases[2] = { kwd_base, kwd_only_base };
  const char *missing[2] = {
    "missing keyword argument %S",
    "missing keyword-only argument %S",
  };

  for (int r = 0; r < 2; r++) {
    PyObject *run = runs[r];
    if (! run)
      continue;
    l = PyTuple_GET_SIZE(run);
    Py_ssize_t base = bases[r];
    for (Py_ssize_t ir = starts[r]; ir < l; ir++) {
      key = PyTuple_GET_ITEM(run, ir);
      val = NULL;
      found = kwsource_take(ks, key, &val);
      if (found < 0)
        return -1;
      if (! found && defaults)
        val = PyDict_GetItem(defaults, key);
      if (! found && ! val)
        return formals_type_error(namespace, missing[r], key);

      if (by_ref_count) {
        br = PySequence_Contains(by_refs, key);
        if (br < 0)
          return -1;
        if (br) {
          if (found) {
            if (bind_supplied_by_ref(namespace, key, val, defaults))
              return -1;
          } else if (bind_default_by_ref(namespace, key, val)) {
            return -1;
          }
          continue;
        }
      }
      if (FORMALS_KW_STORE(base + ir, key, val))
        return -1;
    }
  }

  if (starstar) {
    PyObject *rest = kwsource_leftovers(ks);
    if (! rest)
      return -1;
    int stored = FORMALS_KW_STORE(starstar_slot, starstar, rest);
    Py_DECREF(rest);
    return stored ? -1 : 0;
  }

#undef FORMALS_KW_STORE

  if (ks->remaining) {
    PyObject *rest = kwsource_leftovers(ks);
    if (! rest)
      return -1;
    formals_type_error(namespace, "unexpected keyword arguments %R", rest);
    Py_DECREF(rest);
    return -1;
  }

  return 0;
}


/* === Application === */


/**
   Bind one call's arguments into namespace according to formals.

   args[0..nargs) are the positionals, borrowed. ks is the call's
   keyword source, consumed by the keyword phase.

   fast is a compile-time constant at every call site, so each wrapper
   gets its own specialised body. With fast set the namespace was built
   from the formals' own NamespaceSpec, whose layout is pos, pos_only,
   kwd, star, kwd_only, starstar, so positional i is fast slot i and the
   star slot is fixed by the layout. Without it every store goes by
   name, which any namespace binding the formal names can satisfy.

   Positionals fill pos, then pos_only, then kwd; whatever remains
   slurps into star or is an error. The keyword phase then fills what
   the positionals did not reach from the keywords and defaults.
*/
static inline int formals_bind(
    PyObject *formals,
    PyObject *namespace,
    PyObject *const *args,
    Py_ssize_t nargs,
    KwSource *ks,
    const int fast) {

  /* The fast entries are reached only from the function call paths,
     which build the namespace from the formals' own spec; the by-name
     entry is Python-visible and checks its argument. */
  if (! fast && ! KosNamespace_Check(namespace)) {
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
  PyObject *by_refs = KosFormals_BY_REFS(formals);
  Py_ssize_t by_ref_count = by_refs ? PyTuple_GET_SIZE(by_refs) : 0;

  SAYF("formals_bind(%R, %R, nargs=%zd, keywords=%zd, fast=%d)",
       formals, namespace, nargs, ks->count, fast);

  /* Layout: pos, pos_only, kwd, star, kwd_only, starstar. */
  Py_ssize_t npos = pos ? PyTuple_GET_SIZE(pos) : 0;
  Py_ssize_t npos_only = pos_only ? PyTuple_GET_SIZE(pos_only) : 0;
  Py_ssize_t nkwd = kwd ? PyTuple_GET_SIZE(kwd) : 0;
  Py_ssize_t nkwd_only = kwd_only ? PyTuple_GET_SIZE(kwd_only) : 0;
  Py_ssize_t kwd_base = npos + npos_only;
  Py_ssize_t star_slot = kwd_base + nkwd;
  Py_ssize_t kwd_only_base = star_slot + (star ? 1 : 0);
  Py_ssize_t starstar_slot = kwd_only_base + nkwd_only;

  Py_ssize_t i = 0, ipos = 0, ipos_only = 0, ikwd = 0, ikwd_only = 0;
  Py_ssize_t l = nargs;

#define FORMALS_STORE(name, val)                                    \
  (fast ? KosNamespace_FastStore(namespace, i, (val))               \
        : KosNamespace_Store(namespace, (name), (val)))

  /* Positional phase. The by-ref test is hoisted out of each inner loop
     so the common path stays a tight store; only pos and kwd may carry
     by-ref formals. */
  if (pos) {
    if (by_ref_count) {
      for (; ipos < npos && i < l; ipos++, i++) {
        PyObject *nm = PyTuple_GET_ITEM(pos, ipos);
        PyObject *av = args[i];
        int br = PySequence_Contains(by_refs, nm);
        if (br < 0)
          return -1;
        if (br) {
          /* positional by-ref: required, no floor */
          BIND_SUPPLIED_BY_REF(namespace, nm, av);
        } else if (FORMALS_STORE(nm, av)) {
          return -1;
        }
      }
    } else {
      for (; ipos < npos && i < l; ipos++, i++)
        if (FORMALS_STORE(PyTuple_GET_ITEM(pos, ipos), args[i]))
          return -1;
    }
    if (ipos >= npos)
      pos = NULL;
  }

  if (pos_only && i < l) {
    for (; ipos_only < npos_only && i < l; ipos_only++, i++)
      if (FORMALS_STORE(PyTuple_GET_ITEM(pos_only, ipos_only), args[i]))
        return -1;
    if (ipos_only >= npos_only)
      pos_only = NULL;
  }

  if (kwd && i < l) {
    if (by_ref_count) {
      for (; ikwd < nkwd && i < l; ikwd++, i++) {
        PyObject *nm = PyTuple_GET_ITEM(kwd, ikwd);
        PyObject *av = args[i];
        int br = PySequence_Contains(by_refs, nm);
        if (br < 0)
          return -1;
        if (br) {
          if (bind_supplied_by_ref(namespace, nm, av, defaults))
            return -1;
        } else if (FORMALS_STORE(nm, av)) {
          return -1;
        }
      }
    } else {
      for (; ikwd < nkwd && i < l; ikwd++, i++)
        if (FORMALS_STORE(PyTuple_GET_ITEM(kwd, ikwd), args[i]))
          return -1;
    }
    if (ikwd >= nkwd)
      kwd = NULL;
  }

#undef FORMALS_STORE

  if (star) {
    /* Everything left slurps into star; nothing left binds (). Either
       way the slot is the layout's, not the argument count's: earlier
       runs may have been left to their defaults. */
    Py_ssize_t nstar = i < l ? l - i : 0;
    PyObject *sl = PyTuple_New(nstar);
    if (! sl)
      return -1;
    for (Py_ssize_t k = 0; k < nstar; k++) {
      PyObject *a = args[i + k];
      Py_INCREF(a);
      PyTuple_SET_ITEM(sl, k, a);
    }
    int stored = fast
        ? KosNamespace_FastStore(namespace, star_slot, sl)
        : KosNamespace_Store(namespace, star, sl);
    Py_DECREF(sl);
    if (stored)
      return -1;
    i += nstar;
  }

  if (i < l)
    return formals_type_error(namespace, "too many positional arguments");

  if (pos_only) {
    PyObject *key = PyTuple_GET_ITEM(pos_only, ipos_only);
    return formals_type_error(namespace,
                              "missing positional-only argument: %S", key);
  }

  if (! ks->count) {
    if (pos) {
      PyObject *key = PyTuple_GET_ITEM(pos, ipos);
      return formals_type_error(namespace,
                                "missing positional argument: %S", key);

    } else if (kwd && ! defaults) {
      PyObject *key = PyTuple_GET_ITEM(kwd, ikwd);
      return formals_type_error(namespace,
                                "missing keyword argument: %S", key);

    } else if (kwd_only && ! defaults) {
      PyObject *key = PyTuple_GET_ITEM(kwd_only, ikwd_only);
      return formals_type_error(namespace,
                                "missing keyword-only argument: %S", key);
    }
  }

  /* A keyword naming a formal the positionals already filled is a
     second value for it, as Python reports; positional-only formals
     are left out, their names being free for the starstar dict. The
     runs are read from the formals again, since a run filled to its
     end was dropped above. The scan runs only when keywords remain,
     and the names are interned, so a hit is an identity match before
     any comparison. */
  if (ks->remaining) {
    PyObject *filled[2] = { KosFormals_POS(formals), KosFormals_KWD(formals) };
    Py_ssize_t counts[2] = { ipos, ikwd };
    for (int r = 0; r < 2; r++) {
      for (Py_ssize_t k = 0; filled[r] && k < counts[r]; k++) {
        PyObject *key = PyTuple_GET_ITEM(filled[r], k);
        int found = kwsource_has(ks, key);
        if (found < 0)
          return -1;
        if (found)
          return formals_type_error(
              namespace, "got multiple values for argument %S", key);
      }
    }
  }

  return apply_formals_kwargs_phase(namespace, pos, kwd, kwd_only,
                                    starstar, defaults, by_refs, ks,
                                    ipos, ikwd, ikwd_only,
                                    kwd_base, kwd_only_base, starstar_slot,
                                    fast);
}


/**
   Positional items of a tuple as a borrowed vector, NULL for no tuple
*/
static inline PyObject *const *tuple_items(PyObject *args, Py_ssize_t *n) {

  if (! args) {
    *n = 0;
    return NULL;
  }
  *n = PyTuple_GET_SIZE(args);
  return PySequence_Fast_ITEMS(args);
}


static int formals_apply_tuple(
    PyObject *formals,
    PyObject *namespace,
    PyObject *args,
    PyObject *kwds,
    const int fast) {

  if (args && ! PyTuple_Check(args)) {
    PyErr_Format(PyExc_TypeError, "args must be a tuple, not %R", args);
    return -1;
  }
  if (kwds && ! PyDict_Check(kwds)) {
    PyErr_Format(PyExc_TypeError, "kwds must be a dict, not %R", kwds);
    return -1;
  }

  Py_ssize_t nargs;
  PyObject *const *items = tuple_items(args, &nargs);
  KwSource ks;
  if (kwsource_from_dict(&ks, kwds) < 0)
    return -1;

  int res = formals_bind(formals, namespace, items, nargs, &ks, fast);
  kwsource_fini(&ks);
  return res;
}


int KosFormals_ApplyFast(
    PyObject *formals,
    PyObject *namespace,
    PyObject *args,
    PyObject *kwds) {

  return formals_apply_tuple(formals, namespace, args, kwds, 1);
}


int KosFormals_Apply(
    PyObject *formals,
    PyObject *namespace,
    PyObject *args,
    PyObject *kwds) {

  return formals_apply_tuple(formals, namespace, args, kwds, 0);
}


int KosFormals_VectorApplyFast(
    PyObject *formals,
    PyObject *namespace,
    PyObject *const *args,
    size_t nargs,
    PyObject *names) {

  Py_ssize_t l = PyVectorcall_NARGS(nargs);
  KwSource ks;
  if (kwsource_from_vector(&ks, names, args + l) < 0)
    return -1;

  int res = formals_bind(formals, namespace, args, l, &ks, 1);
  kwsource_fini(&ks);
  return res;
}


/* === Simple-call steal binding === */



KOS_NOINLINE int kos_formals_steal_underrun(
    const KosSegment *block,
    Py_ssize_t nargs) {

  PyErr_Format(PyExc_RuntimeError, "formals steal underrun."
               " requested: %zu, available: %zu", (size_t) nargs,
               block ? KosSegment_LEN(block) : 0);
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


int koskari_formals_clear(const PyObject *mod) {

  (void) mod;
  return 0;
}


// The end.
