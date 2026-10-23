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

  /* KosIter provider: flattened into the same eval loop
     (see yield_from_drive_kos_iter). */
  if (KosIter_Check(obj))
    return KOS_YF_KOS_ITER;

  /* Python sync generator: send/throw/close; send(None) steps. */
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

  /* Sync iterator: PyIter_Next steps; no send/close. */
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

  /* --- Reject non-iterables ---
     Coroutines are awaitable, not iterable. Using yield-from on a
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

  /* --- Callable (e.g. generator function) ---
     Call once to get the actual iterator/gen; then classify as above.
     Reject if the call returns a coroutine or a non-delegate shape
     (a plain iterator result stays an error; no iter() fallback here). */
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

  /* --- Fallback: sync iteration via iter(obj) ---
     For any other object that supports __iter__. If obj is not iterable,
     set a clear TypeError so the user sees that yield-from requires an
     iterable or async iterable. */
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


/**
  Reset the pending inject state back to a plain STEP: release
  PENDING_ARG / PENDING_ARG_TYPE and set PENDING_ACTION.
*/
static void yield_from_clear_pending(KosDynYieldFrom *yf) {

  Py_XDECREF(KosDynYieldFrom_PENDING_ARG(yf));
  KosDynYieldFrom_PENDING_ARG(yf) = NULL;
  Py_XDECREF(KosDynYieldFrom_PENDING_ARG_TYPE(yf));
  KosDynYieldFrom_PENDING_ARG_TYPE(yf) = NULL;
  KosDynYieldFrom_PENDING_ACTION(yf) = KOS_YF_STEP;
}


/**
  Consume the resume-from-await inject: release INFLIGHT and take the
  stashed await result out of PENDING_ARG (ownership transfers to the
  caller), resetting PENDING_ACTION to STEP.

  :return: the await result (new ref), or NULL with PyErr set
*/
static PyObject *yield_from_take_await_result(KosDynYieldFrom *yf) {

  PyObject *aw_result = KosDynYieldFrom_PENDING_ARG(yf);

  Py_XDECREF(KosDynYieldFrom_INFLIGHT(yf));
  KosDynYieldFrom_INFLIGHT(yf) = NULL;
  KosDynYieldFrom_PENDING_ARG(yf) = NULL;
  KosDynYieldFrom_PENDING_ACTION(yf) = KOS_YF_STEP;

  if (! aw_result)
    PyErr_SetString(PyExc_RuntimeError,
                    "yield-from resume with no await result");
  return aw_result;
}


/* the resume kinds: one static record per resume function, its
   name and lifecycle; no clear (the node owns the state) and a shared
   clone (the node is captured with the chain, the frame's arm follows
   it as it is) */
static KOS_NOINLINE KOS_PRESERVE_NONE KosOpResult yield_from_resume_await(
    KosContext *ctx, void *state, PyObject *value);
static KOS_NOINLINE KOS_PRESERVE_NONE KosOpResult yield_from_resume_emit(
    KosContext *ctx, void *state, PyObject *value);
static KOS_NOINLINE KOS_PRESERVE_NONE KosOpResult yield_from_resume_send(
    KosContext *ctx, void *state, PyObject *value);

KOS_RESUME_KIND_AS_IS(yield_from_resume_await_kind, "yield-from",
                      yield_from_resume_await, NULL, kos_resume_clone_shared);
KOS_RESUME_KIND_AS_IS(yield_from_resume_emit_kind, "yield-from",
                      yield_from_resume_emit, NULL, kos_resume_clone_shared);
KOS_RESUME_KIND_AS_IS(yield_from_resume_send_kind, "yield-from",
                      yield_from_resume_send, NULL, kos_resume_clone_shared);


