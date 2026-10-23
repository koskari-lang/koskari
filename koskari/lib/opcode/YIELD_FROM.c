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
   YIELD_FROM: delegation as a resume. The delegating frame's resume
   is the delegation: its state is the delegate itself (owned), and
   its kind is the delegate's kind and the phase the delegation is in.
   A drive kind waits on the consumer: a next arrives as NULL, a send
   as the value, a throw through the injected-throw channel, and the
   kind steps the delegate once. The emit kind waits on a provider
   delegate whose frames are installed: the element it yields, or its
   end, arrives through the frame. The await kind waits on a python
   async delegate's step: the awaited result arrives through the
   revive. Every element is re-yielded as the delegating generator's
   own with the drive kind set for the send that answers it; the end
   releases the delegate and threads to the next instruction; the
   kind's clear closes the delegate on a teardown with no delivery
   coming, and its clone shares it on capture.

   :author: Christopher O'Brien <obriencj@gmail.com>
   :license: GNU General Public License v3
*/


/* The delegate kinds, decided once at normalization. */
enum {
  KOS_YF_SYNC_ITER = 0,
  KOS_YF_SYNC_GEN = 1,
  KOS_YF_PY_ASYNC_ITER = 2,
  KOS_YF_PY_ASYNC_GEN = 3,
  KOS_YF_KOS_ITER = 4,
};


#define yield_from_check_coro(obj) \
  (KosCoroutine_Check(obj) || PyCoro_CheckExact(obj) || KosDefer_Check(obj))


/**
  Classify obj as a yield-from delegate kind.  Order matters and must
  match the flattening preference: a Koskari async gen is driven
  flattened even though it also speaks the Python async protocol.

  :return: a KOS_YF_* kind, or -1 when obj is none of the delegate
    shapes (callable and plain-iterable fallbacks are the caller's)
*/
static int yield_from_classify(PyObject *obj) {

  if (KosIter_Check(obj))
    return KOS_YF_KOS_ITER;

  if (PyGen_Check(obj))
    return KOS_YF_SYNC_GEN;

  /* Python async iterator / async gen: __anext__ steps; async gen also
     has asend/athrow/aclose. The am_anext test is the one get_await_next
     drives the step with, so classification and stepping agree; asend is
     a method protocol, so its lookup is on the type, with no instance
     getattr and no AttributeError built to be cleared. */
  {
    PyTypeObject *tp = Py_TYPE(obj);
    if (tp->tp_as_async && tp->tp_as_async->am_anext)
      return PyType_Lookup(tp, str_asend)
          ? KOS_YF_PY_ASYNC_GEN
          : KOS_YF_PY_ASYNC_ITER;
  }

  if (PyIter_Check(obj))
    return KOS_YF_SYNC_ITER;

  return -1;
}


/**
  Normalize TOS to a (delegate, kind) for yield-from. On success returns 0
  and sets *delegate_out (new ref) and *kind_out. On failure returns -1.

  Yield-from operates on iterables and async-iterables only. Coroutines
  are rejected (same idea as iter(1) being invalid). We classify by type
  so the handler can drive the delegate with the right protocol.
*/
static int yield_from_normalize(
    PyObject *obj,
    PyObject **delegate_out,
    uint8_t *kind_out) {

  if (! obj) {
    PyErr_SetString(PyExc_TypeError, "yield-from requires an object");
    return -1;
  }

  /* Coroutines are awaitable, not iterable. Using yield-from on a
     coroutine is a type error (like yield from 1). */
  if (yield_from_check_coro(obj)) {
    PyErr_SetString(PyExc_TypeError,
                    "coroutine object is not async iterable");
    return -1;
  }

  {
    int kind = yield_from_classify(obj);
    if (kind >= 0) {
      Py_INCREF(obj);
      *delegate_out = obj;
      *kind_out = (uint8_t) kind;
      return 0;
    }
  }

  /* A callable (e.g. a generator function): call once to get the
     iterator or generator, then classify that. A coroutine result or a
     plain iterator result stays an error; no iter() fallback here. */
  if (PyCallable_Check(obj)) {
    PyObject *gen = PyObject_CallFunctionObjArgs(obj, NULL);
    int kind;

    if (! gen)
      return -1;

    if (yield_from_check_coro(gen)) {
      Py_DECREF(gen);
      PyErr_SetString(PyExc_TypeError,
                      "coroutine object is not async iterable");
      return -1;
    }

    kind = yield_from_classify(gen);
    if (kind >= 0) {
      *delegate_out = gen;
      *kind_out = (uint8_t) kind;
      return 0;
    }

    Py_DECREF(gen);
    PyErr_SetString(PyExc_TypeError,
                    "yield-from callable did not return a generator");
    return -1;
  }

  /* Anything else that supports __iter__ steps as a sync iterator. */
  {
    PyObject *it = PyObject_GetIter(obj);
    if (! it) {
      PyErr_Clear();
      PyErr_Format(PyExc_TypeError,
                   "yield from requires an iterable or async iterable; got '%.50s' object",
                   Py_TYPE(obj)->tp_name);
      return -1;
    }
    *delegate_out = it;
    *kind_out = KOS_YF_SYNC_ITER;
    return 0;
  }
}


