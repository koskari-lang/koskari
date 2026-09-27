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
   AWAIT or the coroutine protocol (send/throw/close, __await__
   returning self), which makes it pass collections.abc.Coroutine's
   structural hook and asyncio.iscoroutine. Driving to completion
   consumes the handle (SPENT); reuse raises RuntimeError like a
   finished coroutine.

   author: Christopher O'Brien <obriencj@preoccupied.net>
   license: GPL v.3
*/


#include "lib.h"


/* Method-name constants for delegation; init/cleared in unit lifecycle */
static PyObject *str_send = NULL;
static PyObject *str_throw = NULL;
static PyObject *str_close = NULL;


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
    case KOS_DEFER_ITERATING:
      self->state = KOS_DEFER_ITERATING;
      self->u.iter = donor->u.iter;
      donor->u.iter = NULL;
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
    case KOS_DEFER_ITERATING:
      Py_CLEAR(d->u.iter);
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
    case KOS_DEFER_ITERATING:
      Py_VISIT(d->u.iter);
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
    case KOS_DEFER_ITERATING:
      Py_CLEAR(d->u.iter);
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
      return PyUnicode_FromFormat("<koskari defer delegating inner=%R>", d->u.inner);
    case KOS_DEFER_DONE:
      return PyUnicode_FromString("<koskari defer done>");
    case KOS_DEFER_ITERATING:
      return PyUnicode_FromString("<koskari defer iterating>");
    case KOS_DEFER_SPENT:
      return PyUnicode_FromString("<koskari defer spent>");
    case KOS_DEFER_MOVED_FROM:
      return PyUnicode_FromString("<koskari defer moved_from>");
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


PyObject *KosDefer_TakeFreshCoroutine(PyObject *self) {

  KosDefer *d = (KosDefer *) self;
  PyObject *inner;

  if (d->state != KOS_DEFER_DELEGATING)
    return NULL;
  inner = d->u.inner;
  if (! KosCoroutine_CheckExact(inner)
      || ((KosCoroutine *) inner)->state != KOS_CORO_NEW)
    return NULL;

  /* the handle is consumed: its coroutine runs elsewhere */
  d->u.inner = NULL;
  d->state = KOS_DEFER_SPENT;
  return inner;
}


/* === Coroutine protocol ===

   defer carries the full coroutine protocol (__await__ returning self,
   __next__, send, throw, close), so it passes the structural
   collections.abc.Coroutine subclass hook and asyncio.iscoroutine, and
   asyncio.Task drives it directly. Driving transitions DELEGATING ->
   ITERATING (holding the iterator from inner.__await__()) and forwards
   with yield-from delegation rules; completion, failure, or close
   consumes the handle (SPENT). There is deliberately no tp_iter:
   iter(d) stays a TypeError so iteration constructs cannot silently
   drive a defer.
*/


/**
   Set the reuse error appropriate to a dead handle.
*/
static void defer_set_dead_error(KosDefer *self) {
  if (self->state == KOS_DEFER_MOVED_FROM) {
    PyErr_SetString(PyExc_RuntimeError,
                    "cannot await defer after move (moved_from)");
  } else {
    PyErr_SetString(PyExc_RuntimeError,
                    "cannot reuse already awaited defer");
  }
}


/**
   DELEGATING -> ITERATING: obtain inner.__await__() and hold it. On
   failure the defer stays DELEGATING (payload not consumed) and the
   error propagates.
*/
static int defer_start_iterating(KosDefer *self) {
  PyObject *it = kos_get_awaitable(self->u.inner);
  if (! it)
    return -1;
  Py_CLEAR(self->u.inner);
  self->u.iter = it;
  self->state = KOS_DEFER_ITERATING;
  return 0;
}


/**
   Consume the payload (whatever the state holds) and transition SPENT.
*/
static void defer_spend(KosDefer *self) {
  switch (self->state) {
    case KOS_DEFER_DELEGATING:
      Py_CLEAR(self->u.inner);
      break;
    case KOS_DEFER_DONE:
      Py_CLEAR(self->u.value);
      break;
    case KOS_DEFER_ITERATING:
      Py_CLEAR(self->u.iter);
      break;
    default:
      break;
  }
  self->state = KOS_DEFER_SPENT;
}


/**
   Method lookup distinguishing lookup-AttributeError (NULL, *err=0,
   error cleared) from a raising descriptor (NULL, *err=1, error left
   set), so an AttributeError raised inside a delegated body is never
   mistaken for a missing method.
*/
static PyObject *defer_lookup(PyObject *obj, PyObject *name, int *err) {
  *err = 0;
  PyObject *meth = PyObject_GetAttr(obj, name);
  if (! meth) {
    if (PyErr_ExceptionMatches(PyExc_AttributeError)) {
      PyErr_Clear();
    } else {
      *err = 1;
    }
  }
  return meth;
}


