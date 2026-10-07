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
   koskari/lib/partial.c

   KosPartial: a standalone native KosCallable with the behavior of
   functools.partial.

   It binds a callable together with leading positional args and keyword args;
   calling it merges those with the call-site args and invokes the inner
   callable. Construction flattens nested KosPartials, the same way
   functools.partial collapses partial(partial(f, a), b).

   Unlike functools.partial it is not in that type's inheritance tree -- the
   match is by duck typing, not isinstance. The payoff is that it is a static
   KosEvaluatedTypeObject: when koskari bytecode calls it, the install slot
   merges the arguments and calls the inner callable through the
   evaluator's call entry, with no intermediate KosContext.

   :author: Christopher O'Brien <obriencj@gmail.com>
   :license: GNU General Public License v3
*/


#include "lib.h"


/* === instance === */


typedef struct {
  PyObject_HEAD
  PyObject *func;        /* callable; non-NULL once constructed */
  PyObject *args;        /* tuple of bound positional args; non-NULL */
  PyObject *keywords;    /* dict of bound keyword args; non-NULL */
  PyObject *dict;        /* instance __dict__; lazily created, may be NULL */
  PyObject *weakreflist; /* weak references; may be NULL */
  vectorcallfunc vectorcall;
} KosPartialObject;


/* === Python-side call === */


/**
   The Python vectorcall entry, and the type's tp_call through
   PyVectorcall_Call: the bound arguments merged with the call vector
   by the same spread the install uses, then the inner vectorcalled.
   Returns whatever the inner returns (a coroutine for an async
   Koskari callable, a value otherwise).
*/
static PyObject *kospartial_vectorcall(
    PyObject *self,
    PyObject *const *args,
    size_t nargsf,
    PyObject *kwnames) {

  KosPartialObject *p = (KosPartialObject *) self;
  Py_ssize_t nargs = PyVectorcall_NARGS(nargsf);
  PyObject *small[16];
  KosArgSpread spread;
  PyObject *result;

  if (KosArgSpread_InitMerged(
          &spread, p->args, p->keywords, args, nargs, kwnames, small, 16) < 0)
    return NULL;
  result = PyObject_Vectorcall(
      p->func, spread.argv, (size_t) spread.nargs, spread.kwnames);
  KosArgSpread_Clear(&spread);
  return result;
}


/* === KosCallable install === */


/**
   Evaluator install: call the inner through the evaluator's call entry
   with the bound arguments merged in, whatever the inner is. A
   KosCallable installs or runs in this evaluator. A Python callable
   is called and its awaitable result awaited. The merge is one
   borrowed vector, built beside the call.
*/
static KosOpResult kospartial_kos_vinstall(
    KosEvalState *es,
    PyObject *callable,
    PyObject *const *args,
    Py_ssize_t nargs,
    PyObject *kwnames,
    int tail_call) {

  KosPartialObject *p = (KosPartialObject *) callable;
  PyObject *small[16];
  KosArgSpread spread;
  KosOpResult r;

  if (KosArgSpread_InitMerged(
          &spread, p->args, p->keywords, args, nargs, kwnames, small, 16) < 0)
    return EVAL_RAISE();
  r = KosEvalState_Call(
      es, p->func, spread.argv, spread.nargs, spread.kwnames, tail_call,
      NULL, NULL);
  KosArgSpread_Clear(&spread);
  return r;
}


/* === construction === */


