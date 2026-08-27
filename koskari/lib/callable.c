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
   KosEvaluatedTypeObject's kos_call suite, and the reusable Python-slot
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


KosOpResult KosCallable_CallSlots(KosEvalState *es,
    KosCallSlots *slots, PyObject *callable,
    PyObject *const *args, Py_ssize_t nargs, PyObject *kwnames,
    int tail_call) {

  if (slots->kc_vinstall)
    return callable_promote_normal(
        es, slots->kc_vinstall(es, callable, args, nargs, kwnames, tail_call));

  if (! slots->kc_install) {
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
  KosOpResult r = slots->kc_install(es, callable, tup, kwds, tail_call);
  Py_XDECREF(tup);
  Py_XDECREF(kwds);
  return callable_promote_normal(es, r);
}


KosOpResult KosCallable_CallArgsSlots(KosEvalState *es,
    KosCallSlots *slots, PyObject *callable,
    PyObject *args_tuple, PyObject *kwds_dict,
    int tail_call) {

  if (! slots->kc_install) {
    PyErr_SetString(PyExc_TypeError, "KosCallable has no install slot");
    return EVAL_RAISE();
  }
  return callable_promote_normal(
      es, slots->kc_install(es, callable, args_tuple, kwds_dict, tail_call));
}


KosOpResult KosCallable_Call(KosEvalState *es,
    PyObject *callable,
    PyObject *const *args, Py_ssize_t nargs, PyObject *kwnames,
    int tail_call) {

  KosCallSlots *slots = KosEvaluated_CallSlots(Py_TYPE(callable));

  if (! slots) {
    PyErr_SetString(PyExc_TypeError, "KosCallable has no install slot");
    return EVAL_RAISE();
  }
  return KosCallable_CallSlots(es, slots, callable, args, nargs, kwnames,
                               tail_call);
}


KosOpResult KosCallable_CallArgs(KosEvalState *es,
    PyObject *callable, PyObject *args_tuple, PyObject *kwds_dict,
    int tail_call) {

  KosCallSlots *slots = KosEvaluated_CallSlots(Py_TYPE(callable));

  if (! slots) {
    PyErr_SetString(PyExc_TypeError, "KosCallable has no install slot");
    return EVAL_RAISE();
  }
  return KosCallable_CallArgsSlots(es, slots, callable, args_tuple,
                                   kwds_dict, tail_call);
}


KosOpResult KosCallable_CallNoArgs(KosEvalState *es,
    PyObject *callable, int tail_call) {

  KosCallSlots *slots = KosEvaluated_CallSlots(Py_TYPE(callable));

  if (! slots || ! slots->kc_install) {
    PyErr_SetString(PyExc_TypeError, "KosCallable has no install slot");
    return EVAL_RAISE();
  }
  return callable_promote_normal(
      es, slots->kc_install(es, callable, NULL, NULL, tail_call));
}


/* === Python-slot adapters === */


PyObject *Koskari_AsyncCall(
    PyObject *callable,
    PyObject *args,
    PyObject *kwds) {

  /* The kos call interface: the type's Python-boundary worker. */
  if (KosEvaluated_Check(callable)) {
    KosCallSlots *slots = KosEvaluated_CallSlots(Py_TYPE(callable));
    if (slots && slots->kc_pycall)
      return slots->kc_pycall(callable, args, kwds);
  }

  /* No suite worker: the callable speaks for itself. */
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

  if (KosEvaluated_Check(self)) {
    KosCallSlots *slots = KosEvaluated_CallSlots(Py_TYPE(self));
    if (slots && slots->kc_pyvectorcall)
      return slots->kc_pyvectorcall(self, args, nargsf, kwnames);
  }

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
