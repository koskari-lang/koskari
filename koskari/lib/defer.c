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

   :author: Christopher O'Brien <obriencj@preoccupied.net>
   :license: GNU General Public License v3
*/


#include "lib.h"


/* Method-name constants for delegation; init/cleared in unit lifecycle */


/* === helpers === */


/* What await accepts: an am_await slot, or a types.coroutine marked
   generator. */
static int is_awaitable(PyObject *obj) {
  PyTypeObject *type = Py_TYPE(obj);
  if (type->tp_as_async && type->tp_as_async->am_await)
    return 1;
  return PyGen_CheckExact(obj) && kos_gen_is_iterable_coroutine(obj);
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
   Normalize the deferred call's result into self: a defer is adopted,
   an awaitable is delegated to, anything else is a done value. result
   is consumed. Returns 0; -1 with PyErr set when result is NULL.
*/
static int defer_normalize_result(KosDefer *self, PyObject *result) {

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
  return defer_normalize_result(self, result);
}


/**
   A fresh defer over the call's result, with the state a construction
   failure can be released from. Returns a new untracked reference; the
   caller tracks it once the result is in.
*/
static KosDefer *defer_alloc(void) {

  KosDefer *self = PyObject_GC_New(KosDefer, KOS_EVALUATED_PYTYPE(KosDeferType));
  if (! self)
    return NULL;

  /* Safe defaults: normalize overwrites on success, and dealloc after a
     normalize failure (or a payload-less adopt) must not switch on a
     garbage state. */
  self->state = KOS_DEFER_SPENT;
  self->u.inner = NULL;
  return self;
}


/**
   The vectorcall entry for the type: defer(fn, *args, **kwds) with the
   argument vector handed on to fn as it stands, no tuple built for the
   arguments and none for an empty call.
*/
static PyObject *defer_vectorcall(
    PyObject *type,
    PyObject *const *args,
    size_t nargsf,
    PyObject *kwnames) {

  Py_ssize_t nargs = PyVectorcall_NARGS(nargsf);
  KosDefer *self;
  PyObject *fn;
  PyObject *result;

  (void) type;

  if (nargs < 1) {
    PyErr_SetString(PyExc_TypeError, "defer requires at least one argument (fn)");
    return NULL;
  }

  /* Idempotent: defer(d) where d is already a defer returns d directly,
     mirroring demand(demand(x)) -> demand(x). */
  if (nargs == 1 && (! kwnames || PyTuple_GET_SIZE(kwnames) == 0)
      && KosDefer_CheckExact(args[0])) {
    Py_INCREF(args[0]);
    return args[0];
  }

  self = defer_alloc();
  if (! self)
    return NULL;

  fn = args[0];
  Py_INCREF(fn);
  result = PyObject_Vectorcall(fn, args + 1, (size_t) (nargs - 1), kwnames);
  Py_DECREF(fn);

  if (defer_normalize_result(self, result) < 0) {
    Py_DECREF(self);
    return NULL;
  }

  PyObject_GC_Track((PyObject *) self);
  return (PyObject *) self;
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


/**
   Take the payload the current state holds, leaving the slot NULL: the
   inner, the value or the iterator, or NULL for a state holding none.
   The state itself is the caller's to write before it releases what
   it took, since the release can run code that reaches this defer.
*/
static PyObject *defer_take_payload(KosDefer *self) {
  PyObject *payload;

  switch (self->state) {
    case KOS_DEFER_DELEGATING:
      payload = self->u.inner;
      self->u.inner = NULL;
      return payload;
    case KOS_DEFER_DONE:
      payload = self->u.value;
      self->u.value = NULL;
      return payload;
    case KOS_DEFER_ITERATING:
      payload = self->u.iter;
      self->u.iter = NULL;
      return payload;
    default:
      return NULL;
  }
}


static int defer_clear(PyObject *self) {
  KosDefer *d = (KosDefer *) self;
  PyObject *payload = defer_take_payload(d);

  d->state = KOS_DEFER_SPENT;
  Py_XDECREF(payload);
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
  KosDefer *self = defer_alloc();
  if (! self)
    return NULL;

  if (defer_normalize_from_call(self, fn, args, kwds) < 0) {
    Py_DECREF(self);
    return NULL;
  }

  PyObject_GC_Track((PyObject *) self);
  return (PyObject *) self;
}


/**
   The await suite's kind: a delegating defer stands for what it
   delegates to. An inner value with an await suite of its own is
   handed to that suite's kind on this await frame and the handle is
   spent, as a drive to completion would spend it; anything else is
   awaited through the defer's own protocol. An implicit site takes a
   defer as a plain handle (ka_implicit_plain).
*/
static KOS_NOINLINE KOS_PRESERVE_NONE KosOpResult defer_await_resume(
    KosContext *ctx,
    void *state,
    PyObject *value) {

  KosDefer *d = (KosDefer *) value;
  KosAwaitSlots *slots;
  PyObject *inner;

  if (d->state != KOS_DEFER_DELEGATING
      || ! KosAwait_CheckSlots(d->u.inner, &slots))
    TAILCALL(kos_await_python, ctx, state, value);

  inner = d->u.inner;
  d->u.inner = NULL;
  d->state = KOS_DEFER_SPENT;
  Py_DECREF(value);
  TAILCALL(slots->ka_install->fn, ctx, state, inner);
}

KOS_RESUME_KIND_AS_IS(defer_await_kind, "await defer",
                      defer_await_resume, NULL, NULL);


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
  PyObject *inner = self->u.inner;
  PyObject *it = kos_get_awaitable(inner);

  if (! it)
    return -1;
  /* The new state is written before the old payload goes: releasing
     the inner can run a __del__ that reaches this defer, which must
     meet it iterating, never delegating with nothing to delegate to. */
  self->u.iter = it;
  self->state = KOS_DEFER_ITERATING;
  Py_DECREF(inner);
  return 0;
}


/**
   Consume the payload (whatever the state holds) and transition SPENT.
*/
static void defer_spend(KosDefer *self) {
  PyObject *payload = defer_take_payload(self);

  /* spent before the payload is released, so a __del__ it runs that
     reaches this defer meets the reuse refusal */
  self->state = KOS_DEFER_SPENT;
  Py_XDECREF(payload);
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
  return kos_raise_stop_iteration(defer_take_done(self));
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
   One step of the ITERATING iterator through the send protocol, as
   kos_iter_send steps every iterator: the am_send slot when the type
   has one, next for a None send on an iterator, else its send method.
   A yield stays ITERATING; a completion or failure spends the handle.
   The inner is held across the step, since the step can spend this
   defer from inside the inner body (a close on it), dropping u.iter's
   reference while the step is in flight. *result is owned on RETURN
   and NEXT, NULL on ERROR.
*/
static PySendResult defer_forward_step(
    KosDefer *self, PyObject *value, PyObject **result) {
  PyObject *iter = Py_NewRef(self->u.iter);
  PySendResult r = kos_iter_send(iter, value ? value : Py_None, result);
  Py_DECREF(iter);
  if (r != PYGEN_NEXT)
    defer_spend(self);
  return r;
}


#if PY_MINOR_VERSION >= 10
/**
   Forward a send into the ITERATING iterator on the send protocol.
*/
static PySendResult defer_forward_am_send(
    KosDefer *self, PyObject *value, PyObject **result) {
  return defer_forward_step(self, value, result);
}
#endif


/**
   Forward a send into the ITERATING iterator for a caller on the
   iterator protocol: a return is reported as StopIteration.
*/
static PyObject *defer_forward_send(KosDefer *self, PyObject *value) {
  PyObject *result = NULL;
  PySendResult r = defer_forward_step(self, value, &result);

  if (r == PYGEN_NEXT)
    return result;
  if (r == PYGEN_RETURN)
    return kos_raise_stop_iteration(result);
  return NULL;
}


/**
   Forward a throw into the ITERATING iterator by the yield-from rule
   kos_iter_throw applies. val and tb may be NULL. Returns a yielded
   value (stay ITERATING), or NULL with the handle SPENT and
   StopIteration (carrying the return value) or the raised error set.
*/
static PyObject *defer_forward_throw(
    KosDefer *self,
    PyObject *typ,
    PyObject *val,
    PyObject *tb) {

  PyObject *cls = NULL, *exc = NULL, *result = NULL;
  PyObject *iter;
  PySendResult r;

  if (KosExc_NormalizeThrow(typ, val, tb, &cls, &exc) < 0)
    return NULL;

  iter = Py_NewRef(self->u.iter);  /* see defer_forward_step */
  r = kos_iter_throw(iter, exc, &result);
  Py_DECREF(iter);
  Py_DECREF(cls);
  Py_DECREF(exc);

  if (r == PYGEN_NEXT)
    return result;
  defer_spend(self);
  if (r == PYGEN_RETURN)
    return kos_raise_stop_iteration(result);
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
  {
    PyObject *held = Py_NewRef(target);
    int r = kos_iter_close_or_raise(held);
    Py_DECREF(held);
    defer_spend(d);
    if (r < 0)
      return NULL;
  }
  Py_RETURN_NONE;
}


/**
   defer.__await__(): return self as the drivable iterator, starting the
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


KosEvaluatedTypeObject KosDeferType = {
  .type = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "koskari.lib.defer",
    .tp_basicsize = sizeof(KosDefer),
    .tp_itemsize = 0,
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC,
    .tp_new = defer_new,
    .tp_vectorcall = defer_vectorcall,
    .tp_dealloc = defer_dealloc,
    .tp_traverse = defer_traverse,
    .tp_clear = defer_clear,
    .tp_repr = defer_repr,
    .tp_as_async = &defer_async_methods,
    .tp_methods = defer_methods,
    /* tp_iternext without tp_iter: satisfies GET_AWAITABLE's iterator
       check while keeping iter(d) a TypeError */
    .tp_iternext = defer_iternext,
    .tp_base = KOS_EVALUATED_PYTYPE(KosEvaluatedType),
  },
  .kos_await = {
    .ka_install = &defer_await_kind,
    .ka_implicit_plain = 1,
  },
};


/* === unit lifecycle === */


int koskari_defer_init(PyObject *mod) {
  PyObject *dict;

  if (! mod)
    return -1;
  dict = PyModule_GetDict(mod);
  if (! dict)
    return -1;

  if (PyType_Ready(KOS_EVALUATED_PYTYPE(KosDeferType)) < 0)
    return -1;

  if (PyDict_SetItemString(dict, "defer",
                           (PyObject *) KOS_EVALUATED_PYTYPE(KosDeferType)) < 0)
    return -1;

  return 0;
}


int koskari_defer_clear(PyObject *mod) {
  (void) mod;


  return 0;
}


// The end.