/**
   KosPartial(func, *args, **keywords). func must be callable. A KosPartial
   func is flattened: its bound args/keywords are folded in and its own func is
   used, so the result never nests.
*/
static PyObject *kospartial_new(
    PyTypeObject *type,
    PyObject *args,
    PyObject *kwds) {

  if (PyTuple_GET_SIZE(args) < 1) {
    PyErr_SetString(PyExc_TypeError,
                    "KosPartial takes at least one argument");
    return NULL;
  }

  PyObject *func = PyTuple_GET_ITEM(args, 0);
  if (! PyCallable_Check(func)) {
    PyErr_SetString(PyExc_TypeError, "the first argument must be callable");
    return NULL;
  }

  PyObject *rest = PyTuple_GetSlice(args, 1, PyTuple_GET_SIZE(args));
  if (! rest)
    return NULL;

  PyObject *new_args = NULL;
  PyObject *new_kw = NULL;

  if (KosPartial_CheckExact(func)) {
    KosPartialObject *inner = (KosPartialObject *) func;

    new_args = PySequence_Concat(inner->args, rest);
    Py_DECREF(rest);
    if (! new_args)
      return NULL;

    new_kw = PyDict_Copy(inner->keywords);
    if (! new_kw) {
      Py_DECREF(new_args);
      return NULL;
    }
    if (KosKeywords_Update(new_kw, kwds) < 0) {
      Py_DECREF(new_args);
      Py_DECREF(new_kw);
      return NULL;
    }

    func = inner->func;  /* borrowed; incref below */

  } else {
    new_args = rest;  /* steal the slice reference */
    new_kw = PyDict_New();
    if (! new_kw || KosKeywords_Update(new_kw, kwds) < 0) {
      Py_DECREF(new_args);
      Py_XDECREF(new_kw);
      return NULL;
    }
  }

  KosPartialObject *self = (KosPartialObject *) type->tp_alloc(type, 0);
  if (! self) {
    Py_DECREF(new_args);
    Py_DECREF(new_kw);
    return NULL;
  }

  Py_INCREF(func);
  self->func = func;
  self->args = new_args;
  self->keywords = new_kw;
  self->dict = NULL;
  self->weakreflist = NULL;
  self->vectorcall = kospartial_vectorcall;
  return (PyObject *) self;
}


/* === repr === */


static PyObject *kospartial_repr(PyObject *self) {

  KosPartialObject *p = (KosPartialObject *) self;

  int status = Py_ReprEnter(self);
  if (status != 0)
    return (status < 0) ? NULL : PyUnicode_FromString("...");

  PyObject *result = NULL;
  PyObject *parts = PyList_New(0);
  if (! parts)
    goto done;

  {
    PyObject *fr = PyObject_Repr(p->func);
    if (! fr || PyList_Append(parts, fr) < 0) {
      Py_XDECREF(fr);
      goto done;
    }
    Py_DECREF(fr);
  }

  Py_ssize_t na = PyTuple_GET_SIZE(p->args);
  for (Py_ssize_t i = 0; i < na; i++) {
    PyObject *ar = PyObject_Repr(PyTuple_GET_ITEM(p->args, i));
    if (! ar || PyList_Append(parts, ar) < 0) {
      Py_XDECREF(ar);
      goto done;
    }
    Py_DECREF(ar);
  }

  {
    PyObject *k, *v;
    Py_ssize_t pos = 0;
    while (PyDict_Next(p->keywords, &pos, &k, &v)) {
      PyObject *vr = PyObject_Repr(v);
      if (! vr)
        goto done;
      PyObject *kv = PyUnicode_FromFormat("%S=%U", k, vr);
      Py_DECREF(vr);
      if (! kv || PyList_Append(parts, kv) < 0) {
        Py_XDECREF(kv);
        goto done;
      }
      Py_DECREF(kv);
    }
  }

  {
    PyObject *sep = PyUnicode_FromString(", ");
    if (! sep)
      goto done;
    PyObject *joined = PyUnicode_Join(sep, parts);
    Py_DECREF(sep);
    if (! joined)
      goto done;
    {
      const char *tn = Py_TYPE(self)->tp_name;
      const char *dot = strrchr(tn, '.');
      result = PyUnicode_FromFormat("%s(%U)", dot ? dot + 1 : tn, joined);
    }
    Py_DECREF(joined);
  }

done:
  Py_XDECREF(parts);
  Py_ReprLeave(self);
  return result;
}