/**
  Yield value (owned, consumed) out of the delegating generator as this
  generator's own yield: set the send resume for the value that answers
  it and return EVAL_YIELD. Nothing is left on the operand stack.
*/
static KOS_NOINLINE KosOpResult yield_from_yield_value(
    KosContext *ctx,
    KosDynYieldFrom *yf,
    PyObject *payload) {

  /*
    No demand deny here: this is a yield-shaped suspension, and demand
    generators yield as iteration (the demand bit denies await only).
    A demand-flagged frame can only reach YIELD_FROM as a demand-driven
    generator; the await-shaped suspension below keeps its deny.
  */

  kos_dyn_yield_from_set_resume(ctx, yf, &yield_from_resume_send_kind);
  SAYF("[yf] emit_yield dyn_top=%p frame=%p payload=%p -> KOS_RES_YIELD\n",
       (void *) KosContext_CurrentNode(ctx),
       (void *) KosContext_FRAME(ctx), (void *) payload);

  return EVAL_YIELD(payload);
}


/**
  Delegate exhausted: pop the yield-from node and continue normally
  (Koskari yield-from is a void completion).
*/
static KOS_NOINLINE KosOpResult yield_from_finish(
    KosContext *ctx,
    KosDynYieldFrom *yf) {

  if (unlikely(kos_dyn_pop_yield_from(ctx, yf) < 0))
    return EVAL_RAISE();

  return EVAL_NORMAL();
}


/**
  Consume active StopIteration and discard its value (do not push or return).
*/
static void yield_from_discard_stop_iteration_value(void) {

  PyObject *value = NULL;
  PyGen_FetchStopIterationValue(&value);
  Py_XDECREF(value);
}


/**
  Active StopAsyncIteration ends the delegation: consume the raised
  exception, its value with it, pop the yield-from node.
*/
static KOS_NOINLINE KosOpResult yield_from_finish_stop_async(
    KosContext *ctx,
    KosDynYieldFrom *yf) {

  PyObject *exc = PyErr_GetRaisedException();
  Py_DECREF(exc);

  kos_dyn_pop_yield_from(ctx, yf);

  return EVAL_NORMAL();
}


/**
  THROW(StopAsyncIteration) closes the delegation quietly: when the
  pending inject is a StopAsyncIteration instance, discard it and its
  value, pop the yield-from node.

  :return: 1 when handled (node popped), 0 when not applicable
*/
static KOS_NOINLINE int yield_from_throw_stop_async_finish(
    KosContext *ctx,
    KosDynYieldFrom *yf) {

  PyObject *exc = KosDynYieldFrom_PENDING_ARG(yf);

  if (! exc || ! PyObject_IsInstance(exc, (PyObject *) PyExc_StopAsyncIteration))
    return 0;

  yield_from_clear_pending(yf);
  kos_dyn_pop_yield_from(ctx, yf);
  return 1;
}


/**
  Throw the pending exception into a sync delegate, per PEP 380: a
  GeneratorExit closes the delegate and re-raises; a delegate with a
  throw method receives the exception and may yield on, return, or let
  it propagate; one without gets it raised at the yield-from point,
  where the delegating generator's own handlers take it.
*/
static KosOpResult yield_from_throw_sync(
    KosContext *ctx,
    KosDynYieldFrom *yf,
    PyObject *delegate) {

  PyObject *exc = KosDynYieldFrom_PENDING_ARG(yf);
  if (! exc) {
    PyErr_SetString(PyExc_SystemError,
                    "yield-from throw with no exception");
    return EVAL_RAISE();
  }
  PyObject *res = NULL;
  PySendResult r = kos_iter_throw(delegate, exc, &res);

  if (r == PYGEN_NEXT) {
    yield_from_clear_pending(yf);
    return yield_from_yield_value(ctx, yf, res);
  }
  if (r == PYGEN_RETURN) {
    Py_DECREF(res);
    return yield_from_finish(ctx, yf);
  }
  /* The delegate let it propagate, or raised in its place: it lands
     here and the raise walk retires the node. */
  return EVAL_RAISE();
}


