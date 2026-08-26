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
   koskari/lib/callable.c

   The call suite: KosCallable dispatch into the install slots of a
   KosEvaluatedTypeObject's tp_kos_call, and the reusable Python-slot
   adapters for demand-surface callables.

   :author: Christopher O'Brien <obriencj@gmail.com>
   :license: GNU General Public License v3
*/


#include "lib.h"


/* === Helpers === */


static PyObject *vcall_build_args_tuple(PyObject *const *args, Py_ssize_t nargs) {
  PyObject *tup = PyTuple_New(nargs);
  if (! tup)
    return NULL;
  for (Py_ssize_t i = 0; i < nargs; i++) {
    Py_INCREF(args[i]);
    PyTuple_SET_ITEM(tup, i, args[i]);
  }
  return tup;
}


static PyObject *vcall_build_kwds_dict(
    PyObject *const *kw_vals,
    PyObject *kwnames) {

  Py_ssize_t nkw = PyTuple_GET_SIZE(kwnames);
  PyObject *kwds = PyDict_New();
  if (! kwds)
    return NULL;
  for (Py_ssize_t i = 0; i < nkw; i++) {
    if (PyDict_SetItem(kwds, PyTuple_GET_ITEM(kwnames, i), kw_vals[i]) < 0) {
      Py_DECREF(kwds);
      return NULL;
    }
  }
  return kwds;
}


/* === KosCallable_* : KosCallable-only (no type check, no frame prep) === */


static KosOpResult callable_promote_normal(KosEvalState *es, KosOpResult r) {
  if (r.kind != KOS_RES_NORMAL)
    return r;
  if (KosEvalState_PLAN(es))
    return EVAL_REPROLOGUE();
  if (KosEvalState_FRAME(es)
      && KosDynFrame_NATIVE_HANDLER(KosEvalState_FRAME(es)))
    return EVAL_REPROLOGUE();
  return EVAL_NORMAL();
}


KosOpResult KosCallable_Call(KosEvalState *es,
    PyObject *callable,
    PyObject *const *args, Py_ssize_t nargs, PyObject *kwnames,
    int tail_call) {

  KosCallSlots *slots = KosEvaluated_CallSlots(Py_TYPE(callable));

  if (slots && slots->ki_vinstall)
    return callable_promote_normal(
        es, slots->ki_vinstall(es, callable, args, nargs, kwnames, tail_call));

  if (! slots || ! slots->ki_install) {
    PyErr_SetString(PyExc_TypeError, "KosCallable has no install slot");
    return EVAL_RAISE();
  }

  PyObject *tup = (nargs > 0) ? vcall_build_args_tuple(args, nargs) : NULL;
  if (nargs > 0 && ! tup)
    return EVAL_RAISE();
  PyObject *kwds = NULL;
  if (kwnames) {
    kwds = vcall_build_kwds_dict(args + nargs, kwnames);
    if (! kwds) {
      Py_XDECREF(tup);
      return EVAL_RAISE();
    }
  }
  KosOpResult r = slots->ki_install(es, callable, tup, kwds, tail_call);
  Py_XDECREF(tup);
  Py_XDECREF(kwds);
  return callable_promote_normal(es, r);
}


KosOpResult KosCallable_CallArgs(KosEvalState *es,
    PyObject *callable, PyObject *args_tuple, PyObject *kwds_dict,
    int tail_call) {

  KosCallSlots *slots = KosEvaluated_CallSlots(Py_TYPE(callable));

  if (! slots || ! slots->ki_install) {
    PyErr_SetString(PyExc_TypeError, "KosCallable has no install slot");
    return EVAL_RAISE();
  }
  return callable_promote_normal(
      es, slots->ki_install(es, callable, args_tuple, kwds_dict, tail_call));
}


KosOpResult KosCallable_CallNoArgs(KosEvalState *es,
    PyObject *callable, int tail_call) {

  KosCallSlots *slots = KosEvaluated_CallSlots(Py_TYPE(callable));

  if (! slots || ! slots->ki_install) {
    PyErr_SetString(PyExc_TypeError, "KosCallable has no install slot");
    return EVAL_RAISE();
  }
  return callable_promote_normal(
      es, slots->ki_install(es, callable, NULL, NULL, tail_call));
}


/* === Python-slot adapters === */


