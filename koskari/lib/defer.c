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
   koskari.lib defer

   Single-shot await-handle (DeferCoroutine). Invokes its target at
   construction; excluded from implicit auto-await; driven by explicit
   AWAIT. The first await consumes the handle (SPENT); reuse raises
   RuntimeError like a finished coroutine.

   author: Christopher O'Brien <obriencj@preoccupied.net>
   license: GPL v.3
*/


#include "lib.h"


/* === helpers === */


static int is_awaitable(PyObject *obj) {
  PyTypeObject *type = Py_TYPE(obj);
  return (type && type->tp_as_async && type->tp_as_async->am_await) ? 1 : 0;
}


/**
   Adopt donor's state into self (O(1)). Donor becomes MOVED_FROM.
   Caller must hold refs; adoption transfers ownership of donor's payload.
*/
static void defer_adopt(KosDefer *self, KosDefer *donor) {
  switch (donor->state) {
    case KOS_DEFER_DELEGATING:
      self->state = KOS_DEFER_DELEGATING;
      self->u.inner = donor->u.inner;
      donor->u.inner = NULL;
      break;
    case KOS_DEFER_DONE:
      self->state = KOS_DEFER_DONE;
      self->u.value = donor->u.value;
      donor->u.value = NULL;
      break;
    case KOS_DEFER_SPENT:
      /* nothing to adopt; adoptee is as dead as the donor */
      self->state = KOS_DEFER_SPENT;
      break;
    case KOS_DEFER_MOVED_FROM:
      /* nothing to adopt */
      self->state = KOS_DEFER_MOVED_FROM;
      break;
  }
  donor->state = KOS_DEFER_MOVED_FROM;
}


/**
   Normalize result into self (defer-of-defer adopt loop). On failure set
   PyErr and return -1. fn/args/kwds are borrowed (not stored on self).
*/
static int defer_normalize_from_call(
    KosDefer *self,
    PyObject *fn,
    PyObject *args,
    PyObject *kwds) {

  PyObject *owned_args = NULL;
  PyObject *call_args = args;

  if (! call_args) {
    owned_args = PyTuple_New(0);
    if (! owned_args)
      return -1;
    call_args = owned_args;
  }

  PyObject *result = PyObject_Call(fn, call_args, kwds);
  Py_XDECREF(owned_args);

  if (! result)
    return -1;

  if (KosDefer_CheckExact(result)) {
    KosDefer *other = (KosDefer *) result;
    defer_adopt(self, other);
    Py_DECREF(result);
    return 0;
  }

  if (is_awaitable(result)) {
    self->state = KOS_DEFER_DELEGATING;
    self->u.inner = result;
    return 0;
  }

  self->state = KOS_DEFER_DONE;
  self->u.value = result;
  return 0;
}


/* === type methods === */


static PyObject *defer_new(PyTypeObject *type, PyObject *args, PyObject *kwds) {
  (void) type;

  if (! args || PyTuple_GET_SIZE(args) < 1) {
    PyErr_SetString(PyExc_TypeError, "defer requires at least one argument (fn)");
    return NULL;
  }

  /* Idempotent: defer(d) where d is already a defer returns d directly,
     mirroring demand(demand(x)) -> demand(x). */
  if (PyTuple_GET_SIZE(args) == 1 && (! kwds || ! PyDict_Size(kwds))) {
    PyObject *only = PyTuple_GET_ITEM(args, 0);
    if (KosDefer_CheckExact(only)) {
      Py_INCREF(only);
      return only;
    }
  }

  PyObject *fn = PyTuple_GET_ITEM(args, 0);
  Py_INCREF(fn);

  PyObject *defer_args = NULL;
  if (PyTuple_GET_SIZE(args) > 1) {
    defer_args = PyTuple_GetSlice(args, 1, PyTuple_GET_SIZE(args));
  } else {
    defer_args = PyTuple_New(0);
  }
  if (! defer_args) {
    Py_DECREF(fn);
    return NULL;
  }

  PyObject *self = KosDefer_New(fn, defer_args, kwds);
  Py_DECREF(fn);
  Py_DECREF(defer_args);
  return self;
}


