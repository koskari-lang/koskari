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
  spread->nheld = 0;
  spread->held_tuple = NULL;

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
      Py_INCREF(v);
      argv[nargs + i] = v;
      i++;
    }
    spread->nheld = i;
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
  Py_ssize_t nkw = nck;
  PyObject **argv = buf;
  PyObject *names = NULL;

  spread->nargs = npos;
  spread->kwnames = NULL;
  spread->heap = NULL;
  spread->nheld = 0;
  spread->held_tuple = NULL;

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
     merged names tuple is sized once. Names match by text. */
  if (nbk) {
    Py_ssize_t nhit = 0;

    for (Py_ssize_t j = 0; j < nck; j++) {
      PyObject *k = PyTuple_GET_ITEM(kwnames, j);
      PyObject *bk, *bv;
      Py_ssize_t pos = 0;

      if (! PyUnicode_Check(k))
        goto not_str;
      while (PyDict_Next(bound_kwds, &pos, &bk, &bv)) {
        if (PyUnicode_Check(bk) && PyUnicode_Compare(bk, k) == 0) {
          nhit++;
          break;
        }
      }
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

  if (nb) {
    Py_INCREF(bound_args);
    spread->held_tuple = bound_args;
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
     that names one replaces its value there, the rest append. Every
     keyword value is held, the replaced bound one released, so the
     tail of nkw entries is one span to release. */
  names = PyTuple_New(nkw);
  if (! names)
    goto fail;

  {
    PyObject *k, *v;
    Py_ssize_t pos = 0, i = 0;
    Py_ssize_t tail = nbk;

    /* nheld is published with each value stored, so a later key that
       fails the str check releases every value already held */
    while (PyDict_Next(bound_kwds, &pos, &k, &v)) {
      if (! PyUnicode_Check(k))
        goto not_str;
      Py_INCREF(k);
      PyTuple_SET_ITEM(names, i, k);
      Py_INCREF(v);
      argv[npos + i] = v;
      spread->nheld = ++i;
    }

    for (Py_ssize_t j = 0; j < nck; j++) {
      Py_ssize_t at = -1;

      k = PyTuple_GET_ITEM(kwnames, j);
      v = args[nargs + j];
      for (Py_ssize_t h = 0; h < nbk; h++) {
        if (PyUnicode_Compare(PyTuple_GET_ITEM(names, h), k) == 0) {
          at = h;
          break;
        }
      }
      Py_INCREF(v);
      if (at >= 0) {
        Py_SETREF(argv[npos + at], v);
        continue;
      }
      Py_INCREF(k);
      PyTuple_SET_ITEM(names, tail, k);
      argv[npos + tail] = v;
      tail++;
      spread->nheld = tail;
    }
  }

  spread->kwnames = names;
  spread->argv = argv;
  return 0;

 not_str:
  PyErr_SetString(PyExc_TypeError, "keywords must be strings");
 fail:
  Py_XDECREF(names);
  for (Py_ssize_t i = 0; i < spread->nheld; i++)
    Py_DECREF(argv[npos + i]);
  Py_CLEAR(spread->held_tuple);
  PyMem_Free(spread->heap);
  spread->heap = NULL;
  return -1;
}


void KosArgSpread_Clear(KosArgSpread *spread) {

  for (Py_ssize_t i = 0; i < spread->nheld; i++)
    Py_DECREF(spread->argv[spread->nargs + i]);
  spread->nheld = 0;
  Py_CLEAR(spread->held_tuple);
  Py_CLEAR(spread->kwnames);
  PyMem_Free(spread->heap);
  spread->heap = NULL;
  spread->argv = NULL;
}


int KosKeywords_Update(PyObject *dst, PyObject *src) {

  PyObject *k, *v;
  Py_ssize_t pos = 0;
  PyObject *plain_src = NULL;

  if (! src)
    return 0;

  /* PyDict_Next reads the dict storage raw; a dict subclass that
     relays its reads, such as a module namespace, is copied into a
     plain dict first */
  if (! PyDict_CheckExact(src)) {
    plain_src = PyDict_New();
    if (! plain_src || PyDict_Update(plain_src, src) < 0) {
      Py_XDECREF(plain_src);
      return -1;
    }
    src = plain_src;
  }

  int rc = 0;
  while (PyDict_Next(src, &pos, &k, &v)) {
    if (! PyUnicode_Check(k)) {
      PyErr_SetString(PyExc_TypeError, "keywords must be strings");
      rc = -1;
      break;
    }
    if (PyUnicode_CheckExact(k)) {
      rc = PyDict_SetItem(dst, k, v);
    } else {
      PyObject *plain = PyUnicode_FromObject(k);
      if (! plain) {
        rc = -1;
        break;
      }
      rc = PyDict_SetItem(dst, plain, v);
      Py_DECREF(plain);
    }
    if (rc < 0)
      break;
  }

  Py_XDECREF(plain_src);
  return rc;
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


/* === Override suites === */


/**
   KOS install: a heap subclass overriding __call__ with a KosCallable
   runs it in the calling evaluator, as the instance-call arm of the
   call dispatch does: a callable that binds its receiver gets the
   instance injected as its first positional, a non-descriptor is
   called without it (kos_instance_call_target). A __call__ that is no
   longer a KosCallable is called through tp_call, the value reported
   in hand.
*/
static KosOpResult kos_call_override_kos_vinstall(
    KosContext *ctx,
    PyObject *callable,
    PyObject *const *args,
    Py_ssize_t nargs,
    PyObject *kwnames,
    int tail_call) {

  int binds;
  PyObject *target = kos_instance_call_target(callable, &binds);

  if (! target) {
    PyObject *r = PyObject_Vectorcall(callable, args, (size_t) nargs,
                                      kwnames);
    return r ? EVAL_RETURN(r) : EVAL_RAISE();
  }
  if (binds)
    return KosContext_CallMethod(ctx, target, callable, args, nargs,
                                 kwnames, tail_call, NULL, NULL);
  return KosContext_Call(ctx, target, args, nargs, kwnames, tail_call,
                         NULL, NULL);
}


/* KOS carries the install above. PY is empty: no install, so the
   instance is a plain Python callable to every consumer. */
KosCallSlots kos_call_override_suites[2] = {
  { .kc_vinstall = kos_call_override_kos_vinstall },
  { 0 },
};


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