/**
   __kos_repr__: the Koskari s-expression form,

     (partial <func> <arg>... <kwd>: <val> ...)

   func and each positional render via KosUtils_Repr (their own kos form);
   keywords render as a name-plus-colon token followed by the value's kos
   form. Registered into the kos_repr registry by koskari_partial_init.
*/
static PyObject *kospartial_kos_repr(
    PyObject *self,
    PyObject *Py_UNUSED(ignored)) {

  KosPartialObject *p = (KosPartialObject *) self;

  int status = Py_ReprEnter(self);
  if (status != 0)
    return (status < 0) ? NULL : PyUnicode_FromString("...");

  PyObject *result = NULL;
  PyObject *parts = PyList_New(0);
  if (! parts)
    goto done;

  {
    PyObject *head = PyUnicode_FromString("partial");
    if (! head || PyList_Append(parts, head) < 0) {
      Py_XDECREF(head);
      goto done;
    }
    Py_DECREF(head);
  }

  {
    PyObject *fr = KosUtils_Repr(p->func);
    if (! fr || PyList_Append(parts, fr) < 0) {
      Py_XDECREF(fr);
      goto done;
    }
    Py_DECREF(fr);
  }

  Py_ssize_t na = PyTuple_GET_SIZE(p->args);
  for (Py_ssize_t i = 0; i < na; i++) {
    PyObject *ar = KosUtils_Repr(PyTuple_GET_ITEM(p->args, i));
    if (! ar || PyList_Append(parts, ar) < 0) {
      Py_XDECREF(ar);
      goto done;
    }
    Py_DECREF(ar);
  }

  {
    PyObject *k, *v;
    Py_ssize_t pos = 0;
    while (PyDict_Next(p->keywords, &pos, &k, &v)) {
      PyObject *kwd = PyUnicode_FromFormat("%U:", k);
      if (! kwd || PyList_Append(parts, kwd) < 0) {
        Py_XDECREF(kwd);
        goto done;
      }
      Py_DECREF(kwd);

      PyObject *vr = KosUtils_Repr(v);
      if (! vr || PyList_Append(parts, vr) < 0) {
        Py_XDECREF(vr);
        goto done;
      }
      Py_DECREF(vr);
    }
  }

  {
    PyObject *sep = PyUnicode_FromString(" ");
    if (! sep)
      goto done;
    PyObject *joined = PyUnicode_Join(sep, parts);
    Py_DECREF(sep);
    if (! joined)
      goto done;
    result = PyUnicode_FromFormat("(%U)", joined);
    Py_DECREF(joined);
  }

done:
  Py_XDECREF(parts);
  Py_ReprLeave(self);
  return result;
}


/* === pickle === */


static PyObject *kospartial_reduce(PyObject *self, PyObject *unused) {

  KosPartialObject *p = (KosPartialObject *) self;
  PyObject *d = p->dict ? p->dict : Py_None;

  (void) unused;
  return Py_BuildValue("O(O)(OOOO)",
      (PyObject *) Py_TYPE(self), p->func,
      p->func, p->args, p->keywords, d);
}


static PyObject *kospartial_setstate(PyObject *self, PyObject *state) {

  KosPartialObject *p = (KosPartialObject *) self;

  if (! PyTuple_Check(state) || PyTuple_GET_SIZE(state) != 4) {
    PyErr_SetString(PyExc_TypeError, "invalid KosPartial state");
    return NULL;
  }

  PyObject *func = PyTuple_GET_ITEM(state, 0);
  PyObject *fnargs = PyTuple_GET_ITEM(state, 1);
  PyObject *kw = PyTuple_GET_ITEM(state, 2);
  PyObject *d = PyTuple_GET_ITEM(state, 3);

  if (! PyCallable_Check(func) || ! PyTuple_Check(fnargs)
      || (kw != Py_None && ! PyDict_Check(kw))
      || (d != Py_None && ! PyDict_Check(d))) {
    PyErr_SetString(PyExc_TypeError, "invalid KosPartial state");
    return NULL;
  }

  PyObject *new_args = PySequence_Tuple(fnargs);
  if (! new_args)
    return NULL;

  PyObject *new_kw = PyDict_New();
  if (! new_kw
      || KosKeywords_Update(new_kw, (kw == Py_None) ? NULL : kw) < 0) {
    Py_DECREF(new_args);
    Py_XDECREF(new_kw);
    return NULL;
  }

  Py_INCREF(func);
  Py_SETREF(p->func, func);
  Py_SETREF(p->args, new_args);
  Py_SETREF(p->keywords, new_kw);

  if (d == Py_None) {
    Py_CLEAR(p->dict);
  } else {
    Py_INCREF(d);
    Py_XSETREF(p->dict, d);
  }

  Py_RETURN_NONE;
}


/* === GC + dealloc === */


static int kospartial_traverse(PyObject *self, visitproc visit, void *arg) {

  KosPartialObject *p = (KosPartialObject *) self;
  Py_VISIT(p->func);
  Py_VISIT(p->args);
  Py_VISIT(p->keywords);
  Py_VISIT(p->dict);
  return 0;
}


