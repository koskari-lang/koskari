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
   as the value, and the kind steps the delegate once; a throw arrives
   at the drive's throw kind with the exception as its value, and
   that kind throws it into the delegate. The emit kind waits on a provider
   delegate whose frames are installed: the element it yields, or its
   end, arrives through the frame. An await kind, one per python async
   drive, waits on the delegate's step: the awaited result arrives through the
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


/* The kind's traverse: the delegate is the state's one owned object. */
static int yield_from_state_traverse(void *state, visitproc visit, void *arg) {
  Py_VISIT((PyObject *) state);
  return 0;
}


/* Every yield-from kind owns its delegate the same way, and none
   clones: a capture that reached a delegating frame would be refused
   by name, and none can, since the delegate in flight sits above the
   frame as a generator node or a native frame, which refuse first, or
   runs outside the evaluator with no capture point inside it. One
   owner, so the clear's close at teardown is the delegate's only. */
#define YF_KIND(ident, fnp)                                             \
  KOS_RESUME_KIND_DIRECT_OWNING(ident, "yield-from", fnp,               \
                                yield_from_state_clear, NULL,           \
                                yield_from_state_traverse)

#define YF_KIND_THROWABLE(ident, fnp, throwp)                           \
  KOS_RESUME_KIND_DIRECT_OWNING_THROWABLE(ident, "yield-from", fnp,     \
                                          yield_from_state_clear, NULL, \
                                          yield_from_state_traverse,    \
                                          throwp)


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
static KOS_NOINLINE KOS_PRESERVE_NONE KosOpResult yf_throw_sync_iter(
    KosContext *ctx, void *state, PyObject *exc);
static KOS_NOINLINE KOS_PRESERVE_NONE KosOpResult yf_throw_sync_gen(
    KosContext *ctx, void *state, PyObject *exc);
static KOS_NOINLINE KOS_PRESERVE_NONE KosOpResult yf_throw_py_async_iter(
    KosContext *ctx, void *state, PyObject *exc);
static KOS_NOINLINE KOS_PRESERVE_NONE KosOpResult yf_throw_py_async_gen(
    KosContext *ctx, void *state, PyObject *exc);
static KOS_NOINLINE KOS_PRESERVE_NONE KosOpResult yf_throw_kos_iter(
    KosContext *ctx, void *state, PyObject *exc);
static KOS_NOINLINE KOS_PRESERVE_NONE KosOpResult yf_emit_kos_iter(
    KosContext *ctx, void *state, PyObject *value);
static KOS_NOINLINE KOS_PRESERVE_NONE KosOpResult yf_await_py_async_iter(
    KosContext *ctx, void *state, PyObject *value);
static KOS_NOINLINE KOS_PRESERVE_NONE KosOpResult yf_await_py_async_gen(
    KosContext *ctx, void *state, PyObject *value);

/* The throw kinds: a throw into a waiting drive, the exception object
   as the value, thrown into the delegate per its kind. */
YF_KIND(yf_throw_sync_iter_kind, yf_throw_sync_iter);
YF_KIND(yf_throw_sync_gen_kind, yf_throw_sync_gen);
YF_KIND(yf_throw_py_async_iter_kind, yf_throw_py_async_iter);
YF_KIND(yf_throw_py_async_gen_kind, yf_throw_py_async_gen);
YF_KIND(yf_throw_kos_iter_kind, yf_throw_kos_iter);

/* The drive kinds: a next or a send from the consumer, each naming
   the throw kind that takes a throw instead. */
YF_KIND_THROWABLE(yf_drive_sync_iter_kind, yf_drive_sync_iter,
                  &yf_throw_sync_iter_kind);
YF_KIND_THROWABLE(yf_drive_sync_gen_kind, yf_drive_sync_gen,
                  &yf_throw_sync_gen_kind);
YF_KIND_THROWABLE(yf_drive_py_async_iter_kind, yf_drive_py_async_iter,
                  &yf_throw_py_async_iter_kind);
YF_KIND_THROWABLE(yf_drive_py_async_gen_kind, yf_drive_py_async_gen,
                  &yf_throw_py_async_gen_kind);
YF_KIND_THROWABLE(yf_drive_kos_iter_kind, yf_drive_kos_iter,
                  &yf_throw_kos_iter_kind);

/* The waits on the delegate: an installed provider's element, an
   awaited python async step, one per python async drive. */