/* === the delegate as the resume's state === */


/**
   Close a delegate the delegation is leaving while it may still be
   active: a sync iterator or generator through its close method, a
   provider through its suite. A python async delegate is only
   released: this runs where nothing can await, and aclose would make
   a coroutine nothing awaits; the delegate finalizes itself (a python
   async generator's finalizer hook runs aclose on the event loop). An
   explicit close of the delegating generator reaches the inner one
   through the GeneratorExit throw, which can await.
*/
static void yield_from_close_delegate(PyObject *delegate) {

  KosIterSlots *islots;

  if (KosIter_CheckSlots(delegate, &islots)) {
    islots->ki_close(delegate);
    return;
  }
  if (Py_TYPE(delegate)->tp_as_async && Py_TYPE(delegate)->tp_as_async->am_anext)
    return;
  kos_iter_close(delegate);
}


/* The kind's clear: a teardown with no delivery coming (the frame
   freed, cached or reused with the delegation in flight, a raise walk
   unwinding past it) closes the delegate and releases it. The pending
   exception, when the teardown runs under one, is kept across the
   close. */
static void yield_from_state_clear(void *state) {

  PyObject *delegate = (PyObject *) state;
  PyObject *typ, *val, *tb;

  if (! delegate)
    return;
  PyErr_Fetch(&typ, &val, &tb);
  yield_from_close_delegate(delegate);
  PyErr_Restore(typ, val, tb);
  Py_DECREF(delegate);
}


/* The kind's clone: a capture shares the delegate. */
static void *yield_from_state_clone(void *state) {
  return Py_XNewRef((PyObject *) state);
}


static KOS_NOINLINE KOS_PRESERVE_NONE KosOpResult yf_drive_sync_iter(
    KosContext *ctx, void *state, PyObject *value);
static KOS_NOINLINE KOS_PRESERVE_NONE KosOpResult yf_drive_sync_gen(
    KosContext *ctx, void *state, PyObject *value);
static KOS_NOINLINE KOS_PRESERVE_NONE KosOpResult yf_drive_py_async_iter(
    KosContext *ctx, void *state, PyObject *value);
static KOS_NOINLINE KOS_PRESERVE_NONE KosOpResult yf_drive_py_async_gen(
    KosContext *ctx, void *state, PyObject *value);
static KOS_NOINLINE KOS_PRESERVE_NONE KosOpResult yf_drive_kos_iter(
    KosContext *ctx, void *state, PyObject *value);
static KOS_NOINLINE KOS_PRESERVE_NONE KosOpResult yf_emit_kos_iter(
    KosContext *ctx, void *state, PyObject *value);
static KOS_NOINLINE KOS_PRESERVE_NONE KosOpResult yf_await_py_async(
    KosContext *ctx, void *state, PyObject *value);