/**
  Drive one step of a sync-iterator delegate (PyIter_Next). On a yielded
  value returns EVAL_YIELD; on exhaustion returns EVAL_NORMAL; on error
  returns EVAL_RAISE.
*/
static KosOpResult yield_from_drive_sync_iter(
    KosContext *ctx,
    KosDynYieldFrom *yf,
    PyObject *delegate,
    uint8_t action) {

  if (action == KOS_YF_THROW)
    return yield_from_throw_sync(ctx, yf, delegate);

  if (action == KOS_YF_SEND && KosDynYieldFrom_PENDING_ARG(yf) != Py_None) {
    PyErr_SetString(PyExc_TypeError,
                    "send() to sync iterator only accepts None");
    return EVAL_RAISE();
  }

  PyObject *n = PyIter_Next(delegate);
  if (n)
    return yield_from_yield_value(ctx, yf, n);

  /*
    PyIter_Next returns NULL with no exception set on normal exhaustion.
    StopIteration (with optional .value) is only set for some iterators /
    gen.send paths; when present, discard the value per yield-from rules.
  */
  if (PyErr_Occurred()) {
    if (! PyErr_ExceptionMatches(PyExc_StopIteration))
      return EVAL_RAISE();
    yield_from_discard_stop_iteration_value();
  }

  return yield_from_finish(ctx, yf);
}


/**
  Drive one step of a sync-generator delegate (send/throw/close).
  Check exhaustion (StopIteration) before treating non-NULL send result
  as a yielded value.
*/
static KosOpResult yield_from_drive_sync_gen(
    KosContext *ctx,
    KosDynYieldFrom *yf,
    PyObject *delegate,
    uint8_t action) {

  PyObject *send_val = Py_None;
  if (action == KOS_YF_SEND && KosDynYieldFrom_PENDING_ARG(yf))
    send_val = KosDynYieldFrom_PENDING_ARG(yf);

  if (action == KOS_YF_THROW)
    return yield_from_throw_sync(ctx, yf, delegate);

  {
    PyObject *res = NULL;
    PySendResult r = kos_iter_send(delegate, send_val, &res);

    if (r == PYGEN_NEXT)
      return yield_from_yield_value(ctx, yf, res);
    if (r == PYGEN_RETURN) {
      /* the delegation ends; its value is discarded, as it was */
      Py_DECREF(res);
      return yield_from_finish(ctx, yf);
    }
    return EVAL_RAISE();
  }
}


