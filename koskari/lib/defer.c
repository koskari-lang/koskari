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

   Single-shot await-handle (DeferCoroutine). Excluded from implicit
   auto-await; driven by explicit AWAIT.

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
    case KOS_DEFER_PENDING:
      self->state = KOS_DEFER_PENDING;
      self->u.pending.fn = donor->u.pending.fn;
      self->u.pending.args = donor->u.pending.args;
      self->u.pending.kwds = donor->u.pending.kwds;
      donor->u.pending.fn = NULL;
      donor->u.pending.args = NULL;
      donor->u.pending.kwds = NULL;
      break;
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
    case KOS_DEFER_FAILED:
      self->state = KOS_DEFER_FAILED;
      self->u.failed.exc_type = donor->u.failed.exc_type;
      self->u.failed.exc = donor->u.failed.exc;
      donor->u.failed.exc_type = NULL;
      donor->u.failed.exc = NULL;
      break;
    case KOS_DEFER_MOVED_FROM:
      /* nothing to adopt */
      break;
  }
  donor->state = KOS_DEFER_MOVED_FROM;
}


/* === type methods === */


static PyObject *defer_new(PyTypeObject *type, PyObject *args, PyObject *kwds) {
  (void) type;
  if (! args || PyTuple_GET_SIZE(args) < 1) {
    PyErr_SetString(PyExc_TypeError, "defer requires at least one argument (fn)");
    return NULL;
  }
  PyObject *fn = PyTuple_GET_ITEM(args, 0);
  Py_INCREF(fn);
  PyObject *defer_args = NULL;
  if (PyTuple_GET_SIZE(args) > 1) {
    defer_args = PyTuple_GetSlice(args, 1, PyTuple_GET_SIZE(args));
    if (! defer_args) {
      Py_DECREF(fn);
      return NULL;
    }
  } else {
    defer_args = PyTuple_New(0);
    if (! defer_args) {
      Py_DECREF(fn);
      return NULL;
    }
  }
  PyObject *defer_kwds = kwds ? PyDict_Copy(kwds) : NULL;
  if (kwds && ! defer_kwds) {
    Py_DECREF(fn);
    Py_DECREF(defer_args);
    return NULL;
  }
  PyObject *self = KosDefer_New(fn, defer_args, defer_kwds);
  Py_DECREF(fn);
  Py_DECREF(defer_args);
  Py_XDECREF(defer_kwds);
  return self;
}


static void defer_dealloc(PyObject *self) {
  PyObject_GC_UnTrack(self);
  KosDefer *d = (KosDefer *) self;
  switch (d->state) {
    case KOS_DEFER_PENDING:
      Py_CLEAR(d->u.pending.fn);
      Py_CLEAR(d->u.pending.args);
      Py_CLEAR(d->u.pending.kwds);
      break;
    case KOS_DEFER_DELEGATING:
      Py_CLEAR(d->u.inner);
      break;
    case KOS_DEFER_DONE:
      Py_CLEAR(d->u.value);
      break;
    case KOS_DEFER_FAILED:
      Py_CLEAR(d->u.failed.exc_type);
      Py_CLEAR(d->u.failed.exc);
      break;
    case KOS_DEFER_MOVED_FROM:
      break;
  }
  PyObject_GC_Del(self);
}


static int defer_traverse(PyObject *self, visitproc visit, void *arg) {
  KosDefer *d = (KosDefer *) self;
  switch (d->state) {
    case KOS_DEFER_PENDING:
      Py_VISIT(d->u.pending.fn);
      Py_VISIT(d->u.pending.args);
      Py_VISIT(d->u.pending.kwds);
      break;
    case KOS_DEFER_DELEGATING:
      Py_VISIT(d->u.inner);
      break;
    case KOS_DEFER_DONE:
      Py_VISIT(d->u.value);
      break;
    case KOS_DEFER_FAILED:
      Py_VISIT(d->u.failed.exc_type);
      Py_VISIT(d->u.failed.exc);
      break;
    case KOS_DEFER_MOVED_FROM:
      break;
  }
  return 0;
}


