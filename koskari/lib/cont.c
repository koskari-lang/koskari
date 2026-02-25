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
  koskari/lib/cont.c

  KosPyDelimitedContinuation and KosPyContinuation. Python wrappers
  for captured continuations (shift and call/cc).

  :author: Christopher O'Brien <obriencj@preoccupied.net>
  :license: GNU General Public License v3
*/


#include "lib.h"


/* === KosPyDelimitedContinuation type === */


static void delimited_continuation_dealloc(PyObject *self) {
  KosPyDelimitedContinuation *cont = (KosPyDelimitedContinuation *) self;

  kos_continuation_free_exclude(KosPyDelimitedContinuation_CAPTURED(cont), self);

  Py_TYPE(self)->tp_free(self);
}


static int delimited_continuation_traverse(
    PyObject *self,
    visitproc visit,
    void *arg) {

  KosPyDelimitedContinuation *cont = (KosPyDelimitedContinuation *) self;

  if (KosPyDelimitedContinuation_CAPTURED(cont)) {
    return kos_continuation_traverse(KosPyDelimitedContinuation_CAPTURED(cont), visit, arg);
  }

  return 0;
}


static int delimited_continuation_clear(PyObject *self) {

  KosPyDelimitedContinuation *cont = (KosPyDelimitedContinuation *) self;

  kos_continuation_free(KosPyDelimitedContinuation_CAPTURED(cont));
  KosPyDelimitedContinuation_CAPTURED(cont) = NULL;

  return 0;
}


static PyObject *delimited_continuation_call(
    PyObject *self,
    PyObject *args,
    PyObject *kwds) {

  (void) args;
  (void) kwds;
  (void) self;

  PyErr_SetString(PyExc_TypeError,
                  "DelimitedContinuation cannot be called from Python");
  return NULL;
}


static PyObject *delimited_continuation_repr(PyObject *self) {

  return PyUnicode_FromFormat("<DelimitedContinuation at %p>", self);
}


KosCallableTypeObject KosPyDelimitedContinuationType = {
  .type = {
    PyVarObject_HEAD_INIT(NULL, 0)

    .tp_name = "koskari.lib.DelimitedContinuation",
    .tp_basicsize = sizeof(KosPyDelimitedContinuation),
    .tp_itemsize = 0,
    .tp_dealloc = delimited_continuation_dealloc,
    .tp_repr = delimited_continuation_repr,
    .tp_call = delimited_continuation_call,
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC,
    .tp_doc = "Captured delimited continuation",
    .tp_traverse = delimited_continuation_traverse,
    .tp_clear = delimited_continuation_clear,
    .tp_base = KOS_CALLABLE_PYTYPE(KosCallableType),
  },
  .tp_kos_install = kos_install_continuation,
  .tp_kos_tail_install = NULL,
};


/**
  KosPyDelimitedContinuation wraps a KosContinuation captured by shift.

  DelimitedContinuation cannot be called from Python; application is only
  permitted via CALL inside koskari_eval.
*/
PyObject *KosPyDelimitedContinuation_New(KosContinuation *captured) {

  KosPyDelimitedContinuation *cont = PyObject_GC_New(
    KosPyDelimitedContinuation,
    KOS_CALLABLE_PYTYPE(KosPyDelimitedContinuationType));

  if (! cont)
    return NULL;

  KosPyDelimitedContinuation_CAPTURED(cont) = captured;

  PyObject_GC_Track((PyObject *) cont);
  return (PyObject *) cont;
}


/* === KosPyContinuation type (undelimited, for future call/cc) === */


static void continuation_dealloc(PyObject *self) {

  KosPyContinuation *cont = (KosPyContinuation *) self;

  kos_continuation_free_exclude(KosPyContinuation_CAPTURED(cont), self);

  Py_TYPE(self)->tp_free(self);
}


static int continuation_traverse(
    PyObject *self,
    visitproc visit,
    void *arg) {

  KosPyContinuation *cont = (KosPyContinuation *) self;

  if (KosPyContinuation_CAPTURED(cont)) {
    return kos_continuation_traverse(KosPyContinuation_CAPTURED(cont), visit, arg);
  }

  return 0;
}


static int continuation_clear(PyObject *self) {

  KosPyContinuation *cont = (KosPyContinuation *) self;

  kos_continuation_free(KosPyContinuation_CAPTURED(cont));
  KosPyContinuation_CAPTURED(cont) = NULL;

  return 0;
}