/**
  Drive one step of a Python async-iterator or async-generator
  (__anext__ / asend / athrow / aclose). kind is KOS_YF_PY_ASYNC_ITER
  or KOS_YF_PY_ASYNC_GEN.
*/
static KosOpResult yield_from_drive_py_async(
    KosContext *ctx,
    KosDynYieldFrom *yf,
    PyObject *delegate,
    uint8_t kind,
    uint8_t action) {

  /* Resuming from await: we had suspended with INFLIGHT set; the
     result was stashed in PENDING_ARG. Push it and emit YIELD. */
  if (KosDynYieldFrom_INFLIGHT(yf)) {
    PyObject *aw_result = yield_from_take_await_result(yf);
    if (! aw_result)
      return EVAL_RAISE();
    return yield_from_yield_value(ctx, yf, aw_result);
  }

  /* Throw(StopAsyncIteration): delegate was closed with that; discard value. */
  if (action == KOS_YF_THROW
      && yield_from_throw_stop_async_finish(ctx, yf))
    return EVAL_NORMAL();

  /* Build the awaitable for this step: __anext__, asend, or athrow. */
  PyObject *awaitable = NULL;
  if (action == KOS_YF_STEP
      || (action == KOS_YF_SEND
          && (! KosDynYieldFrom_PENDING_ARG(yf)
              || KosDynYieldFrom_PENDING_ARG(yf) == Py_None))) {
    awaitable = get_await_next(delegate);

  } else if (kind == KOS_YF_PY_ASYNC_GEN && action == KOS_YF_SEND) {
    PyObject *send_val = KosDynYieldFrom_PENDING_ARG(yf);
    PyObject *tmp = PyObject_CallMethodObjArgs(delegate, str_asend, send_val, NULL);
    if (tmp) {
      awaitable = kos_get_awaitable(tmp);
      Py_DECREF(tmp);
    }

  } else if (kind == KOS_YF_PY_ASYNC_GEN && action == KOS_YF_THROW) {
    PyObject *exc = KosDynYieldFrom_PENDING_ARG(yf);
    if (exc) {
      PyObject *tmp = PyObject_CallMethodObjArgs(delegate, str_athrow, exc,
                                                 NULL);
      if (tmp) {
        awaitable = kos_get_awaitable(tmp);
        Py_DECREF(tmp);
      }
    }

  } else if (action == KOS_YF_THROW) {
    /* PY_ASYNC_ITER has no athrow: the thrown exception lands here,
       and the raise walk retires the node */
    PyObject *exc = KosDynYieldFrom_PENDING_ARG(yf);
    PyObject *typ = KosDynYieldFrom_PENDING_ARG_TYPE(yf);
    PyErr_SetObject(typ ? typ : (PyObject *) Py_TYPE(exc), exc);
    return EVAL_RAISE();

  } else {
    PyErr_SetString(PyExc_TypeError,
                    "send() to async iterator only accepts None");
    return EVAL_RAISE();
  }

  if (! awaitable) {
    if (PyErr_ExceptionMatches(PyExc_StopAsyncIteration))
      return yield_from_finish_stop_async(ctx, yf);
    /* no error: the type lost its __anext__ since the bind classified
       it; the kind is fixed, so the step cannot be taken another way */
    if (! PyErr_Occurred())
      PyErr_Format(PyExc_TypeError,
                   "'%.200s' object is no longer an async iterator",
                   Py_TYPE(delegate)->tp_name);
    return EVAL_RAISE();
  }

  int suspended = 0;
  PyObject *yielded = NULL;
  PyObject *result = KosContext_EagerStepAwaitable(
      ctx, awaitable, &suspended, &yielded);

  if (suspended && yielded) {
    if (kos_eval_demand_deny_suspend(ctx,
        "async suspension is not allowed in demand_sync")) {
      Py_DECREF(awaitable);
      Py_DECREF(yielded);
      return EVAL_RAISE();
    }
    KosDynYieldFrom_INFLIGHT(yf) = awaitable;
    kos_dyn_yield_from_set_resume(ctx, yf, &yield_from_resume_await_kind);
    KosContext_FlushLiveRegsToFrame(ctx);
    KosContext_MarkDeadAfterYield(ctx);
    return EVAL_AWAIT(yielded);
  }

  Py_DECREF(awaitable);
  if (result) {
    yield_from_clear_pending(yf);
    return yield_from_yield_value(ctx, yf, result);
  }

  if (PyErr_ExceptionMatches(PyExc_StopAsyncIteration))
    return yield_from_finish_stop_async(ctx, yf);
  return EVAL_RAISE();
}