KOS_RESUME_KIND_DIRECT(yf_drive_sync_iter_kind, "yield-from",
                       yf_drive_sync_iter, yield_from_state_clear,
                       yield_from_state_clone);
KOS_RESUME_KIND_DIRECT(yf_drive_sync_gen_kind, "yield-from",
                       yf_drive_sync_gen, yield_from_state_clear,
                       yield_from_state_clone);
KOS_RESUME_KIND_DIRECT(yf_drive_py_async_iter_kind, "yield-from",
                       yf_drive_py_async_iter, yield_from_state_clear,
                       yield_from_state_clone);
KOS_RESUME_KIND_DIRECT(yf_drive_py_async_gen_kind, "yield-from",
                       yf_drive_py_async_gen, yield_from_state_clear,
                       yield_from_state_clone);
KOS_RESUME_KIND_DIRECT(yf_drive_kos_iter_kind, "yield-from",
                       yf_drive_kos_iter, yield_from_state_clear,
                       yield_from_state_clone);
KOS_RESUME_KIND_DIRECT(yf_emit_kos_iter_kind, "yield-from",
                       yf_emit_kos_iter, yield_from_state_clear,
                       yield_from_state_clone);
KOS_RESUME_KIND_DIRECT(yf_await_py_async_kind, "yield-from",
                       yf_await_py_async, yield_from_state_clear,
                       yield_from_state_clone);


/* The drive kind a delegate kind elects. */
static const KosResumeKind *yield_from_drive_kind(uint8_t kind) {
  switch (kind) {
    case KOS_YF_SYNC_ITER:
      return &yf_drive_sync_iter_kind;
    case KOS_YF_SYNC_GEN:
      return &yf_drive_sync_gen_kind;
    case KOS_YF_PY_ASYNC_ITER:
      return &yf_drive_py_async_iter_kind;
    case KOS_YF_PY_ASYNC_GEN:
      return &yf_drive_py_async_gen_kind;
    case KOS_YF_KOS_ITER:
      return &yf_drive_kos_iter_kind;
    default:
      return NULL;
  }
}


/* === the phases' shared tails === */


/* Set the delegation's next phase on the live frame: kind, with the
   delegate as its state. */
static inline void yf_set_phase(
    KosContext *ctx,
    const KosResumeKind *kind,
    PyObject *delegate) {

  KosDynFrame *frame = KosContext_FRAME(ctx);

  KosDynFrame_RESUME_KIND(frame) = kind;
  KosDynFrame_RESUME_STATE(frame) = (void *) delegate;
}


/* An element the delegate produced (owned): yield it as the delegating
   generator's own, with the drive kind set for the send that answers
   it. A thread function's tail: the yield event takes it from here,
   never a raw result up the chain. */
#define YF_EMIT(ctx, drive, delegate, element)                          \
  do {                                                                  \
    yf_set_phase((ctx), (drive), (delegate));                           \
    RESULT_RELAY((ctx), EVAL_YIELD(element));                           \
  } while (0)


/* The delegation is over: release the delegate and thread on to the
   instruction after the YIELD_FROM, where the frame's pc already is.
   Koskari yield-from is a void completion. A thread function's tail. */
#define YF_FINISH(ctx, delegate)                                        \
  do {                                                                  \
    Py_DECREF(delegate);                                                \
    KOS_THREAD_NEXT(ctx);                                               \
  } while (0)


/**
   A delivery of NULL with an exception pending, at any phase: a
   failure delivered to the delegating frame while it waited.
   GeneratorExit closes the delegate first (PEP 380) and propagates;
   the delegate's own end (StopIteration, or StopAsyncIteration from
   an async delegate) ends the delegation; anything else propagates
   with the delegate released, as it would have past the node.
   Returns 1 when the delegation ended and the frame threads on, else
   0 with the exception standing and the delegate released.
*/
static int yf_take_failure(PyObject *delegate) {

  if (PyErr_ExceptionMatches(PyExc_StopIteration)
      || PyErr_ExceptionMatches(PyExc_StopAsyncIteration)) {
    PyObject *exc = PyErr_GetRaisedException();
    Py_DECREF(exc);
    return 1;
  }
  if (PyErr_ExceptionMatches(PyExc_GeneratorExit)) {
    PyObject *typ, *val, *tb;
    PyErr_Fetch(&typ, &val, &tb);
    yield_from_close_delegate(delegate);
    PyErr_Restore(typ, val, tb);
  }
  Py_DECREF(delegate);
  return 0;
}


