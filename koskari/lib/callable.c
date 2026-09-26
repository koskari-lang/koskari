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

   The call suite's shared pieces: the tuple-and-dict argument spread
   and the reusable Python-slot adapters over a KosEvaluatedTypeObject's
   kos_call suite.

   :author: Christopher O'Brien <obriencj@gmail.com>
   :license: GNU General Public License v3
*/


#include "lib.h"


/* === Helpers === */


/**
   Vector-to-tuple packing for KosCallAdapt_VectorAsync, whose worker
   (kc_pycall through Koskari_AsyncCall) takes the tuple + dict shapes.
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


/* === KosCallShape === */


void KosCallShape_Init(KosCallShape *shape, PyObject *callable) {

  Py_INCREF(callable);
  shape->cs_callable = callable;
  shape->cs_kind = KosCallable_Check(callable)
      ? KOS_CALL_SHAPE_FRAME : KOS_CALL_SHAPE_PLAIN;

  /* The frame-return guarantee needs the callable to actually take a
     frame, not merely to be a KosCallable: a yielding KosFunction's
     install completes in hand with a new AsyncGenerator, so its return
     is never the call's value. */
  shape->cs_direct =
      (Py_TYPE(callable) == KOS_EVALUATED_PYTYPE(KosFunctionType)
       && KosFunction_CODE(callable)
       && ! KosCode_YIELDS((KosCode *) KosFunction_CODE(callable)));
}


/* === KosArgSpread === */


int KosArgSpread_Init(
    KosArgSpread *spread,
    PyObject *args_tuple,
    PyObject *kwds_dict,
    PyObject **buf,
    Py_ssize_t bufcap) {

  Py_ssize_t nargs = args_tuple ? PyTuple_GET_SIZE(args_tuple) : 0;
  Py_ssize_t nkw = kwds_dict ? PyDict_Size(kwds_dict) : 0;
  PyObject **argv = buf;

  spread->nargs = nargs;
  spread->kwnames = NULL;
  spread->heap = NULL;

  /* No keywords: the tuple's item array IS the vector. */
  if (! nkw) {
    spread->argv = nargs ? &PyTuple_GET_ITEM(args_tuple, 0) : NULL;
    return 0;
  }

  /* Keywords: extend the vector with the dict's values and name them
     through a kwnames tuple. */
  if (nargs + nkw > bufcap) {
    argv = PyMem_Malloc((size_t) (nargs + nkw) * sizeof(PyObject *));
    if (! argv) {
      PyErr_NoMemory();
      return -1;
    }
    spread->heap = argv;
  }

  spread->kwnames = PyTuple_New(nkw);
  if (! spread->kwnames) {
    PyMem_Free(spread->heap);
    spread->heap = NULL;
    return -1;
  }

  for (Py_ssize_t i = 0; i < nargs; i++)
    argv[i] = PyTuple_GET_ITEM(args_tuple, i);
  {
    PyObject *k, *v;
    Py_ssize_t pos = 0, i = 0;
    while (PyDict_Next(kwds_dict, &pos, &k, &v)) {
      Py_INCREF(k);
      PyTuple_SET_ITEM(spread->kwnames, i, k);
      argv[nargs + i] = v;
      i++;
    }
  }

  spread->argv = argv;
  return 0;
}


int KosArgSpread_InitMerged(
    KosArgSpread *spread,
    PyObject *bound_args,
    PyObject *bound_kwds,
    PyObject *const *args,
    Py_ssize_t nargs,
    PyObject *kwnames,
    PyObject **buf,
    Py_ssize_t bufcap) {

  Py_ssize_t nb = bound_args ? PyTuple_GET_SIZE(bound_args) : 0;
  Py_ssize_t nbk = bound_kwds ? PyDict_Size(bound_kwds) : 0;
  Py_ssize_t nck = kwnames ? PyTuple_GET_SIZE(kwnames) : 0;
  Py_ssize_t npos = nb + nargs;
  Py_ssize_t nhit = 0;
  Py_ssize_t nkw = nck;
  PyObject **argv = buf;
  PyObject *names = NULL;

  spread->nargs = npos;
  spread->kwnames = NULL;
  spread->heap = NULL;

  /* Nothing bound: the call vector IS the vector. */
  if (! nb && ! nbk) {
    spread->argv = args;
    if (nck) {
      Py_INCREF(kwnames);
      spread->kwnames = kwnames;
    }
    return 0;
  }

  /* Bound keywords: count the call keywords that replace one, so the
     merged names tuple is sized once. */
  if (nbk) {
    for (Py_ssize_t i = 0; i < nck; i++) {
      int has = PyDict_Contains(bound_kwds, PyTuple_GET_ITEM(kwnames, i));
      if (has < 0)
        return -1;
      nhit += has;
    }
    nkw = nbk + nck - nhit;
  }

  if (npos + nkw > bufcap) {
    argv = PyMem_Malloc((size_t) (npos + nkw) * sizeof(PyObject *));
    if (! argv) {
      PyErr_NoMemory();
      return -1;
    }
    spread->heap = argv;
  }

  for (Py_ssize_t i = 0; i < nb; i++)
    argv[i] = PyTuple_GET_ITEM(bound_args, i);
  for (Py_ssize_t i = 0; i < nargs; i++)
    argv[nb + i] = args[i];

  /* Only the call side names keywords: its kwnames serve as they are. */
  if (! nbk) {
    for (Py_ssize_t i = 0; i < nck; i++)
      argv[npos + i] = args[nargs + i];
    if (nck) {
      Py_INCREF(kwnames);
      spread->kwnames = kwnames;
    }
    spread->argv = argv;
    return 0;
  }

  /* The bound keywords head the names, in dict order; a call keyword
     that names one replaces its value there, the rest append. */
  names = PyTuple_New(nkw);
  if (! names)
    goto fail;

  {
    PyObject *k, *v;
    Py_ssize_t pos = 0, i = 0;
    Py_ssize_t tail = nbk;
    Py_ssize_t found = 0;

    while (PyDict_Next(bound_kwds, &pos, &k, &v)) {
      if (i == nbk)
        goto changed;
      Py_INCREF(k);
      PyTuple_SET_ITEM(names, i, k);
      argv[npos + i] = v;
      i++;
    }
    if (i != nbk)
      goto changed;

    for (Py_ssize_t j = 0; j < nck; j++) {
      Py_ssize_t at = -1;

      k = PyTuple_GET_ITEM(kwnames, j);
      v = args[nargs + j];
      for (Py_ssize_t h = 0; found < nhit && h < nbk; h++) {
        int eq = PyObject_RichCompareBool(
            PyTuple_GET_ITEM(names, h), k, Py_EQ);
        if (eq < 0)
          goto fail;
        if (eq) {
          at = h;
          found++;
          break;
        }
      }
      if (at >= 0) {
        argv[npos + at] = v;
        continue;
      }
      if (tail == nkw)
        goto changed;
      Py_INCREF(k);
      PyTuple_SET_ITEM(names, tail, k);
      argv[npos + tail] = v;
      tail++;
    }
  }

  spread->kwnames = names;
  spread->argv = argv;
  return 0;

 changed:
  PyErr_SetString(PyExc_RuntimeError,
                  "bound keywords changed during argument merge");
 fail:
  Py_XDECREF(names);
  PyMem_Free(spread->heap);
  spread->heap = NULL;
  return -1;
}


void KosArgSpread_Clear(KosArgSpread *spread) {

  Py_CLEAR(spread->kwnames);
  PyMem_Free(spread->heap);
  spread->heap = NULL;
  spread->argv = NULL;
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