/**
  Drive one step of a KosIter provider delegate through its iter
  suite: close, throw(StopAsyncIteration), send, and stepping. The
  suite never suspends -- installed work runs flattened in the same
  koskari_eval and delivers at this node's request record.
*/
static KosOpResult yield_from_drive_kos_iter(
    KosContext *ctx,
    KosDynYieldFrom *yf,
    PyObject *delegate,
    uint8_t action) {

  PyObject *yf_send_val = NULL;
  KosIterSlots *islots;

  if (! KosIter_CheckSlots(delegate, &islots)) {
    PyErr_SetString(PyExc_RuntimeError,
                    "yield-from delegate lost its iter suite");
    return EVAL_RAISE();
  }

  if (action == KOS_YF_THROW
      && yield_from_throw_stop_async_finish(ctx, yf)) {
    return EVAL_NORMAL();
  }

  if (action == KOS_YF_THROW && KosDynYieldFrom_PENDING_ARG(yf)) {
    PyObject *out = NULL;
    PyObject *typ = KosDynYieldFrom_PENDING_ARG_TYPE(yf);
    PyObject *exc = KosDynYieldFrom_PENDING_ARG(yf);
    KosIterStepResult tr;

    Py_XINCREF(typ);
    Py_INCREF(exc);
    yield_from_clear_pending(yf);
    tr = islots->ki_throw(ctx, delegate,
                          typ ? typ : (PyObject *) Py_TYPE(exc), exc, &out);
    Py_XDECREF(typ);
    Py_DECREF(exc);

    switch (tr) {
      case KOS_ITER_INSTALLED:
      case KOS_ITER_INSTALLED_VALUE: {
        kos_dyn_iter_request_set(
            (KosDynNode *) yf, delegate,
            (tr == KOS_ITER_INSTALLED) ? KOS_IREQ_INSTALLED
                                       : KOS_IREQ_GRAFT_VALUE);
        /* A throw the ingress left raised for the installed frame is
           processed by the prologue, never threaded past. */
        if (PyErr_Occurred())
          return EVAL_REPROLOGUE();
        return kos_iter_installed(ctx);
      }
      case KOS_ITER_VALUE:
        return yield_from_yield_value(ctx, yf, out);
      case KOS_ITER_EXHAUSTED:
        Py_XDECREF(out);
        return yield_from_finish(ctx, yf);
      case KOS_ITER_ERROR:
      default:
        return EVAL_RAISE();
    }
  }

  if (action == KOS_YF_SEND && KosDynYieldFrom_PENDING_ARG(yf)) {
    yf_send_val = KosDynYieldFrom_PENDING_ARG(yf);
    Py_INCREF(yf_send_val);
  }

  yield_from_clear_pending(yf);

  {
    PyObject *out = NULL;
    KosIterStepResult sr = islots->ki_step(ctx, delegate, yf_send_val, &out);

    Py_XDECREF(yf_send_val);

    switch (sr) {
      case KOS_ITER_INSTALLED:
      case KOS_ITER_INSTALLED_VALUE:
        kos_dyn_iter_request_set(
            (KosDynNode *) yf, delegate,
            (sr == KOS_ITER_INSTALLED) ? KOS_IREQ_INSTALLED
                                       : KOS_IREQ_GRAFT_VALUE);
        return kos_iter_installed(ctx);

      case KOS_ITER_VALUE:
        /* The delegate produced an element without grafting: re-emit
           it as the delegating generator's own yield. */
        return yield_from_yield_value(ctx, yf, out);

      case KOS_ITER_EXHAUSTED:
        Py_XDECREF(out);
        return yield_from_finish(ctx, yf);

      case KOS_ITER_ERROR:
      default:
        /* The raise walk unwinds the yield-from node and its
           close-delegate hook marks the provider closed. */
        return EVAL_RAISE();
    }
  }
}


/* === The drives as thread steps === */


/*
  The four drive bodies above take the node's state and return the
  result the handler used to relay. These are their KosThreadFn entries:
  each reads its node from dyn_top, drives once, and ends in the relay
  itself, so the handler tail-calls the drive the node elected at push
  and never runs the kind ladder again. insn is the site's record and
  value is unused: the phase resume consumed the delivery into the
  node's pending action before the drive runs.
*/

#define KOS_YF_DRIVE(name, ...)                                         \
  static KOS_NOINLINE KOS_PRESERVE_NONE KosOpResult yf_drive_##name(   \
      KosContext *ctx, void *insn, PyObject *value) {                   \
    KosDynYieldFrom *yf = (KosDynYieldFrom *) KosContext_CurrentNode(ctx); \
    (void) insn;                                                        \
    (void) value;                                                       \
    RESULT_RELAY(ctx, yield_from_drive_##name(                          \
        ctx, yf, KosDynYieldFrom_DELEGATE(yf), __VA_ARGS__              \
        KosDynYieldFrom_PENDING_ACTION(yf)));                           \
  }

KOS_YF_DRIVE(sync_iter, )
KOS_YF_DRIVE(sync_gen, )
KOS_YF_DRIVE(py_async, KosDynYieldFrom_KIND(yf),)
KOS_YF_DRIVE(kos_iter, )

#undef KOS_YF_DRIVE


/**
  The drive a delegate kind elects. Chosen once, when the node is
  pushed, and stored on it.
*/
static KosThreadFn yield_from_drive_for_kind(uint8_t kind) {
  switch (kind) {
    case KOS_YF_SYNC_ITER:
      return yf_drive_sync_iter;
    case KOS_YF_SYNC_GEN:
      return yf_drive_sync_gen;
    case KOS_YF_PY_ASYNC_ITER:
    case KOS_YF_PY_ASYNC_GEN:
      return yf_drive_py_async;
    case KOS_YF_KOS_ITER:
      return yf_drive_kos_iter;
    default:
      return NULL;
  }
}