/**
   The drive kinds' common entry. A NULL delivery with an error pending
   is a failure delivered to the waiting frame. Otherwise a throw
   injected for this frame is taken ahead of the delivery, which a
   throw ingress stages as a placeholder, and is forwarded into the
   delegate per its kind. Returns 0 when the drive proceeds to step the
   delegate (*value is the send value, or NULL for a plain next or a
   throw); 1 with *r the result to relay; 2 when the delegation ended
   and the frame threads on.
*/
static int yf_drive_entry(
    KosContext *ctx,
    PyObject *delegate,
    PyObject **value,
    PyObject **throw_type,
    PyObject **throw_exc,
    KosOpResult *r) {

  *throw_type = NULL;
  *throw_exc = NULL;

  if (! *value && PyErr_Occurred()) {
    if (yf_take_failure(delegate)) {
      *r = EVAL_NORMAL();
      return 2;
    }
    *r = EVAL_RAISE();
    return 1;
  }
  if (KosContext_TakeInjectThrow(ctx, throw_type, throw_exc))
    Py_CLEAR(*value);
  return 0;
}


/* === the drives === */


/* A sync iterator steps through PyIter_Next and takes no send. */
static KOS_NOINLINE KOS_PRESERVE_NONE KosOpResult yf_drive_sync_iter(
    KosContext *ctx, void *state, PyObject *value) {

  PyObject *delegate = (PyObject *) state;
  PyObject *throw_type, *throw_exc;
  KosOpResult r;
  PyObject *n;

  switch (yf_drive_entry(ctx, delegate, &value, &throw_type, &throw_exc, &r)) {
    case 1:
      RESULT_RELAY(ctx, r);
    case 2:
      YF_FINISH(ctx, delegate);
    default:
      break;
  }

  if (throw_exc) {
    PyObject *res = NULL;
    PySendResult sr = kos_iter_throw(delegate, throw_exc, &res);

    Py_XDECREF(throw_type);
    Py_DECREF(throw_exc);
    if (sr == PYGEN_NEXT)
      YF_EMIT(ctx, &yf_drive_sync_iter_kind, delegate, res);
    if (sr == PYGEN_RETURN) {
      Py_DECREF(res);
      YF_FINISH(ctx, delegate);
    }
    Py_DECREF(delegate);
    RESULT_RAISE();
  }

  if (value && value != Py_None) {
    Py_DECREF(value);
    Py_DECREF(delegate);
    PyErr_SetString(PyExc_TypeError,
                    "send() to sync iterator only accepts None");
    RESULT_RAISE();
  }
  Py_XDECREF(value);

  n = PyIter_Next(delegate);
  if (n)
    YF_EMIT(ctx, &yf_drive_sync_iter_kind, delegate, n);

  if (PyErr_Occurred()) {
    if (! PyErr_ExceptionMatches(PyExc_StopIteration)) {
      Py_DECREF(delegate);
      RESULT_RAISE();
    }
    /* a StopIteration with a value: the value is discarded */
    PyErr_Clear();
  }
  YF_FINISH(ctx, delegate);
}