static PyObject *continuation_call(
    PyObject *self,
    PyObject *args,
    PyObject *kwds) {

  KosPyContinuation *cont = (KosPyContinuation *) self;
  (void) kwds;

  if (! KosPyContinuation_CAPTURED(cont)) {
    PyErr_SetString(PyExc_RuntimeError,
                    "continuation has no captured state");
    return NULL;
  }

  if (! KoskariContinuationApply) {
    PyErr_SetString(PyExc_RuntimeError,
                    "KoskariContinuationApply not initialized");
    return NULL;
  }

  {
    PyObject *token_ref = KosContinuation_TOKEN_REF(KosPyContinuation_CAPTURED(cont));
    int target_dead = 0;
    if (! token_ref) {
      target_dead = 1;
    } else if (PyObject_TypeCheck(token_ref, &KosEvaluatorTokenType)) {
      KosEvaluatorToken *token = (KosEvaluatorToken *) token_ref;
      if (token->live_depth == 0)
        target_dead = 1;
    }
    if (target_dead) {
      if (KoskariContinuationDead)
        PyErr_SetString(KoskariContinuationDead,
                        "continuation target context is no longer alive");
      else
        PyErr_SetString(PyExc_RuntimeError,
                        "continuation target context is no longer alive");
      return NULL;
    }
  }

  PyObject *resume_val = Py_None;
  if (args && PyTuple_GET_SIZE(args) > 0)
    resume_val = PyTuple_GET_ITEM(args, 0);
  Py_INCREF(resume_val);

  PyObject *exc = PyObject_CallNoArgs(KoskariContinuationApply);
  if (! exc) {
    Py_DECREF(resume_val);
    return NULL;
  }
  if (PyObject_SetAttrString(
      exc, "target",
      KosContinuation_TOKEN_REF(KosPyContinuation_CAPTURED(cont))) < 0) {
    Py_DECREF(exc);
    Py_DECREF(resume_val);
    return NULL;
  }
  if (PyObject_SetAttrString(exc, "continuation", self) < 0) {
    Py_DECREF(exc);
    Py_DECREF(resume_val);
    return NULL;
  }
  PyObject *args_tup = PyTuple_Pack(1, resume_val);
  Py_DECREF(resume_val);
  if (! args_tup) {
    Py_DECREF(exc);
    return NULL;
  }
  if (PyObject_SetAttrString(exc, "args", args_tup) < 0) {
    Py_DECREF(args_tup);
    Py_DECREF(exc);
    return NULL;
  }
  Py_DECREF(args_tup);
  PyErr_SetObject(KoskariContinuationApply, exc);
  Py_DECREF(exc);
  return NULL;
}


static PyObject *continuation_repr(PyObject *self) {

  return PyUnicode_FromFormat("<Continuation at %p>", self);
}


KosCallableTypeObject KosPyContinuationType = {
  .type = {
    PyVarObject_HEAD_INIT(NULL, 0)

    .tp_name = "koskari.lib.Continuation",
    .tp_basicsize = sizeof(KosPyContinuation),
    .tp_itemsize = 0,
    .tp_dealloc = continuation_dealloc,
    .tp_repr = continuation_repr,
    .tp_call = continuation_call,
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC,
    .tp_doc = "Captured undelimited continuation",
    .tp_traverse = continuation_traverse,
    .tp_clear = continuation_clear,
    .tp_base = KOS_CALLABLE_PYTYPE(KosCallableType),
  },
  .tp_kos_install = kos_install_continuation,
  .tp_kos_tail_install = NULL,
};


/**
  KosPyContinuation wraps an undelimited KosContinuation (from call/cc).

  Python-callable: raises KoskariContinuationDead or KoskariContinuationApply.
  Not yet instantiated (call/cc capture not implemented).
*/
PyObject *KosPyContinuation_New(KosContinuation *captured) {

  KosPyContinuation *cont = PyObject_GC_New(
    KosPyContinuation,
    KOS_CALLABLE_PYTYPE(KosPyContinuationType));

  if (! cont)
    return NULL;

  KosPyContinuation_CAPTURED(cont) = captured;

  PyObject_GC_Track((PyObject *) cont);
  return (PyObject *) cont;
}


/* === Unit lifecycle === */


int koskari_cont_init(PyObject *mod) {

  PyObject *dict = PyModule_GetDict(mod);
  if (! dict)
    return -1;

  if (PyType_Ready(KOS_CALLABLE_PYTYPE(KosPyDelimitedContinuationType)) ||
      PyType_Ready(KOS_CALLABLE_PYTYPE(KosPyContinuationType)))
    return -1;

  if (PyDict_SetItemString(dict, "DelimitedContinuation",
                           (PyObject *) KOS_CALLABLE_PYTYPE(KosPyDelimitedContinuationType)))
    return -1;

  if (PyDict_SetItemString(dict, "Continuation",
                           (PyObject *) KOS_CALLABLE_PYTYPE(KosPyContinuationType)))
    return -1;

  return 0;
}


int koskari_cont_clear(PyObject *mod) {

  (void) mod;
  return 0;
}


// The end.