PyObject *Koskari_AsyncCall(
    PyObject *callable,
    PyObject *args,
    PyObject *kwds) {

  /* Demand (or a subtype such as Procedure): the demand surface wins
     over the async one. Dispatch through tp_call so subtype overrides
     (Procedure's result discard) are honored. */
  if (KosDemand_Check(callable)) {
    PyObject *empty = NULL;
    if (! args) {
      empty = PyTuple_New(0);
      if (! empty)
        return NULL;
      args = empty;
    }
    PyObject *result = PyObject_Call(callable, args, kwds);
    Py_XDECREF(empty);
    return result;
  }

  /* BoundMethod: prepend self and follow the wrapped callable. */
  if (KosBoundMethod_CheckExact(callable)) {
    PyObject *bound_self = KosBoundMethod_SELF(callable);
    PyObject *bound_args;
    Py_ssize_t n = args ? PyTuple_GET_SIZE(args) : 0;

    bound_args = PyTuple_New(1 + n);
    if (! bound_args)
      return NULL;
    Py_INCREF(bound_self);
    PyTuple_SET_ITEM(bound_args, 0, bound_self);
    for (Py_ssize_t i = 0; i < n; i++) {
      PyObject *item = PyTuple_GET_ITEM(args, i);
      Py_INCREF(item);
      PyTuple_SET_ITEM(bound_args, 1 + i, item);
    }

    PyObject *result = Koskari_AsyncCall(
        KosBoundMethod_FUNC(callable), bound_args, kwds);
    Py_DECREF(bound_args);
    return result;
  }

  if (KosFunction_CheckExact(callable))
    return KosFunction_Call(callable, args, kwds);

  if (KosExpression_CheckExact(callable)) {
    if (args && PyTuple_GET_SIZE(args) != 0) {
      PyErr_SetString(PyExc_TypeError, "Expression() takes no arguments");
      return NULL;
    }
    if (kwds && PyDict_Size(kwds) != 0) {
      PyErr_SetString(PyExc_TypeError, "Expression takes no named arguments");
      return NULL;
    }
    return KosExpression_Eval(callable);
  }

  /* KosPartial (or a subtype -- its tp_call is this adapter, so the
     fallback would recurse): merge bound args and follow the inner
     callable. */
  if (KosPartial_Check(callable))
    return KosPartial_Call(callable, args, kwds);

  /* Everything else (continuations, values, plain Python) speaks for
     itself through its own tp_call. */
  {
    PyObject *empty = NULL;
    if (! args) {
      empty = PyTuple_New(0);
      if (! empty)
        return NULL;
      args = empty;
    }
    PyObject *result = PyObject_Call(callable, args, kwds);
    Py_XDECREF(empty);
    return result;
  }
}


PyObject *KosCallAdapt_CallAsync(
    PyObject *self,
    PyObject *args,
    PyObject *kwds) {

  return Koskari_AsyncCall(self, args, kwds);
}


PyObject *KosCallAdapt_VectorAsync(
    PyObject *self,
    PyObject *const *args,
    size_t nargsf,
    PyObject *kwnames) {

  if (KosFunction_CheckExact(self))
    return KosFunction_VectorCall(self, args, nargsf, kwnames);

  Py_ssize_t nargs = PyVectorcall_NARGS(nargsf);
  PyObject *tup = vcall_build_args_tuple(args, nargs);
  if (! tup)
    return NULL;

  PyObject *kwds = NULL;
  if (kwnames && PyTuple_GET_SIZE(kwnames)) {
    kwds = vcall_build_kwds_dict(args + nargs, kwnames);
    if (! kwds) {
      Py_DECREF(tup);
      return NULL;
    }
  }

  PyObject *result = Koskari_AsyncCall(self, tup, kwds);
  Py_DECREF(tup);
  Py_XDECREF(kwds);
  return result;
}


PyObject *KosCallAdapt_CallDemand(
    PyObject *self,
    PyObject *args,
    PyObject *kwds) {

  return Koskari_DemandSync(self, args, kwds);
}


PyObject *KosCallAdapt_VectorDemand(
    PyObject *self,
    PyObject *const *args,
    size_t nargsf,
    PyObject *kwnames) {

  Py_ssize_t nargs = PyVectorcall_NARGS(nargsf);
  PyObject *tup = vcall_build_args_tuple(args, nargs);
  if (! tup)
    return NULL;

  PyObject *kwds = NULL;
  if (kwnames && PyTuple_GET_SIZE(kwnames)) {
    kwds = vcall_build_kwds_dict(args + nargs, kwnames);
    if (! kwds) {
      Py_DECREF(tup);
      return NULL;
    }
  }

  PyObject *result = Koskari_DemandSync(self, tup, kwds);
  Py_DECREF(tup);
  Py_XDECREF(kwds);
  return result;
}


/* === Unit lifecycle === */


int koskari_callable_init(PyObject *mod) {
  (void) mod;
  return 0;
}


int koskari_callable_clear(PyObject *mod) {
  (void) mod;
  return 0;
}


// The end.