/* A sync generator steps through send and throw. */
static KOS_NOINLINE KOS_PRESERVE_NONE KosOpResult yf_drive_sync_gen(
    KosContext *ctx, void *state, PyObject *value) {

  PyObject *delegate = (PyObject *) state;
  PyObject *throw_type, *throw_exc;
  KosOpResult r;
  PyObject *res = NULL;
  PySendResult sr;

  switch (yf_drive_entry(ctx, delegate, &value, &throw_type, &throw_exc, &r)) {
    case 1:
      RESULT_RELAY(ctx, r);
    case 2:
      YF_FINISH(ctx, delegate);
    default:
      break;
  }

  if (throw_exc) {
    sr = kos_iter_throw(delegate, throw_exc, &res);
    Py_XDECREF(throw_type);
    Py_DECREF(throw_exc);
  } else {
    sr = kos_iter_send(delegate, value ? value : Py_None, &res);
    Py_XDECREF(value);
  }

  if (sr == PYGEN_NEXT)
    YF_EMIT(ctx, &yf_drive_sync_gen_kind, delegate, res);
  if (sr == PYGEN_RETURN) {
    /* the delegation ends; its value is discarded */
    Py_DECREF(res);
    YF_FINISH(ctx, delegate);
  }
  Py_DECREF(delegate);
  RESULT_RAISE();
}


/**
   A python async delegate's step: build the awaitable for it
   (__anext__, asend or athrow), step it eagerly, and suspend on it
   with the await kind set when it does not complete at once. gen says
   whether the delegate speaks asend and athrow.
*/
static KosOpResult yf_drive_py_async(
    KosContext *ctx,
    PyObject *delegate,
    PyObject *value,
    int gen) {

  const KosResumeKind *drive =
      gen ? &yf_drive_py_async_gen_kind : &yf_drive_py_async_iter_kind;
  PyObject *throw_type, *throw_exc;
  KosOpResult r;
  PyObject *awaitable = NULL;

  switch (yf_drive_entry(ctx, delegate, &value, &throw_type, &throw_exc, &r)) {
    case 1:
      return r;
    case 2:
      Py_DECREF(delegate);
      return EVAL_NORMAL();
    default:
      break;
  }

  if (throw_exc) {
    if (PyErr_GivenExceptionMatches(throw_exc, PyExc_StopAsyncIteration)) {
      /* throw(StopAsyncIteration) closes the delegation quietly */
      Py_XDECREF(throw_type);
      Py_DECREF(throw_exc);
      Py_DECREF(delegate);
      return EVAL_NORMAL();
    }
    if (gen) {
      PyObject *tmp = PyObject_CallMethodObjArgs(delegate, str_athrow,
                                                 throw_exc, NULL);
      Py_XDECREF(throw_type);
      Py_DECREF(throw_exc);
      if (tmp) {
        awaitable = kos_get_awaitable(tmp);
        Py_DECREF(tmp);
      }
    } else {
      /* no athrow: the thrown exception lands at the yield-from point */
      PyErr_SetObject(throw_type ? throw_type : (PyObject *) Py_TYPE(throw_exc),
                      throw_exc);
      Py_XDECREF(throw_type);
      Py_DECREF(throw_exc);
      Py_DECREF(delegate);
      return EVAL_RAISE();
    }
  } else if (! value || value == Py_None) {
    Py_XDECREF(value);
    awaitable = get_await_next(delegate);
  } else if (gen) {
    PyObject *tmp = PyObject_CallMethodObjArgs(delegate, str_asend, value, NULL);
    Py_DECREF(value);
    if (tmp) {
      awaitable = kos_get_awaitable(tmp);
      Py_DECREF(tmp);
    }
  } else {
    Py_DECREF(value);
    Py_DECREF(delegate);
    PyErr_SetString(PyExc_TypeError,
                    "send() to async iterator only accepts None");
    return EVAL_RAISE();
  }

  if (! awaitable) {
    if (PyErr_ExceptionMatches(PyExc_StopAsyncIteration)) {
      PyErr_Clear();
      Py_DECREF(delegate);
      return EVAL_NORMAL();
    }
    if (! PyErr_Occurred())
      PyErr_Format(PyExc_TypeError,
                   "'%.200s' object is no longer an async iterator",
                   Py_TYPE(delegate)->tp_name);
    Py_DECREF(delegate);
    return EVAL_RAISE();
  }

  {
    int suspended = 0;
    PyObject *yielded = NULL;
    PyObject *result = KosContext_EagerStepAwaitable(
        ctx, awaitable, &suspended, &yielded);

    Py_DECREF(awaitable);

    if (suspended && yielded) {
      if (kos_eval_demand_deny_suspend(
              ctx, "async suspension is not allowed in demand_sync")) {
        Py_DECREF(yielded);
        Py_DECREF(delegate);
        return EVAL_RAISE();
      }
      yf_set_phase(ctx, &yf_await_py_async_kind, delegate);
      KosContext_FlushLiveRegsToFrame(ctx);
      KosContext_MarkDeadAfterYield(ctx);
      return EVAL_AWAIT(yielded);
    }
    if (result) {
      yf_set_phase(ctx, drive, delegate);
      return EVAL_YIELD(result);
    }
    if (PyErr_ExceptionMatches(PyExc_StopAsyncIteration)) {
      PyErr_Clear();
      Py_DECREF(delegate);
      return EVAL_NORMAL();
    }
    Py_DECREF(delegate);
    return EVAL_RAISE();
  }
}


