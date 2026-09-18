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


/**
   Vector-to-tuple packing for the python-boundary adapters, whose
   workers (kc_pycall, Koskari_AsyncCall, Koskari_DemandSync) take the
   tuple + dict shapes.
*/
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
  if (KosEvalState_GOAL(es))
    return EVAL_REPROLOGUE();
  /* An install that left a resume on the frame rather than bytecode
     must go through the prologue, which is where it is picked up.
     NORMAL here would resume instruction dispatch on a frame that has
     no code. */
  if (KosEvalState_FRAME(es)
      && KosDynFrame_RESUME_KIND(KosEvalState_FRAME(es)))
    return EVAL_REPROLOGUE();
  return EVAL_NORMAL();
}


KosOpResult KosCallable_CallSlots(KosEvalState *es,
    KosCallSlots *slots, PyObject *callable,
    PyObject *const *args, Py_ssize_t nargs, PyObject *kwnames,
    int tail_call) {

  if (! slots->kc_vinstall) {
    PyErr_SetString(PyExc_TypeError, "KosCallable has no install slot");
    return EVAL_RAISE();
  }
  return callable_promote_normal(
      es, slots->kc_vinstall(es, callable, args, nargs, kwnames, tail_call));
}


#define KOS_CALLARGS_SPREAD_FAST 16

KosOpResult KosCallable_CallArgsSlots(KosEvalState *es,
    KosCallSlots *slots, PyObject *callable,
    PyObject *args_tuple, PyObject *kwds_dict,
    int tail_call) {

  Py_ssize_t nargs = args_tuple ? PyTuple_GET_SIZE(args_tuple) : 0;
  Py_ssize_t nkw = kwds_dict ? PyDict_Size(kwds_dict) : 0;

  if (! slots->kc_vinstall) {
    PyErr_SetString(PyExc_TypeError, "KosCallable has no install slot");
    return EVAL_RAISE();
  }

  /* No keywords: the tuple's item array IS the vector. */
  if (! nkw) {
    PyObject *const *argv =
        nargs ? &PyTuple_GET_ITEM(args_tuple, 0) : NULL;
    return callable_promote_normal(
        es, slots->kc_vinstall(es, callable, argv, nargs, NULL, tail_call));
  }

  /* Keywords: extend the vector with the dict's values and name them
     through a kwnames tuple. Every entry is borrowed -- the caller's
     tuple and dict stay alive across the install. */
  {
    PyObject *small[KOS_CALLARGS_SPREAD_FAST];
    PyObject **argv = small;
    PyObject *kwnames = NULL;
    KosOpResult r;

    if (nargs + nkw > KOS_CALLARGS_SPREAD_FAST) {
      argv = PyMem_Malloc((size_t) (nargs + nkw) * sizeof(PyObject *));
      if (! argv) {
        PyErr_NoMemory();
        return EVAL_RAISE();
      }
    }

    for (Py_ssize_t i = 0; i < nargs; i++)
      argv[i] = PyTuple_GET_ITEM(args_tuple, i);

    kwnames = PyTuple_New(nkw);
    if (! kwnames) {
      if (argv != small)
        PyMem_Free(argv);
      return EVAL_RAISE();
    }
    {
      PyObject *k, *v;
      Py_ssize_t pos = 0, i = 0;
      while (PyDict_Next(kwds_dict, &pos, &k, &v)) {
        Py_INCREF(k);
        PyTuple_SET_ITEM(kwnames, i, k);
        argv[nargs + i] = v;
        i++;
      }
    }

    r = slots->kc_vinstall(es, callable, argv, nargs, kwnames, tail_call);
    Py_DECREF(kwnames);
    if (argv != small)
      PyMem_Free(argv);
    return callable_promote_normal(es, r);
  }
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

  if (! slots || ! slots->kc_vinstall) {
    PyErr_SetString(PyExc_TypeError, "KosCallable has no install slot");
    return EVAL_RAISE();
  }
  return callable_promote_normal(
      es, slots->kc_vinstall(es, callable, NULL, 0, NULL, tail_call));
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