YF_KIND(yf_emit_kos_iter_kind, yf_emit_kos_iter);
YF_KIND(yf_await_py_async_iter_kind, yf_await_py_async_iter);
YF_KIND(yf_await_py_async_gen_kind, yf_await_py_async_gen);


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


/* Set the delegation's next phase on the delegating frame: kind, with
   the delegate as its state. Every kind reads that frame once at
   entry and writes its phases there, never through KosContext_FRAME,
   which a step that installs the delegate's frames has moved. */
static inline void yf_set_phase(
    KosDynFrame *frame,
    const KosResumeKind *kind,
    PyObject *delegate) {

  KosDynFrame_RESUME_KIND(frame) = kind;
  KosDynFrame_RESUME_STATE(frame) = (void *) delegate;
}


/* An element the delegate produced (owned): yield it as the delegating
   generator's own, with the drive kind set for the send that answers
   it. A thread function's tail: the yield event takes it from here,
   never a raw result up the chain. */
#define YF_EMIT(ctx, frame, drive, delegate, element)                   \
  do {                                                                  \
    yf_set_phase((frame), (drive), (delegate));                         \
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


/* The drive kinds' common entry: a NULL delivery with an error
   pending is a failure delivered to the waiting frame. Relays it or
   finishes the delegation; falls through on a delivery to step on. */
#define YF_DRIVE_ENTRY(ctx, delegate, value)                            \
  do {                                                                  \
    if (! (value) && PyErr_Occurred()) {                                \
      if (yf_take_failure(delegate))                                    \
        YF_FINISH((ctx), (delegate));                                   \
      RESULT_RAISE();                                                   \
    }                                                                   \
  } while (0)


/* A throw kind's entry: the exception object is the delivery; a NULL
   is a failure delivered to the waiting frame, taken as the drive's
   would be. */
#define YF_THROW_ENTRY(ctx, delegate, exc)                              \
  do {                                                                  \
    if (! (exc)) {                                                      \
      if (! PyErr_Occurred()) {                                         \
        Py_DECREF(delegate);                                            \
        PyErr_SetString(PyExc_SystemError,                              \
                        "yield-from throw with no exception");          \
        RESULT_RAISE();                                                 \
      }                                                                 \
      if (yf_take_failure(delegate))                                    \
        YF_FINISH((ctx), (delegate));                                   \
      RESULT_RAISE();                                                   \
    }                                                                   \
  } while (0)


/* The outcome of a sync delegate's send or throw (res owned on NEXT
   and RETURN): the element re-yielded, the end, or the failure. */
#define YF_SYNC_OUTCOME(ctx, frame, drive, delegate, sr, res)           \
  do {                                                                  \
    if ((sr) == PYGEN_NEXT)                                             \
      YF_EMIT((ctx), (frame), (drive), (delegate), (res));              \
    if ((sr) == PYGEN_RETURN) {                                         \
      /* the delegation ends; its value is discarded */                 \
      Py_DECREF(res);                                                   \
      YF_FINISH((ctx), (delegate));                                     \
    }                                                                   \
    Py_DECREF(delegate);                                                \
    RESULT_RAISE();                                                     \
  } while (0)


/* === the sync drives === */


/* A sync iterator steps through PyIter_Next and takes no send. */
static KOS_NOINLINE KOS_PRESERVE_NONE KosOpResult yf_drive_sync_iter(
    KosContext *ctx, void *state, PyObject *value) {

  PyObject *delegate = (PyObject *) state;
  KosDynFrame *frame = KosContext_FRAME(ctx);
  PyObject *n;

  YF_DRIVE_ENTRY(ctx, delegate, value);

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
    YF_EMIT(ctx, frame, &yf_drive_sync_iter_kind, delegate, n);

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


/* A throw into a sync iterator: through its throw when it has one,
   else it raises at the yield-from point (kos_iter_throw). */
static KOS_NOINLINE KOS_PRESERVE_NONE KosOpResult yf_throw_sync_iter(
    KosContext *ctx, void *state, PyObject *exc) {

  PyObject *delegate = (PyObject *) state;
  KosDynFrame *frame = KosContext_FRAME(ctx);
  PyObject *res = NULL;
  PySendResult sr;

  YF_THROW_ENTRY(ctx, delegate, exc);

  sr = kos_iter_throw(delegate, exc, &res);
  Py_DECREF(exc);
  YF_SYNC_OUTCOME(ctx, frame, &yf_drive_sync_iter_kind, delegate, sr, res);
}


/* A sync generator steps through send. */
static KOS_NOINLINE KOS_PRESERVE_NONE KosOpResult yf_drive_sync_gen(
    KosContext *ctx, void *state, PyObject *value) {

  PyObject *delegate = (PyObject *) state;
  KosDynFrame *frame = KosContext_FRAME(ctx);
  PyObject *res = NULL;
  PySendResult sr;

  YF_DRIVE_ENTRY(ctx, delegate, value);

  sr = kos_iter_send(delegate, value ? value : Py_None, &res);
  Py_XDECREF(value);
  YF_SYNC_OUTCOME(ctx, frame, &yf_drive_sync_gen_kind, delegate, sr, res);
}


/* A throw into a sync generator: through its throw. */
static KOS_NOINLINE KOS_PRESERVE_NONE KosOpResult yf_throw_sync_gen(
    KosContext *ctx, void *state, PyObject *exc) {

  PyObject *delegate = (PyObject *) state;
  KosDynFrame *frame = KosContext_FRAME(ctx);
  PyObject *res = NULL;
  PySendResult sr;

  YF_THROW_ENTRY(ctx, delegate, exc);

  sr = kos_iter_throw(delegate, exc, &res);
  Py_DECREF(exc);
  YF_SYNC_OUTCOME(ctx, frame, &yf_drive_sync_gen_kind, delegate, sr, res);
}


/* === the python async drives === */


/**
   A python async delegate's step, the awaitable (consumed) for
   __anext__, asend or athrow in hand: step it eagerly, and suspend on
   it with the await kind set when it does not complete at once. A
   plain function: its result is raw, for its thread-function caller
   to relay.
*/
static KosOpResult yf_py_async_await(
    KosContext *ctx,
    KosDynFrame *frame,
    PyObject *delegate,
    PyObject *awaitable,
    const KosResumeKind *drive,
    const KosResumeKind *await_kind) {

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
    yf_set_phase(frame, await_kind, delegate);
    KosContext_FlushLiveRegsToFrame(ctx);
    KosContext_MarkDeadAfterYield(ctx);
    return EVAL_AWAIT(yielded);
  }
  if (result) {
    yf_set_phase(frame, drive, delegate);
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


/* No awaitable came of a step method: the delegate's end, or the
   error. Raw, for the caller to relay. */
static KosOpResult yf_py_async_no_awaitable(PyObject *delegate) {

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


/* The step the awaitable, or its absence, makes: a thread function's
   tail. */
#define YF_PY_ASYNC_STEP(ctx, frame, delegate, awaitable, drive, await_kind) \
  do {                                                                  \
    if (! (awaitable))                                                  \
      RESULT_RELAY((ctx), yf_py_async_no_awaitable(delegate));          \
    RESULT_RELAY((ctx), yf_py_async_await((ctx), (frame), (delegate),   \
                                          (awaitable), (drive),         \
                                          (await_kind)));               \
  } while (0)


/* A python async iterator steps through __anext__ and takes no send. */
static KOS_NOINLINE KOS_PRESERVE_NONE KosOpResult yf_drive_py_async_iter(
    KosContext *ctx, void *state, PyObject *value) {

  PyObject *delegate = (PyObject *) state;
  KosDynFrame *frame = KosContext_FRAME(ctx);
  PyObject *awaitable;

  YF_DRIVE_ENTRY(ctx, delegate, value);

  if (value && value != Py_None) {
    Py_DECREF(value);
    Py_DECREF(delegate);
    PyErr_SetString(PyExc_TypeError,
                    "send() to async iterator only accepts None");
    RESULT_RAISE();
  }
  Py_XDECREF(value);

  awaitable = get_await_next(delegate);
  YF_PY_ASYNC_STEP(ctx, frame, delegate, awaitable,
                   &yf_drive_py_async_iter_kind, &yf_await_py_async_iter_kind);
}


/* A python async generator steps through __anext__ or asend. */
static KOS_NOINLINE KOS_PRESERVE_NONE KosOpResult yf_drive_py_async_gen(
    KosContext *ctx, void *state, PyObject *value) {

  PyObject *delegate = (PyObject *) state;
  KosDynFrame *frame = KosContext_FRAME(ctx);
  PyObject *awaitable = NULL;

  YF_DRIVE_ENTRY(ctx, delegate, value);

  if (! value || value == Py_None) {
    Py_XDECREF(value);
    awaitable = get_await_next(delegate);
  } else {
    PyObject *tmp = PyObject_CallMethodObjArgs(delegate, str_asend, value, NULL);
    Py_DECREF(value);
    if (tmp) {
      awaitable = kos_get_awaitable(tmp);
      Py_DECREF(tmp);
    }
  }
  YF_PY_ASYNC_STEP(ctx, frame, delegate, awaitable,
                   &yf_drive_py_async_gen_kind, &yf_await_py_async_gen_kind);
}


/* A throw into a python async iterator with no athrow: a
   StopAsyncIteration closes the delegation quietly, anything else
   raises at the yield-from point. */
static KOS_NOINLINE KOS_PRESERVE_NONE KosOpResult yf_throw_py_async_iter(
    KosContext *ctx, void *state, PyObject *exc) {

  PyObject *delegate = (PyObject *) state;

  YF_THROW_ENTRY(ctx, delegate, exc);

  if (PyErr_GivenExceptionMatches(exc, PyExc_StopAsyncIteration)) {
    Py_DECREF(exc);
    YF_FINISH(ctx, delegate);
  }
  PyErr_SetObject((PyObject *) Py_TYPE(exc), exc);
  Py_DECREF(exc);
  Py_DECREF(delegate);
  RESULT_RAISE();
}


/* A throw into a python async generator: through athrow, awaited as
   a step. A StopAsyncIteration closes the delegation quietly. */
static KOS_NOINLINE KOS_PRESERVE_NONE KosOpResult yf_throw_py_async_gen(
    KosContext *ctx, void *state, PyObject *exc) {

  PyObject *delegate = (PyObject *) state;
  KosDynFrame *frame = KosContext_FRAME(ctx);
  PyObject *awaitable = NULL;
  PyObject *tmp;

  YF_THROW_ENTRY(ctx, delegate, exc);

  if (PyErr_GivenExceptionMatches(exc, PyExc_StopAsyncIteration)) {
    Py_DECREF(exc);
    YF_FINISH(ctx, delegate);
  }
  tmp = PyObject_CallMethodObjArgs(delegate, str_athrow, exc, NULL);
  Py_DECREF(exc);
  if (tmp) {
    awaitable = kos_get_awaitable(tmp);
    Py_DECREF(tmp);
  }
  YF_PY_ASYNC_STEP(ctx, frame, delegate, awaitable,
                   &yf_drive_py_async_gen_kind, &yf_await_py_async_gen_kind);
}


/* The awaited result of a python async delegate's step: the element,
   re-yielded with the drive kind set; a failure taken as the drive
   would take it. */
#define YF_AWAIT_OUTCOME(ctx, frame, delegate, value, drive)            \
  do {                                                                  \
    if (! (value)) {                                                    \
      if (! PyErr_Occurred()) {                                         \
        Py_DECREF(delegate);                                            \
        PyErr_SetString(PyExc_RuntimeError,                             \
                        "yield-from resume with no await result");      \
        RESULT_RAISE();                                                 \
      }                                                                 \
      if (yf_take_failure(delegate))                                    \
        YF_FINISH((ctx), (delegate));                                   \
      RESULT_RAISE();                                                   \
    }                                                                   \
    YF_EMIT((ctx), (frame), (drive), (delegate), (value));              \
  } while (0)


static KOS_NOINLINE KOS_PRESERVE_NONE KosOpResult yf_await_py_async_iter(
    KosContext *ctx, void *state, PyObject *value) {

  PyObject *delegate = (PyObject *) state;
  KosDynFrame *frame = KosContext_FRAME(ctx);

  YF_AWAIT_OUTCOME(ctx, frame, delegate, value, &yf_drive_py_async_iter_kind);
}


static KOS_NOINLINE KOS_PRESERVE_NONE KosOpResult yf_await_py_async_gen(
    KosContext *ctx, void *state, PyObject *value) {

  PyObject *delegate = (PyObject *) state;
  KosDynFrame *frame = KosContext_FRAME(ctx);

  YF_AWAIT_OUTCOME(ctx, frame, delegate, value, &yf_drive_py_async_gen_kind);
}


/* === the provider drive === */


/* The outcome of a provider delegate's step or throw, sr with out: an
   install leaves the frame read at entry waiting on the emit kind for
   the element, a value is emitted, the end ends the delegation. */
#define YF_KOS_ITER_OUTCOME(ctx, frame, delegate, sr, out)              \
  do {                                                                  \
    switch (sr) {                                                       \
      case KOS_ITER_INSTALLED:                                          \
      case KOS_ITER_INSTALLED_VALUE:                                    \
        /* the element arrives through the delegating frame at the     \
           emit kind, which finishes the delegate's step. The          \
           installed frame is live now, so the phase goes on the       \
           frame read at entry, never on KosContext_FRAME. */          \
        KosDynFrame_RESUME_KIND(frame) = &yf_emit_kos_iter_kind;       \
        KosDynFrame_RESUME_STATE(frame) = (void *) (delegate);         \
        /* A throw the ingress left raised for the installed frame is  \
           processed by the prologue, never threaded past. */          \
        if (PyErr_Occurred())                                          \
          RESULT_RELAY((ctx), EVAL_REPROLOGUE());                      \
        RESULT_RELAY((ctx), kos_iter_installed(ctx));                  \
      case KOS_ITER_VALUE:                                              \
        YF_EMIT((ctx), (frame), &yf_drive_kos_iter_kind, (delegate),    \
                (out));                                                 \
      case KOS_ITER_EXHAUSTED:                                          \
        Py_XDECREF(out);                                                \
        YF_FINISH((ctx), (delegate));                                   \
      case KOS_ITER_ERROR:                                              \
      default:                                                          \
        Py_DECREF(delegate);                                            \
        RESULT_RAISE();                                                 \
    }                                                                   \
  } while (0)


/* A provider delegate that lost its suite: a failure at the
   yield-from point. */
#define YF_KOS_ITER_SLOTS(delegate, islots, extra)                      \
  do {                                                                  \
    if (! KosIter_CheckSlots((delegate), &(islots))) {                  \
      Py_XDECREF(extra);                                                \
      Py_DECREF(delegate);                                              \
      PyErr_SetString(PyExc_RuntimeError,                               \
                      "yield-from delegate lost its iter suite");       \
      RESULT_RAISE();                                                   \
    }                                                                   \
  } while (0)


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
  KosIterSlots *islots;
  KosIterStepResult sr;
  PyObject *out = NULL;

  YF_DRIVE_ENTRY(ctx, delegate, value);
  YF_KOS_ITER_SLOTS(delegate, islots, value);

  sr = kos_iter_step_provider(ctx, delegate, islots, value, &out);
  Py_XDECREF(value);
  YF_KOS_ITER_OUTCOME(ctx, frame, delegate, sr, out);
}


/* A throw into a provider delegate: through its suite's throw. A
   StopAsyncIteration closes the delegation quietly. */
static KOS_NOINLINE KOS_PRESERVE_NONE KosOpResult yf_throw_kos_iter(
    KosContext *ctx, void *state, PyObject *exc) {

  PyObject *delegate = (PyObject *) state;
  KosDynFrame *frame = KosContext_FRAME(ctx);
  KosIterSlots *islots;
  KosIterStepResult sr;
  PyObject *out = NULL;

  YF_THROW_ENTRY(ctx, delegate, exc);
  YF_KOS_ITER_SLOTS(delegate, islots, exc);

  if (PyErr_GivenExceptionMatches(exc, PyExc_StopAsyncIteration)) {
    Py_DECREF(exc);
    YF_FINISH(ctx, delegate);
  }
  sr = islots->ki_throw(ctx, delegate, (PyObject *) Py_TYPE(exc), exc, &out);
  Py_DECREF(exc);
  YF_KOS_ITER_OUTCOME(ctx, frame, delegate, sr, out);
}


/* An installed provider delegate's element, delivered through the
   frame: the step is over, so the delegate is finished, and the
   element is re-yielded with the drive kind set. Its end arrives as
   StopIteration, finishes the delegate as exhausted and ends the
   delegation. */
static KOS_NOINLINE KOS_PRESERVE_NONE KosOpResult yf_emit_kos_iter(
    KosContext *ctx, void *state, PyObject *value) {

  PyObject *delegate = (PyObject *) state;
  KosDynFrame *frame = KosContext_FRAME(ctx);
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
  YF_EMIT(ctx, frame, &yf_drive_kos_iter_kind, delegate, value);
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