/**
   Report a completion to a caller on the iterator protocol: raise
   StopIteration(value) as a real instance, since a bare
   PyErr_SetObject(PyExc_StopIteration, value) would tuple-splat the
   value during normalization. Steals value. Always returns NULL.
*/
static PyObject *defer_raise_stop(PyObject *value) {
  PyObject *stop = PyObject_CallOneArg(PyExc_StopIteration, value);
  Py_DECREF(value);
  if (! stop)
    return NULL;
  PyErr_SetObject(PyExc_StopIteration, stop);
  Py_DECREF(stop);
  return NULL;
}


/**
   DONE: consume the value and report the completion. Returns the value
   (owned) for the send-slot caller, which reports it as a return.
*/
static PyObject *defer_take_done(KosDefer *self) {
  PyObject *value = self->u.value;
  self->u.value = NULL;
  self->state = KOS_DEFER_SPENT;
  return value;
}


static PyObject *defer_finish_done(KosDefer *self) {
  return defer_raise_stop(defer_take_done(self));
}


/**
   Raise the throw arguments at this point, per generator throw()
   semantics: class-or-instance typ, optional val, tb ignored.
*/
static void defer_raise_thrown(PyObject *typ, PyObject *val, PyObject *tb) {
  PyObject *cls = NULL, *exc = NULL;

  if (KosExc_NormalizeThrow(typ, val, tb, &cls, &exc) < 0)
    return;

  PyErr_SetObject(cls, exc);
  Py_DECREF(cls);
  Py_DECREF(exc);
}


/**
   Forward a send into the ITERATING iterator with yield-from rules:
   use the delegate's send when present; a None send falls back to
   tp_iternext; a non-None send without a send method is a TypeError.
   Returns the yielded value (stay ITERATING), or NULL with the handle
   SPENT and StopIteration (carrying the return value) or the raised
   error set. The caller-mistake TypeError leaves the state unchanged.
*/
static PyObject *defer_forward_send_by_method(
    KosDefer *self, PyObject *value) {
  PyObject *iter = self->u.iter;
  int err = 0;
  PyObject *result;

  PyObject *meth = defer_lookup(iter, str_send, &err);
  if (meth) {
    result = PyObject_CallOneArg(meth, value ? value : Py_None);
    Py_DECREF(meth);
  } else if (err) {
    return NULL;
  } else if (value && value != Py_None) {
    PyErr_SetString(PyExc_TypeError,
                    "can't send non-None value to a plain iterator");
    return NULL;
  } else if (! Py_TYPE(iter)->tp_iternext) {
    PyErr_Format(PyExc_TypeError,
                 "defer cannot drive non-iterator %s",
                 Py_TYPE(iter)->tp_name);
    return NULL;
  } else {
    result = Py_TYPE(iter)->tp_iternext(iter);
    if (! result && ! PyErr_Occurred()) {
      /* plain-iterator exhaustion: completion with None */
      PyErr_SetNone(PyExc_StopIteration);
    }
  }

  if (result)
    return result;

  /* completion (StopIteration) or failure consumes the handle */
  defer_spend(self);
  return NULL;
}


#if PY_MINOR_VERSION >= 10
/* The inner iterator's am_send slot, or NULL when its type has none. */
static inline sendfunc defer_inner_am_send(KosDefer *self) {
  PyTypeObject *tp = Py_TYPE(self->u.iter);
  return tp->tp_as_async ? tp->tp_as_async->am_send : NULL;
}


/**
   Forward a send into the ITERATING iterator on the send protocol:
   through the inner am_send when it has one (a generator, a coroutine,
   another defer), else through the method protocol with its
   StopIteration translated back to a return. Completion or failure
   consumes the handle. *result is owned on RETURN and NEXT, NULL on
   ERROR.
*/
static PySendResult defer_forward_am_send(
    KosDefer *self, PyObject *value, PyObject **result) {
  sendfunc am_send = defer_inner_am_send(self);
  PySendResult r;

  if (am_send) {
    r = am_send(self->u.iter, value ? value : Py_None, result);
    if (r != PYGEN_NEXT)
      defer_spend(self);
    return r;
  }

  *result = defer_forward_send_by_method(self, value);
  if (*result)
    return PYGEN_NEXT;
  if (PyErr_ExceptionMatches(PyExc_StopIteration)) {
    if (PyGen_FetchStopIterationValue(result) < 0)
      return PYGEN_ERROR;
    if (! *result)
      *result = Py_NewRef(Py_None);
    return PYGEN_RETURN;
  }
  return PYGEN_ERROR;
}
#endif