static KOS_NOINLINE KOS_PRESERVE_NONE KosOpResult yf_drive_py_async_iter(
    KosContext *ctx, void *state, PyObject *value) {

  RESULT_RELAY(ctx, yf_drive_py_async(ctx, (PyObject *) state, value, 0));
}


static KOS_NOINLINE KOS_PRESERVE_NONE KosOpResult yf_drive_py_async_gen(
    KosContext *ctx, void *state, PyObject *value) {

  RESULT_RELAY(ctx, yf_drive_py_async(ctx, (PyObject *) state, value, 1));
}


/* The awaited result of a python async delegate's step: the element,
   re-yielded with the drive kind set. */
static KOS_NOINLINE KOS_PRESERVE_NONE KosOpResult yf_await_py_async(
    KosContext *ctx, void *state, PyObject *value) {

  PyObject *delegate = (PyObject *) state;
  int gen = PyType_Lookup(Py_TYPE(delegate), str_asend) != NULL;
  const KosResumeKind *drive =
      gen ? &yf_drive_py_async_gen_kind : &yf_drive_py_async_iter_kind;

  if (! value) {
    if (! PyErr_Occurred()) {
      Py_DECREF(delegate);
      PyErr_SetString(PyExc_RuntimeError,
                      "yield-from resume with no await result");
      RESULT_RAISE();
    }
    if (yf_take_failure(delegate))
      YF_FINISH(ctx, delegate);
    RESULT_RAISE();
  }
  YF_EMIT(ctx, drive, delegate, value);
}