static int defer_clear(PyObject *self) {
  KosDefer *d = (KosDefer *) self;
  switch (d->state) {
    case KOS_DEFER_PENDING:
      Py_CLEAR(d->u.pending.fn);
      Py_CLEAR(d->u.pending.args);
      Py_CLEAR(d->u.pending.kwds);
      break;
    case KOS_DEFER_DELEGATING:
      Py_CLEAR(d->u.inner);
      break;
    case KOS_DEFER_DONE:
      Py_CLEAR(d->u.value);
      break;
    case KOS_DEFER_FAILED:
      Py_CLEAR(d->u.failed.exc_type);
      Py_CLEAR(d->u.failed.exc);
      break;
    case KOS_DEFER_MOVED_FROM:
      break;
  }
  return 0;
}


static PyObject *defer_repr(PyObject *self) {
  KosDefer *d = (KosDefer *) self;
  switch (d->state) {
    case KOS_DEFER_PENDING: {
      Py_ssize_t n = d->u.pending.args ? PyTuple_GET_SIZE(d->u.pending.args) : 0;
      Py_ssize_t k = d->u.pending.kwds ? PyDict_Size(d->u.pending.kwds) : 0;
      return PyUnicode_FromFormat("<defer pending fn=%R args=%zd kwds=%zd>",
                                 d->u.pending.fn, n, k);
    }
    case KOS_DEFER_DELEGATING:
      return PyUnicode_FromFormat("<defer delegating inner=%R>", d->u.inner);
    case KOS_DEFER_DONE:
      return PyUnicode_FromString("<defer done>");
    case KOS_DEFER_FAILED:
      return PyUnicode_FromFormat("<defer failed exc=%R>", d->u.failed.exc);
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
  self->state = KOS_DEFER_PENDING;
  self->u.pending.fn = fn;
  Py_INCREF(fn);
  if (args) {
    self->u.pending.args = args;
    Py_INCREF(args);
  } else {
    self->u.pending.args = PyTuple_New(0);
    if (! self->u.pending.args) {
      Py_DECREF(self);
      return NULL;
    }
  }
  self->u.pending.kwds = kwds ? kwds : NULL;
  if (kwds)
    Py_INCREF(kwds);
  PyObject_GC_Track((PyObject *) self);
  return (PyObject *) self;
}


/* === KosDefer_Start === */


PyObject *KosDefer_Start(KosDefer *self, int *suspended, PyObject **yielded) {
  *suspended = 0;
  *yielded = NULL;

  if (self->state == KOS_DEFER_MOVED_FROM) {
    PyErr_SetString(PyExc_RuntimeError, "cannot await defer after move (moved_from)");
    return NULL;
  }

  if (self->state == KOS_DEFER_FAILED) {
    PyErr_SetObject(self->u.failed.exc_type, self->u.failed.exc);
    return NULL;
  }

  if (self->state == KOS_DEFER_DONE) {
    Py_INCREF(self->u.value);
    return self->u.value;
  }

  if (self->state == KOS_DEFER_DELEGATING) {
    *suspended = 1;
    *yielded = self->u.inner;
    Py_INCREF(*yielded);
    return NULL;
  }

  /* PENDING: invoke fn, normalize (loop for defer-of-defer) */
  while (self->state == KOS_DEFER_PENDING) {
    PyObject *result = PyObject_Call(self->u.pending.fn,
                                     self->u.pending.args,
                                     self->u.pending.kwds);
    Py_CLEAR(self->u.pending.fn);
    Py_CLEAR(self->u.pending.args);
    Py_CLEAR(self->u.pending.kwds);

    if (! result) {
      PyObject *exc_typ = NULL;
      PyObject *exc_val = NULL;
      PyObject *exc_tb = NULL;
      PyErr_Fetch(&exc_typ, &exc_val, &exc_tb);
      self->state = KOS_DEFER_FAILED;
      self->u.failed.exc_type = exc_typ;
      Py_INCREF(exc_typ);
      self->u.failed.exc = exc_val ? exc_val : exc_typ;
      Py_INCREF(self->u.failed.exc);
      PyErr_Restore(exc_typ, exc_val, exc_tb);
      return NULL;
    }

    /* Normalize return */
    if (KosDefer_CheckExact(result)) {
      KosDefer *other = (KosDefer *) result;
      defer_adopt(self, other);
      Py_DECREF(result);
      continue;
    }

    if (is_awaitable(result)) {
      self->state = KOS_DEFER_DELEGATING;
      self->u.inner = result;
      *suspended = 1;
      *yielded = result;
      Py_INCREF(*yielded);
      return NULL;
    }

    self->state = KOS_DEFER_DONE;
    self->u.value = result;
    return result;
  }

  /* state changed by adopt to DELEGATING/DONE/FAILED - handle */
  if (self->state == KOS_DEFER_DONE) {
    Py_INCREF(self->u.value);
    return self->u.value;
  }
  if (self->state == KOS_DEFER_FAILED) {
    PyErr_SetObject(self->u.failed.exc_type, self->u.failed.exc);
    return NULL;
  }
  if (self->state == KOS_DEFER_DELEGATING) {
    *suspended = 1;
    *yielded = self->u.inner;
    Py_INCREF(*yielded);
    return NULL;
  }
  PyErr_SetString(PyExc_RuntimeError, "defer in unexpected state");
  return NULL;
}


/* === KosDefer_FeedResult === */


void KosDefer_FeedResult(KosDefer *self, PyObject *result) {
  if (self->state != KOS_DEFER_DELEGATING) {
    PyErr_SetString(PyExc_RuntimeError, "FeedResult only valid when delegating");
    return;
  }
  Py_CLEAR(self->u.inner);
  self->state = KOS_DEFER_DONE;
  self->u.value = result;
  Py_XINCREF(result);
}


void KosDefer_FeedException(KosDefer *self, PyObject *exc_type, PyObject *exc_val) {
  if (self->state != KOS_DEFER_DELEGATING) {
    PyErr_SetString(PyExc_RuntimeError,
                    "FeedException only valid when delegating");
    return;
  }
  Py_CLEAR(self->u.inner);
  self->state = KOS_DEFER_FAILED;
  self->u.failed.exc_type = exc_type;
  Py_XINCREF(exc_type);
  self->u.failed.exc = exc_val ? exc_val : exc_type;
  Py_INCREF(self->u.failed.exc);
}


/* === KosDeferValueIterator ===

   Trivial iterator for the non-awaitable (DONE) case: first __next__
   raises StopIteration(value). Created when defer.__await__() is called
   and fn returns a plain value.
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
  .tp_iternext = defer_valiter_iternext,
};


/**
   defer.__await__(): invoke fn at iterator-creation time. If fn returns
   a non-awaitable value, create a trivial iterator that StopIterations
   with it. If fn returns an awaitable, return its __await__() directly.
*/
static PyObject *defer_await(PyObject *self) {
  KosDefer *defer = (KosDefer *) self;
  int suspended = 0;
  PyObject *yielded = NULL;
  PyObject *result = KosDefer_Start(defer, &suspended, &yielded);

  if (result) {
    KosDeferValueIterator *it = PyObject_GC_New(KosDeferValueIterator,
                                                 &KosDeferValueIteratorType);
    if (! it) {
      Py_DECREF(result);
      return NULL;
    }
    PyObject_Init((PyObject *) it, &KosDeferValueIteratorType);
    it->value = result;
    PyObject_GC_Track((PyObject *) it);
    return (PyObject *) it;
  }

  if (PyErr_Occurred())
    return NULL;

  if (suspended && yielded) {
    PyObject *inner_iter = PyObject_GetAwaitable(yielded);
    Py_DECREF(yielded);
    return inner_iter;
  }

  PyErr_SetString(PyExc_RuntimeError,
                  "defer Start returned without value or suspend");
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