static int kospartial_clear(PyObject *self) {

  KosPartialObject *p = (KosPartialObject *) self;
  Py_CLEAR(p->func);
  Py_CLEAR(p->args);
  Py_CLEAR(p->keywords);
  Py_CLEAR(p->dict);
  return 0;
}


static void kospartial_dealloc(PyObject *self) {

  KosPartialObject *p = (KosPartialObject *) self;

  PyObject_GC_UnTrack(self);
  if (p->weakreflist)
    PyObject_ClearWeakRefs(self);

  Py_CLEAR(p->func);
  Py_CLEAR(p->args);
  Py_CLEAR(p->keywords);
  Py_CLEAR(p->dict);
  Py_TYPE(self)->tp_free(self);
}


/* === KosPartial type === */


static PyMemberDef kospartial_members[] = {
  {"func", T_OBJECT, offsetof(KosPartialObject, func), READONLY,
   "function object to use in future partial calls"},
  {"args", T_OBJECT, offsetof(KosPartialObject, args), READONLY,
   "tuple of arguments to future partial calls"},
  {"keywords", T_OBJECT, offsetof(KosPartialObject, keywords), READONLY,
   "dictionary of keyword arguments to future partial calls"},
  {NULL},
};


static PyGetSetDef kospartial_getset[] = {
  {"__dict__", PyObject_GenericGetDict, PyObject_GenericSetDict, NULL, NULL},
  {NULL},
};


static PyMethodDef kospartial_methods[] = {
  {"__reduce__", kospartial_reduce, METH_NOARGS, NULL},
  {"__setstate__", kospartial_setstate, METH_O, NULL},
  {"__kos_repr__", kospartial_kos_repr, METH_NOARGS,
   "P.__kos_repr__() -> str\n"
   "Koskari-style (partial func args... kwd: val ...) form."},
  {NULL},
};


KosEvaluatedTypeObject KosPartialCallableType = {
  .type = {
    PyVarObject_HEAD_INIT(NULL, 0)

    .tp_name = "koskari.lib.partial",
    .tp_basicsize = sizeof(KosPartialObject),
    .tp_itemsize = 0,
    .tp_dealloc = kospartial_dealloc,
    .tp_repr = kospartial_repr,
    .tp_call = PyVectorcall_Call,
    .tp_vectorcall_offset = offsetof(KosPartialObject, vectorcall),
    .tp_getattro = PyObject_GenericGetAttr,
    .tp_setattro = PyObject_GenericSetAttr,
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC | Py_TPFLAGS_BASETYPE
        | Py_TPFLAGS_HAVE_VECTORCALL,
    .tp_doc = "KosPartial(func, *args, **keywords) - native KosCallable with "
              "the behavior of functools.partial",
    .tp_traverse = kospartial_traverse,
    .tp_clear = kospartial_clear,
    .tp_weaklistoffset = offsetof(KosPartialObject, weakreflist),
    .tp_methods = kospartial_methods,
    .tp_members = kospartial_members,
    .tp_getset = kospartial_getset,
    .tp_dictoffset = offsetof(KosPartialObject, dict),
    .tp_new = kospartial_new,
    .tp_base = KOS_EVALUATED_PYTYPE(KosEvaluatedType),
  },
  .kos_call = {
    .kc_vinstall = kospartial_kos_vinstall,
    .kc_pycall = NULL,
    .kc_pyvectorcall = kospartial_vectorcall,
  },
};


/* === module lifecycle === */


int koskari_partial_init(PyObject *mod) {

  if (PyType_Ready(KOS_EVALUATED_PYTYPE(KosPartialCallableType)) < 0)
    return -1;

  if (KosUtils_ReprRegisterBuiltin(
          (PyObject *) KOS_EVALUATED_PYTYPE(KosPartialCallableType)) < 0)
    return -1;

  PyObject *dict = PyModule_GetDict(mod);
  if (! dict)
    return -1;

  if (PyDict_SetItemString(dict, "KosPartial",
                           (PyObject *) KOS_EVALUATED_PYTYPE(KosPartialCallableType)) < 0)
    return -1;

  /* short alias matching the class __name__, so pickle and koskari
     both resolve the type by its own name */
  if (PyDict_SetItemString(dict, "partial",
                           (PyObject *) KOS_EVALUATED_PYTYPE(KosPartialCallableType)) < 0)
    return -1;

  return 0;
}


int koskari_partial_clear(PyObject *mod) {

  (void) mod;
  return 0;
}


// The end.