/**
   Forward a send into the ITERATING iterator for a caller on the
   iterator protocol: the inner am_send when it has one, with a return
   reported as StopIteration; else the method protocol.
*/
static PyObject *defer_forward_send(KosDefer *self, PyObject *value) {
#if PY_MINOR_VERSION >= 10
  sendfunc am_send = defer_inner_am_send(self);
  if (am_send) {
    PyObject *result = NULL;
    PySendResult r = am_send(self->u.iter, value ? value : Py_None, &result);
    if (r == PYGEN_NEXT)
      return result;
    defer_spend(self);
    if (r == PYGEN_RETURN)
      return defer_raise_stop(result);
    return NULL;
  }
#endif
  return defer_forward_send_by_method(self, value);
}


/**
   Forward a throw into the ITERATING iterator with yield-from rules.
   val and tb may be NULL. Returns a yielded value (stay ITERATING), or
   NULL with the handle SPENT and an exception set.
*/
static PyObject *defer_forward_throw(
    KosDefer *self,
    PyObject *typ,
    PyObject *val,
    PyObject *tb) {

  PyObject *iter = self->u.iter;
  int err = 0;

  PyObject *meth = defer_lookup(iter, str_throw, &err);
  if (meth) {
    /* CallFunctionObjArgs truncates at the first NULL, so a single-arg
       throw forwards single-arg (avoids 3.12's three-arg deprecation) */
    PyObject *result = PyObject_CallFunctionObjArgs(meth, typ, val, tb, NULL);
    Py_DECREF(meth);
    if (result)
      return result;
    defer_spend(self);
    return NULL;
  }

  if (err)
    return NULL;

  /* No throw on the delegate: close it if the exception is
     GeneratorExit, then raise the exception here (yield-from rules). */
  if (PyErr_GivenExceptionMatches(typ, PyExc_GeneratorExit)) {
    PyObject *close_meth = defer_lookup(iter, str_close, &err);
    if (err)
      return NULL;
    if (close_meth) {
      PyObject *res = PyObject_CallNoArgs(close_meth);
      Py_DECREF(close_meth);
      if (! res) {
        defer_spend(self);
        return NULL;
      }
      Py_DECREF(res);
    }
  }

  defer_spend(self);
  defer_raise_thrown(typ, val, tb);
  return NULL;
}


/**
   Core drive shared by send and __next__.
*/
static PyObject *defer_do_send(KosDefer *self, PyObject *value) {
  switch (self->state) {
    case KOS_DEFER_DONE:
      if (value && value != Py_None) {
        PyErr_SetString(PyExc_TypeError,
                        "can't send non-None value to a"
                        " just-started coroutine");
        return NULL;
      }
      return defer_finish_done(self);

    case KOS_DEFER_DELEGATING:
      if (defer_start_iterating(self) < 0)
        return NULL;
      return defer_forward_send(self, value);

    case KOS_DEFER_ITERATING:
      return defer_forward_send(self, value);

    case KOS_DEFER_SPENT:
    case KOS_DEFER_MOVED_FROM:
      defer_set_dead_error(self);
      return NULL;
  }

  PyErr_SetString(PyExc_RuntimeError, "defer in unexpected state");
  return NULL;
}


static PyObject *defer_send(PyObject *self, PyObject *value) {
  return defer_do_send((KosDefer *) self, value);
}


#if PY_MINOR_VERSION >= 10
/**
   The send slot asyncio's task step and a yield-from drive use: one
   step with the outcome as a code and a value, so a completion never
   builds a StopIteration for the caller to catch and unpack. The
   states are defer_do_send's; only the reporting differs.
*/
static PySendResult defer_am_send(
    PyObject *self, PyObject *value, PyObject **result) {
  KosDefer *d = (KosDefer *) self;

  *result = NULL;
  switch (d->state) {
    case KOS_DEFER_DONE:
      if (value && value != Py_None) {
        PyErr_SetString(PyExc_TypeError,
                        "can't send non-None value to a"
                        " just-started coroutine");
        return PYGEN_ERROR;
      }
      *result = defer_take_done(d);
      return PYGEN_RETURN;

    case KOS_DEFER_DELEGATING:
      if (defer_start_iterating(d) < 0)
        return PYGEN_ERROR;
      return defer_forward_am_send(d, value, result);

    case KOS_DEFER_ITERATING:
      return defer_forward_am_send(d, value, result);

    case KOS_DEFER_SPENT:
    case KOS_DEFER_MOVED_FROM:
      defer_set_dead_error(d);
      return PYGEN_ERROR;
  }

  PyErr_SetString(PyExc_RuntimeError, "defer in unexpected state");
  return PYGEN_ERROR;
}
#endif


static PyObject *defer_iternext(PyObject *self) {
  return defer_do_send((KosDefer *) self, Py_None);
}