static void defer_dealloc(PyObject *self) {
  PyObject_GC_UnTrack(self);
  KosDefer *d = (KosDefer *) self;
  switch (d->state) {
    case KOS_DEFER_DELEGATING:
      Py_CLEAR(d->u.inner);
      break;
    case KOS_DEFER_DONE:
      Py_CLEAR(d->u.value);
      break;
    case KOS_DEFER_SPENT:
    case KOS_DEFER_MOVED_FROM:
      break;
  }
  PyObject_GC_Del(self);
}


static int defer_traverse(PyObject *self, visitproc visit, void *arg) {
  KosDefer *d = (KosDefer *) self;
  switch (d->state) {
    case KOS_DEFER_DELEGATING:
      Py_VISIT(d->u.inner);
      break;
    case KOS_DEFER_DONE:
      Py_VISIT(d->u.value);
      break;
    case KOS_DEFER_SPENT:
    case KOS_DEFER_MOVED_FROM:
      break;
  }
  return 0;
}


static int defer_clear(PyObject *self) {
  KosDefer *d = (KosDefer *) self;
  switch (d->state) {
    case KOS_DEFER_DELEGATING:
      Py_CLEAR(d->u.inner);
      break;
    case KOS_DEFER_DONE:
      Py_CLEAR(d->u.value);
      break;
    case KOS_DEFER_SPENT:
    case KOS_DEFER_MOVED_FROM:
      break;
  }
  return 0;
}


static PyObject *defer_repr(PyObject *self) {
  KosDefer *d = (KosDefer *) self;
  switch (d->state) {
    case KOS_DEFER_DELEGATING:
      return PyUnicode_FromFormat("<defer delegating inner=%R>", d->u.inner);
    case KOS_DEFER_DONE:
      return PyUnicode_FromString("<defer done>");
    case KOS_DEFER_SPENT:
      return PyUnicode_FromString("<defer spent>");
    case KOS_DEFER_MOVED_FROM:
      return PyUnicode_FromString("<defer moved_from>");
    default:
      return PyUnicode_FromString("<defer ?>");
  }
}


/* === KosDefer_New === */


PyObject *KosDefer_New(PyObject *fn, PyObject *args, PyObject *kwds) {
  KosDefer *self = PyObject_GC_New(KosDefer, &KosDeferType);
  if (! self)
    return NULL;
  PyObject_Init((PyObject *) self, &KosDeferType);

  /* Safe defaults: normalize overwrites on success, and dealloc after a
     normalize failure (or a payload-less adopt) must not switch on a
     garbage state. */
  self->state = KOS_DEFER_SPENT;
  self->u.inner = NULL;

  if (defer_normalize_from_call(self, fn, args, kwds) < 0) {
    Py_DECREF(self);
    return NULL;
  }

  PyObject_GC_Track((PyObject *) self);
  return (PyObject *) self;
}


/* === KosDeferValueIterator ===

   Trivial iterator for the non-awaitable (DONE) case: first __next__
   raises StopIteration(value). Created when defer.__await__() is called
   on a defer already in DONE state.
*/


typedef struct KosDeferValueIterator {
  PyObject_HEAD
  PyObject *value;
} KosDeferValueIterator;


static PyTypeObject KosDeferValueIteratorType;


static void defer_valiter_dealloc(PyObject *self) {
  PyObject_GC_UnTrack(self);
  KosDeferValueIterator *it = (KosDeferValueIterator *) self;
  Py_CLEAR(it->value);
  PyObject_GC_Del(self);
}


static int defer_valiter_traverse(PyObject *self, visitproc visit, void *arg) {
  KosDeferValueIterator *it = (KosDeferValueIterator *) self;
  Py_VISIT(it->value);
  return 0;
}


static int defer_valiter_clear(PyObject *self) {
  KosDeferValueIterator *it = (KosDeferValueIterator *) self;
  Py_CLEAR(it->value);
  return 0;
}


static PyObject *defer_valiter_iternext(PyObject *self) {
  KosDeferValueIterator *it = (KosDeferValueIterator *) self;
  PyObject *value = it->value;
  it->value = NULL;
  if (! value)
    Py_RETURN_NONE;
  PyErr_SetObject(PyExc_StopIteration, value);
  Py_DECREF(value);
  return NULL;
}