/* === The phase resumes === */


/*
  One resume per suspension of the delegation, set on the delegating
  frame by the suspension that knows which delivery answers it, and run
  by the prologue's delivery step with what arrived. Each consumes the
  delivery into the node's pending action and tail-calls the drive the
  node elected at push. value is owned; state is unused (the node is
  dyn_top above the frame). A transfer plan's throw injection can reach
  any of them: the plan dropped the staged delivery, so value is NULL
  and the injection is the delivery.
*/

/**
  The node at dyn_top, or NULL with PyErr set when the chain is not
  what a phase resume requires.
*/
static KosDynYieldFrom *yield_from_resume_node(KosContext *ctx) {
  KosDynNode *dyn_top = KosContext_CurrentNode(ctx);

  if (unlikely(! dyn_top || ! KosDynNode_IS_YIELD_FROM(dyn_top))) {
    PyErr_SetString(PyExc_SystemError,
                    "yield-from resume without its node at dyn_top");
    return NULL;
  }
  KosDynYieldFrom_RESUME(dyn_top) = NULL;
  return (KosDynYieldFrom *) dyn_top;
}


/**
  Consume a transfer plan's throw injection into the pending action.
  :return: 1 when a throw was taken, 0 when none was pending
*/
static int yield_from_take_throw(KosContext *ctx, KosDynYieldFrom *yf) {
  PyObject *exc = NULL;
  PyObject *exc_type = NULL;

  if (! KosContext_TakeInjectThrow(ctx, &exc_type, &exc))
    return 0;
  /* ownership of both refs transfers */
  Py_XDECREF(KosDynYieldFrom_PENDING_ARG(yf));
  KosDynYieldFrom_PENDING_ARG(yf) = exc;
  Py_XDECREF(KosDynYieldFrom_PENDING_ARG_TYPE(yf));
  KosDynYieldFrom_PENDING_ARG_TYPE(yf) = exc_type;
  KosDynYieldFrom_PENDING_ACTION(yf) = KOS_YF_THROW;
  return 1;
}


/* The drive the node elected, or NULL with PyErr set. */
static KosThreadFn yield_from_resume_drive(KosDynYieldFrom *yf) {
  KosThreadFn drive = (KosThreadFn) KosDynYieldFrom_DRIVE(yf);

  if (unlikely(! drive))
    PyErr_SetString(PyExc_RuntimeError, "yield-from node has no drive");
  return drive;
}


/**
  The shared front of a send or await resume: the node, a transfer
  plan's throw injection, and otherwise the delivery consumed into the
  pending action. value is owned and is released or transferred here.
  A NULL delivery is a send of None when allowed, else an error.
  :return: the elected drive, or NULL with PyErr set
*/
static KosThreadFn yield_from_resume_take(
    KosContext *ctx,
    PyObject *value,
    uint8_t action,
    int none_when_empty) {

  KosDynYieldFrom *yf = yield_from_resume_node(ctx);

  if (unlikely(! yf)) {
    Py_XDECREF(value);
    return NULL;
  }
  /* A NULL with an error pending is a failure delivered to this frame
     while it waited: the phase declines, and the raise walk goes on
     to the yield-from node. */
  if (! value && PyErr_Occurred())
    return NULL;
  if (yield_from_take_throw(ctx, yf)) {
    Py_XDECREF(value);
    return yield_from_resume_drive(yf);
  }
  if (! value) {
    if (! none_when_empty) {
      PyErr_SetString(PyExc_RuntimeError,
                      "yield-from resume with no delivery");
      return NULL;
    }
    value = Py_None;
    Py_INCREF(value);
  }
  Py_XDECREF(KosDynYieldFrom_PENDING_ARG(yf));
  KosDynYieldFrom_PENDING_ARG(yf) = value;
  KosDynYieldFrom_PENDING_ACTION(yf) = action;
  return yield_from_resume_drive(yf);
}