/**
   A provider delegate steps through its suite: a step that installs
   its frames leaves the frame waiting on the emit kind for the
   element, a step that completes at once is emitted or ends the
   delegation here.
*/
static KOS_NOINLINE KOS_PRESERVE_NONE KosOpResult yf_drive_kos_iter(
    KosContext *ctx, void *state, PyObject *value) {

  PyObject *delegate = (PyObject *) state;
  KosDynFrame *frame = KosContext_FRAME(ctx);
  PyObject *throw_type, *throw_exc;
  KosOpResult r;
  KosIterSlots *islots;
  KosIterStepResult sr;
  PyObject *out = NULL;

  switch (yf_drive_entry(ctx, delegate, &value, &throw_type, &throw_exc, &r)) {
    case 1:
      RESULT_RELAY(ctx, r);
    case 2:
      YF_FINISH(ctx, delegate);
    default:
      break;
  }

  if (! KosIter_CheckSlots(delegate, &islots)) {
    Py_XDECREF(value);
    Py_XDECREF(throw_type);
    Py_XDECREF(throw_exc);
    Py_DECREF(delegate);
    PyErr_SetString(PyExc_RuntimeError,
                    "yield-from delegate lost its iter suite");
    RESULT_RAISE();
  }

  if (throw_exc) {
    if (PyErr_GivenExceptionMatches(throw_exc, PyExc_StopAsyncIteration)) {
      Py_XDECREF(throw_type);
      Py_DECREF(throw_exc);
      YF_FINISH(ctx, delegate);
    }
    sr = islots->ki_throw(ctx, delegate,
                          throw_type ? throw_type : (PyObject *) Py_TYPE(throw_exc),
                          throw_exc, &out);
    Py_XDECREF(throw_type);
    Py_DECREF(throw_exc);
  } else {
    sr = kos_iter_step_provider(ctx, delegate, islots, value, &out);
    Py_XDECREF(value);
  }

  switch (sr) {
    case KOS_ITER_INSTALLED:
    case KOS_ITER_INSTALLED_VALUE:
      /* the element arrives through the delegating frame at the emit
         kind, which finishes the delegate's step. The installed frame
         is live now, so the phase goes on the frame read at entry,
         never on KosContext_FRAME. */
      KosDynFrame_RESUME_KIND(frame) = &yf_emit_kos_iter_kind;
      KosDynFrame_RESUME_STATE(frame) = (void *) delegate;
      /* A throw the ingress left raised for the installed frame is
         processed by the prologue, never threaded past. */
      if (PyErr_Occurred())
        RESULT_RELAY(ctx, EVAL_REPROLOGUE());
      RESULT_RELAY(ctx, kos_iter_installed(ctx));

    case KOS_ITER_VALUE:
      YF_EMIT(ctx, &yf_drive_kos_iter_kind, delegate, out);

    case KOS_ITER_EXHAUSTED:
      Py_XDECREF(out);
      YF_FINISH(ctx, delegate);

    case KOS_ITER_ERROR:
    default:
      Py_DECREF(delegate);
      RESULT_RAISE();
  }
}


/* An installed provider delegate's element, delivered through the
   frame: the step is over, so the delegate is finished, and the
   element is re-yielded with the drive kind set. Its end arrives as
   StopIteration, finishes the delegate as exhausted and ends the
   delegation. */
static KOS_NOINLINE KOS_PRESERVE_NONE KosOpResult yf_emit_kos_iter(
    KosContext *ctx, void *state, PyObject *value) {

  PyObject *delegate = (PyObject *) state;
  KosIterSlots *islots = NULL;

  (void) KosIter_CheckSlots(delegate, &islots);

  if (! value) {
    if (! PyErr_Occurred()) {
      Py_DECREF(delegate);
      PyErr_SetString(PyExc_SystemError, "yield-from emit with no value");
      RESULT_RAISE();
    }
    if (islots) {
      int ended = PyErr_ExceptionMatches(PyExc_StopIteration)
          || PyErr_ExceptionMatches(PyExc_StopAsyncIteration);
      islots->ki_finish(delegate, ended && ! islots->ki_pyctx_recyclable);
    }
    if (yf_take_failure(delegate))
      YF_FINISH(ctx, delegate);
    RESULT_RAISE();
  }
  if (islots)
    islots->ki_finish(delegate, 0);
  YF_EMIT(ctx, &yf_drive_kos_iter_kind, delegate, value);
}


/**
  YIELD_FROM: delegate to a sub-iterator or sub-generator. Runs once
  per delegation: TOS is the yield-from expression; normalize it to a
  delegate and its kind, and hop into that kind's drive with NULL, a
  plain next, as the first step. Every later delivery to this frame
  while the delegation is in flight runs the phase the suspension set,
  never this handler: the pc stays past the instruction, and the end
  of the delegation threads on from there.
*/
KOS_OP_HANDLER(YIELD_FROM) {
  KOS_SYNC_PC(ctx, insn);
  PyObject *subexpr;
  PyObject *delegate = NULL;
  uint8_t kind = KOS_YF_SYNC_ITER;
  const KosResumeKind *drive;

  subexpr = KosContext_StackPopRef(ctx);
  if (! subexpr)
    RESULT_RAISE();

  if (yield_from_normalize(subexpr, &delegate, &kind) < 0) {
    Py_DECREF(subexpr);
    RESULT_RAISE();
  }
  Py_DECREF(subexpr);

  drive = yield_from_drive_kind(kind);
  RESUME_HOP(drive, ctx, delegate, NULL);
}


// The end.