static PyObject *defer_valiter_send(PyObject *self, const PyObject *arg) {
  if (arg != Py_None) {
    PyErr_SetString(PyExc_TypeError, "can't send non-None value to a just-started coroutine");
    return NULL;
  }
  return defer_valiter_iternext(self);
}


static PyMethodDef defer_valiter_methods[] = {
  {"send", (PyCFunction) defer_valiter_send, METH_O, NULL},
  {NULL, NULL, 0, NULL},
};


static PyTypeObject KosDeferValueIteratorType = {
  PyVarObject_HEAD_INIT(NULL, 0)
  "defer_value_iterator",
  sizeof(KosDeferValueIterator),
  0,
  .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC,
  .tp_dealloc = defer_valiter_dealloc,
  .tp_traverse = defer_valiter_traverse,
  .tp_clear = defer_valiter_clear,
  .tp_iter = PyObject_SelfIter,
  .tp_methods = defer_valiter_methods,
  .tp_iternext = defer_valiter_iternext,
};


/**
   defer.__await__(): resolve the defer at await time, consuming it.
   DONE hands the value to a trivial iterator; DELEGATING hands out the
   inner's __await__() and drops the inner. Either way the defer
   transitions to SPENT; a later await raises RuntimeError like a
   finished coroutine.
*/
static PyObject *defer_await(PyObject *self) {
  KosDefer *defer = (KosDefer *) self;

  switch (defer->state) {
    case KOS_DEFER_DONE: {
      KosDeferValueIterator *it = PyObject_GC_New(KosDeferValueIterator,
                                                  &KosDeferValueIteratorType);
      if (! it)
        return NULL;
      PyObject_Init((PyObject *) it, &KosDeferValueIteratorType);

      /* transfer the value; the handle is consumed */
      it->value = defer->u.value;
      defer->u.value = NULL;
      defer->state = KOS_DEFER_SPENT;

      PyObject_GC_Track((PyObject *) it);
      return (PyObject *) it;
    }

    case KOS_DEFER_DELEGATING: {
      /* obtain the iterator before consuming: if inner's __await__
         raises, the defer stays DELEGATING and the error propagates */
      PyObject *inner_iter = PyObject_GetAwaitable(defer->u.inner);
      if (! inner_iter)
        return NULL;

      Py_CLEAR(defer->u.inner);
      defer->state = KOS_DEFER_SPENT;
      return inner_iter;
    }

    case KOS_DEFER_SPENT:
      PyErr_SetString(PyExc_RuntimeError,
                      "cannot reuse already awaited defer");
      return NULL;

    case KOS_DEFER_MOVED_FROM:
      PyErr_SetString(PyExc_RuntimeError,
                      "cannot await defer after move (moved_from)");
      return NULL;
  }

  PyErr_SetString(PyExc_RuntimeError, "defer in unexpected state");
  return NULL;
}


static PyAsyncMethods defer_async_methods = {
  .am_await = defer_await,
};


/* === type object === */


PyTypeObject KosDeferType = {
  PyVarObject_HEAD_INIT(NULL, 0)
  "defer",
  sizeof(KosDefer),
  0,
  .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC,
  .tp_new = defer_new,
  .tp_dealloc = defer_dealloc,
  .tp_traverse = defer_traverse,
  .tp_clear = defer_clear,
  .tp_repr = defer_repr,
  .tp_as_async = &defer_async_methods,
};


/* === unit lifecycle === */


int koskari_defer_init(PyObject *mod) {
  PyObject *dict;

  if (! mod)
    return -1;
  dict = PyModule_GetDict(mod);
  if (! dict)
    return -1;

  if (PyType_Ready(&KosDeferType) < 0)
    return -1;

  if (PyType_Ready(&KosDeferValueIteratorType) < 0)
    return -1;

  if (PyDict_SetItemString(dict, "defer", (PyObject *) &KosDeferType) < 0)
    return -1;

  return 0;
}


int koskari_defer_clear(PyObject *mod) {
  (void) mod;
  return 0;
}


// The end.