/**
  After an emitted yield: value is what the consumer sent, or NULL when
  it only stepped, which is a send of None.
*/
static KOS_NOINLINE KOS_PRESERVE_NONE KosOpResult yield_from_resume_send(
    KosContext *ctx,
    void *state,
    PyObject *value) {

  KosThreadFn drive = yield_from_resume_take(ctx, value, KOS_YF_SEND, 1);
  (void) state;

  if (unlikely(! drive))
    return EVAL_RAISE();
  TAILCALL(drive, ctx, NULL, NULL);
}


/**
  After an await on a Python async delegate: value is the awaited
  result, which the drive takes as the delegate's next value.
*/
static KOS_NOINLINE KOS_PRESERVE_NONE KosOpResult yield_from_resume_await(
    KosContext *ctx,
    void *state,
    PyObject *value) {

  KosThreadFn drive = yield_from_resume_take(ctx, value, KOS_YF_STEP, 0);
  (void) state;

  if (unlikely(! drive))
    return EVAL_RAISE();
  TAILCALL(drive, ctx, NULL, NULL);
}


/**
  After a flattened inner delegate handed up a value at the iteration
  node: re-emit it as this generator's own yield; the send that answers
  it arrives at yield_from_resume_send.
*/
static KOS_NOINLINE KOS_PRESERVE_NONE KosOpResult yield_from_resume_emit(
    KosContext *ctx,
    void *state,
    PyObject *value) {

  KosDynYieldFrom *yf = yield_from_resume_node(ctx);
  (void) state;

  if (unlikely(! yf)) {
    Py_XDECREF(value);
    return EVAL_RAISE();
  }
  /* a failure delivered while the frame waited: decline */
  if (! value && PyErr_Occurred())
    return EVAL_RAISE();
  if (yield_from_take_throw(ctx, yf)) {
    KosThreadFn drive = yield_from_resume_drive(yf);
    Py_XDECREF(value);
    if (unlikely(! drive))
      return EVAL_RAISE();
    TAILCALL(drive, ctx, NULL, NULL);
  }
  if (unlikely(! value)) {
    PyErr_SetString(PyExc_SystemError, "yield-from emit with no value");
    return EVAL_RAISE();
  }
  RESULT_RELAY(ctx, yield_from_yield_value(ctx, yf, value));
}


/**
  YIELD_FROM: delegate to a sub-iterator or sub-generator. Runs once
  per delegation: TOS is the yield-from expression; normalize it to
  (delegate, kind), push the yield_from node with the drive its kind
  elects, and tail-call the drive for the first step. Every later
  delivery to this frame while the delegation is in flight runs the
  phase resume its suspension set, never this handler: the pc stays
  past the instruction, and the drive continues there when the delegate
  is exhausted.
*/
KOS_OP_HANDLER(YIELD_FROM) {
  KOS_SYNC_PC(ctx, insn);
  KosDynNode *dyn_top = KosContext_CurrentNode(ctx);
  PyObject *subexpr;
  PyObject *delegate = NULL;
  uint8_t kind = KOS_YF_SYNC_ITER; /* overwritten by yield_from_normalize */

  if (unlikely(dyn_top && KosDynNode_IS_YIELD_FROM(dyn_top))) {
    PyErr_SetString(PyExc_SystemError,
                    "YIELD_FROM entered with its node still on the chain");
    RESULT_RAISE();
  }

  subexpr = KosContext_StackPopRef(ctx);
  if (! subexpr)
    RESULT_RAISE();

  if (yield_from_normalize(subexpr, &delegate, &kind) < 0) {
    Py_DECREF(subexpr);
    RESULT_RAISE();
  }
  Py_DECREF(subexpr);

  if (! kos_dyn_push_yield_from(ctx, delegate, kind)) {
    Py_DECREF(delegate);
    RESULT_RAISE();
  }
  Py_DECREF(delegate);

  {
    KosDynYieldFrom *yf = (KosDynYieldFrom *) KosContext_CurrentNode(ctx);
    KosThreadFn drive = yield_from_drive_for_kind(kind);

    KosDynYieldFrom_DRIVE(yf) = (void *) drive;
    KosDynYieldFrom_RESUME_EMIT(yf) = (void *) &yield_from_resume_emit_kind;
    /* ctx->pc already sits past this instruction, and acting_pc is it */
    TAILCALL(drive, ctx, insn, NULL);
  }
}


// The end.