static PyObject *defer_throw(PyObject *self, PyObject *args) {
  KosDefer *d = (KosDefer *) self;

  PyObject *typ = NULL, *val = NULL, *tb = NULL;
  if (! PyArg_ParseTuple(args, "O|OO:throw", &typ, &val, &tb))
    return NULL;

  switch (d->state) {
    case KOS_DEFER_DONE:
      defer_spend(d);
      defer_raise_thrown(typ, val, tb);
      return NULL;

    case KOS_DEFER_DELEGATING:
      if (defer_start_iterating(d) < 0)
        return NULL;
      return defer_forward_throw(d, typ, val, tb);

    case KOS_DEFER_ITERATING:
      return defer_forward_throw(d, typ, val, tb);

    case KOS_DEFER_SPENT:
    case KOS_DEFER_MOVED_FROM:
      defer_set_dead_error(d);
      return NULL;
  }

  PyErr_SetString(PyExc_RuntimeError, "defer in unexpected state");
  return NULL;
}


static PyObject *defer_close(PyObject *self, PyObject *ignored) {
  (void) ignored;
  KosDefer *d = (KosDefer *) self;

  PyObject *target = NULL;
  switch (d->state) {
    case KOS_DEFER_SPENT:
    case KOS_DEFER_MOVED_FROM:
      Py_RETURN_NONE;

    case KOS_DEFER_DONE:
      defer_spend(d);
      Py_RETURN_NONE;

    case KOS_DEFER_DELEGATING:
      target = d->u.inner;
      break;

    case KOS_DEFER_ITERATING:
      target = d->u.iter;
      break;
  }

  if (! target) {
    PyErr_SetString(PyExc_RuntimeError, "defer in unexpected state");
    return NULL;
  }

  /* Delegate close when the payload supports it; the delegate raises
     the "ignored GeneratorExit" RuntimeError itself, and errors from
     it propagate. The handle is consumed either way. */
  int err = 0;
  PyObject *meth = defer_lookup(target, str_close, &err);
  if (! meth) {
    defer_spend(d);
    if (err)
      return NULL;
    Py_RETURN_NONE;
  }

  PyObject *res = PyObject_CallNoArgs(meth);
  Py_DECREF(meth);
  defer_spend(d);
  if (! res)
    return NULL;
  Py_DECREF(res);
  Py_RETURN_NONE;
}


/**
   defer.__await__(): return self as the drivable iterator, arming the
   delegation (DELEGATING -> ITERATING) if needed. Reuse raises.
*/
static PyObject *defer_await(PyObject *self) {
  KosDefer *d = (KosDefer *) self;

  switch (d->state) {
    case KOS_DEFER_DELEGATING:
      if (defer_start_iterating(d) < 0)
        return NULL;
      Py_INCREF(self);
      return self;

    case KOS_DEFER_DONE:
    case KOS_DEFER_ITERATING:
      Py_INCREF(self);
      return self;

    case KOS_DEFER_SPENT:
    case KOS_DEFER_MOVED_FROM:
      defer_set_dead_error(d);
      return NULL;
  }

  PyErr_SetString(PyExc_RuntimeError, "defer in unexpected state");
  return NULL;
}


static PyMethodDef defer_methods[] = {
  {"send", (PyCFunction) defer_send, METH_O, NULL},
  {"throw", (PyCFunction) defer_throw, METH_VARARGS, NULL},
  {"close", (PyCFunction) defer_close, METH_NOARGS, NULL},
  {NULL, NULL, 0, NULL},
};


static PyAsyncMethods defer_async_methods = {
  .am_await = defer_await,
#if PY_MINOR_VERSION >= 10
  .am_send = defer_am_send,
#endif
};


/* === type object === */


PyTypeObject KosDeferType = {
  PyVarObject_HEAD_INIT(NULL, 0)
  "koskari.lib.defer",
  sizeof(KosDefer),
  0,
  .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC,
  .tp_new = defer_new,
  .tp_dealloc = defer_dealloc,
  .tp_traverse = defer_traverse,
  .tp_clear = defer_clear,
  .tp_repr = defer_repr,
  .tp_as_async = &defer_async_methods,
  .tp_methods = defer_methods,
  /* tp_iternext without tp_iter: satisfies GET_AWAITABLE's iterator
     check while keeping iter(d) a TypeError */
  .tp_iternext = defer_iternext,
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

  STR_CONST(str_send, "send");
  STR_CONST(str_throw, "throw");
  STR_CONST(str_close, "close");

  if (PyDict_SetItemString(dict, "defer", (PyObject *) &KosDeferType) < 0)
    return -1;

  return 0;
}


int koskari_defer_clear(PyObject *mod) {
  (void) mod;

  Py_CLEAR(str_send);
  Py_CLEAR(str_throw);
  Py_CLEAR(str_close);

  return 0;
}


// The end.
