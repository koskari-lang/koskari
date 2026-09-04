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
     has asend/athrow/aclose. */
  if (PyObject_HasAttrString(obj, "__anext__"))
    return PyObject_HasAttrString(obj, "asend")
        ? KOS_YF_PY_ASYNC_GEN
        : KOS_YF_PY_ASYNC_ITER;

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


/**
  Arm the node's phase resume on the delegating frame: the next
  delivery to this frame (a sent value, an awaited result, a flattened
  inner yield) runs the resume instead of an instruction, and the pc
  stays past the YIELD_FROM. Every suspension of the delegation ends in
  this; the node's pop and unwind disarm it.
*/
static void yield_from_arm(KosEvalState *es, KosDynYieldFrom *yf) {
  KosDynFrame *frame = KosEvalState_FRAME(es);

  KosDynFrame_RESUME_FN(frame) = KosDynYieldFrom_RESUME(yf);
  KosDynFrame_RESUME_STATE(frame) = NULL;
  KosDynFrame_RESUME_NAME(frame) = "yield-from";
}


/**
  Emit payload (owned, consumed) as this generator's own yield: arm the
  phase resume for the send that answers it, set EXPECT_SEND, and return
  EVAL_YIELD. Nothing is left on the operand stack.
*/
static KosOpResult yield_from_emit_yield(
    KosEvalState *es,
    KosDynYieldFrom *yf,
    PyObject *payload) {

  /*
    No demand deny here: this is a yield-shaped suspension, and demand
    generators yield as iteration (the demand bit denies await only).
    A demand-flagged frame can only reach YIELD_FROM as a demand-driven
    generator; the await-shaped suspension below keeps its deny.
  */

  KosDynYieldFrom_PHASE_SET(yf, KOS_YF_PHASE_EXPECT_SEND);
  yield_from_arm(es, yf);
  SAYF("[yf] emit_yield dyn_top=%p frame=%p payload=%p -> KOS_RES_YIELD\n",
       (void *) KosEvalState_CurrentNode(es), (void *) KosEvalState_FRAME(es),
       (void *) payload);

  return EVAL_YIELD(payload);
}


/**
  Yield value (a new ref, consumed) out of the delegating generator as
  this generator's own yield.
*/
static KOS_NOINLINE KosOpResult yield_from_yield_value(
    KosEvalState *es,
    KosDynYieldFrom *yf,
    PyObject *value) {

  return yield_from_emit_yield(es, yf, value);
}


/**
  Delegate exhausted: pop the yield-from node and continue normally
  (Koskari yield-from is a void completion).
*/
static KOS_NOINLINE KosOpResult yield_from_finish(
    KosEvalState *es,
    KosDynYieldFrom *yf) {

  if (unlikely(kos_dyn_pop_yield_from(es, yf) < 0))
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
  exception, discard its value, pop the yield-from node.
*/
static KOS_NOINLINE KosOpResult yield_from_finish_stop_async(
    KosEvalState *es,
    KosDynYieldFrom *yf) {

  PyObject *exc = PyErr_GetRaisedException();
  PyObject *final_val = StopAsyncIteration_VALUE(exc);
  Py_XINCREF(final_val);
  Py_DECREF(exc);

  kos_dyn_pop_yield_from(es, yf);
  Py_XDECREF(final_val);

  return EVAL_NORMAL();
}


/**
  THROW(StopAsyncIteration) closes the delegation quietly: when the
  pending inject is a StopAsyncIteration instance, discard it and its
  value, pop the yield-from node.

  :return: 1 when handled (node popped), 0 when not applicable
*/
static KOS_NOINLINE int yield_from_throw_stop_async_finish(
    KosEvalState *es,
    KosDynYieldFrom *yf) {

  PyObject *exc = KosDynYieldFrom_PENDING_ARG(yf);

  if (! exc || ! PyObject_IsInstance(exc, (PyObject *) PyExc_StopAsyncIteration))
    return 0;

  {
    PyObject *final_val = StopAsyncIteration_VALUE(exc);
    Py_XINCREF(final_val);
    yield_from_clear_pending(yf);
    kos_dyn_pop_yield_from(es, yf);
    Py_XDECREF(final_val);
  }
  return 1;
}


/**
  Drive one step of a sync-iterator delegate (PyIter_Next). On a yielded
  value returns EVAL_YIELD; on exhaustion returns EVAL_NORMAL; on error
  returns EVAL_RAISE.
*/
static KosOpResult yield_from_drive_sync_iter(
    KosEvalState *es,
    KosDynYieldFrom *yf,
    PyObject *delegate,
    uint8_t action,
    KosOp *insn) {

  (void) insn;

  if (action != KOS_YF_STEP && action != KOS_YF_SEND) {
    if (action == KOS_YF_CLOSE)
      ;  /* no-op for sync iter */
    if (action == KOS_YF_THROW)
      yield_from_clear_pending(yf);
  }

  if (action == KOS_YF_SEND && KosDynYieldFrom_PENDING_ARG(yf) != Py_None) {
    PyErr_SetString(PyExc_TypeError,
                    "send() to sync iterator only accepts None");
    return EVAL_RAISE();
  }

  PyObject *n = PyIter_Next(delegate);
  if (n)
    return yield_from_yield_value(es, yf, n);

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

  return yield_from_finish(es, yf);
}


/**
  Drive one step of a sync-generator delegate (send/throw/close).
  Check exhaustion (StopIteration) before treating non-NULL send result
  as a yielded value.
*/
static KosOpResult yield_from_drive_sync_gen(
    KosEvalState *es,
    KosDynYieldFrom *yf,
    PyObject *delegate,
    uint8_t action,
    KosOp *insn) {

  (void) insn;

  PyObject *send_val = Py_None;
  if (action == KOS_YF_SEND && KosDynYieldFrom_PENDING_ARG(yf))
    send_val = KosDynYieldFrom_PENDING_ARG(yf);

  if (action == KOS_YF_THROW) {
    PyObject *exc = KosDynYieldFrom_PENDING_ARG(yf);
    if (exc) {
      PyObject *res = PyObject_CallMethodObjArgs(delegate, str_throw,
          exc, Py_None, Py_None, NULL);
      if (res) {
        yield_from_clear_pending(yf);
        return yield_from_yield_value(es, yf, res);
      }
    }
    kos_yieldfrom_close_delegate(yf);
    if (kos_dyn_pop_yield_from(es, yf) < 0)
      return EVAL_RAISE();
    return EVAL_RAISE();
  }

  if (action == KOS_YF_CLOSE) {
    PyObject *res = PyObject_CallMethodObjArgs(delegate, str_close, NULL);
    Py_XDECREF(res);
    PyErr_Clear();
    kos_dyn_pop_yield_from(es, yf);
    PyErr_SetNone(PyExc_GeneratorExit);
    return EVAL_RAISE();
  }

  {
    PyObject *res = PyObject_CallMethodObjArgs(delegate, str_send, send_val, NULL);
    if (PyErr_ExceptionMatches(PyExc_StopIteration)) {
      Py_XDECREF(res);
      yield_from_discard_stop_iteration_value();
      return yield_from_finish(es, yf);
    }

    if (res)
      return yield_from_yield_value(es, yf, res);
    return EVAL_RAISE();
  }
}


/**
  Drive one step of a Python async-iterator or async-generator
  (__anext__ / asend / athrow / aclose). kind is KOS_YF_PY_ASYNC_ITER
  or KOS_YF_PY_ASYNC_GEN.
*/
static KosOpResult yield_from_drive_py_async(
    KosEvalState *es,
    KosDynYieldFrom *yf,
    PyObject *delegate,
    uint8_t kind,
    uint8_t action,
    KosOp *insn) {

  (void) insn;

  /* Resuming from await: we had suspended with INFLIGHT set; the
     result was stashed in PENDING_ARG. Push it and emit YIELD. */
  if (KosDynYieldFrom_INFLIGHT(yf)) {
    PyObject *aw_result = yield_from_take_await_result(yf);
    if (! aw_result)
      return EVAL_RAISE();
    return yield_from_yield_value(es, yf, aw_result);
  }

  /* Throw(StopAsyncIteration): delegate was closed with that; discard value. */
  if (action == KOS_YF_THROW
      && yield_from_throw_stop_async_finish(es, yf))
    return EVAL_NORMAL();

  /* Build the awaitable for this step: __anext__, asend, athrow, or aclose. */
  PyObject *awaitable = NULL;
  if (action == KOS_YF_STEP || (action == KOS_YF_SEND && ! KosDynYieldFrom_PENDING_ARG(yf))) {
    awaitable = get_await_next(delegate);

  } else if (kind == KOS_YF_PY_ASYNC_GEN && action == KOS_YF_SEND) {
    PyObject *send_val = KosDynYieldFrom_PENDING_ARG(yf);
    PyObject *tmp = PyObject_CallMethodObjArgs(delegate, str_asend, send_val, NULL);
    if (tmp) {
      awaitable = PyObject_GetAwaitable(tmp);
      Py_DECREF(tmp);
    }

  } else if (kind == KOS_YF_PY_ASYNC_GEN && action == KOS_YF_THROW) {
    PyObject *exc = KosDynYieldFrom_PENDING_ARG(yf);
    if (exc) {
      PyObject *typ = (PyObject *) Py_TYPE(exc);
      PyObject *tmp = PyObject_CallMethodObjArgs(delegate, str_athrow, typ, exc,
                                                 Py_None, NULL);
      if (tmp) {
        awaitable = PyObject_GetAwaitable(tmp);
        Py_DECREF(tmp);
      }
    }

  } else if (kind == KOS_YF_PY_ASYNC_GEN && action == KOS_YF_CLOSE) {
    PyObject *tmp = PyObject_CallMethodObjArgs(delegate, str_aclose, NULL);
    if (tmp) {
      awaitable = PyObject_GetAwaitable(tmp);
      Py_DECREF(tmp);
    }

  } else if (action == KOS_YF_CLOSE) {
    /* PY_ASYNC_ITER has no aclose; treat as exhausted. */
    return yield_from_finish(es, yf);
  }

  if (! awaitable) {
    if (PyErr_ExceptionMatches(PyExc_StopAsyncIteration))
      return yield_from_finish_stop_async(es, yf);
    return EVAL_RAISE();
  }

  int suspended = 0;
  PyObject *yielded = NULL;
  PyObject *result = KosEvalState_EagerStepAwaitable(
      es, awaitable, &suspended, &yielded);

  if (suspended && yielded) {
    if (kos_eval_demand_deny_suspend(es,
        "async suspension is not allowed in demand_sync")) {
      Py_DECREF(awaitable);
      Py_DECREF(yielded);
      return EVAL_RAISE();
    }
    KosDynYieldFrom_INFLIGHT(yf) = awaitable;
    KosDynYieldFrom_PHASE_SET(yf, KOS_YF_PHASE_EXPECT_AWAIT);
    yield_from_arm(es, yf);
    KosEvalState_FlushLiveRegsToFrame(es);
    KosEvalState_MarkDeadAfterYield(es);
    return EVAL_AWAIT(yielded);
  }

  Py_DECREF(awaitable);
  if (result) {
    yield_from_clear_pending(yf);
    return yield_from_yield_value(es, yf, result);
  }

  if (PyErr_ExceptionMatches(PyExc_StopAsyncIteration))
    return yield_from_finish_stop_async(es, yf);
  return EVAL_RAISE();
}


/**
  Drive one step of a KosIter provider delegate through its iter
  suite: close, throw(StopAsyncIteration), send, and stepping. The
  suite never parks -- installed work runs flattened in the same
  koskari_eval and delivers at this node's request record.
*/
static KosOpResult yield_from_drive_kos_iter(
    KosEvalState *es,
    KosDynYieldFrom *yf,
    PyObject *delegate,
    KosIterSlots *islots,
    uint8_t action,
    KosOp *insn) {

  PyObject *yf_send_val = NULL;

  (void) insn;

  if (action == KOS_YF_CLOSE) {
    islots->ki_finish(delegate, 1);
    kos_dyn_pop_yield_from(es, yf);
    PyErr_SetNone(PyExc_GeneratorExit);
    return EVAL_RAISE();
  }

  if (action == KOS_YF_THROW
      && yield_from_throw_stop_async_finish(es, yf)) {
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
    tr = islots->ki_throw(es, delegate,
                          typ ? typ : (PyObject *) Py_TYPE(exc), exc, &out);
    Py_XDECREF(typ);
    Py_DECREF(exc);

    switch (tr) {
      case KOS_ITER_INSTALLED:
      case KOS_ITER_INSTALLED_VALUE: {
        kos_dyn_iter_request_arm(
            (KosDynNode *) yf, delegate,
            (tr == KOS_ITER_INSTALLED) ? KOS_IREQ_INSTALLED
                                       : KOS_IREQ_GRAFT_VALUE);
        if (kos_frame_threads(KosEvalState_FRAME(es)))
          return EVAL_NORMAL();
        return EVAL_REPROLOGUE();
      }
      case KOS_ITER_VALUE:
        return yield_from_yield_value(es, yf, out);
      case KOS_ITER_EXHAUSTED:
        Py_XDECREF(out);
        return yield_from_finish(es, yf);
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
    KosIterStepResult sr = islots->ki_step(es, delegate, yf_send_val, &out);

    Py_XDECREF(yf_send_val);

    switch (sr) {
      case KOS_ITER_INSTALLED:
        kos_dyn_iter_request_arm((KosDynNode *) yf, delegate,
                                 KOS_IREQ_INSTALLED);
        if (kos_frame_threads(KosEvalState_FRAME(es)))
          return EVAL_NORMAL();
        return EVAL_REPROLOGUE();

      case KOS_ITER_INSTALLED_VALUE: {
        /* A frame with bytecode continues dispatch directly; only a
           code-less native frame takes the entry sequence. */
        kos_dyn_iter_request_arm((KosDynNode *) yf, delegate,
                                 KOS_IREQ_GRAFT_VALUE);
        if (kos_frame_threads(KosEvalState_FRAME(es)))
          return EVAL_NORMAL();
        return EVAL_REPROLOGUE();
      }

      case KOS_ITER_VALUE:
        /* The delegate produced an element without grafting: re-emit
           it as the delegating generator's own yield. */
        return yield_from_yield_value(es, yf, out);

      case KOS_ITER_EXHAUSTED:
        Py_XDECREF(out);
        return yield_from_finish(es, yf);

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
  value is unused until the phases become resumes.
*/

#define KOS_YF_DRIVE(name)                                              \
  static KOS_NOINLINE KOS_PRESERVE_NONE KosOpResult yf_drive_##name(   \
      KosEvalState *es, void *insn, PyObject *value)

KOS_YF_DRIVE(sync_iter) {
  KosDynYieldFrom *yf = (KosDynYieldFrom *) KosEvalState_CurrentNode(es);
  (void) value;
  RESULT_RELAY(es, yield_from_drive_sync_iter(
      es, yf, KosDynYieldFrom_DELEGATE(yf),
      KosDynYieldFrom_PENDING_ACTION(yf), (KosOp *) insn));
}

KOS_YF_DRIVE(sync_gen) {
  KosDynYieldFrom *yf = (KosDynYieldFrom *) KosEvalState_CurrentNode(es);
  (void) value;
  RESULT_RELAY(es, yield_from_drive_sync_gen(
      es, yf, KosDynYieldFrom_DELEGATE(yf),
      KosDynYieldFrom_PENDING_ACTION(yf), (KosOp *) insn));
}

KOS_YF_DRIVE(py_async) {
  KosDynYieldFrom *yf = (KosDynYieldFrom *) KosEvalState_CurrentNode(es);
  (void) value;
  RESULT_RELAY(es, yield_from_drive_py_async(
      es, yf, KosDynYieldFrom_DELEGATE(yf), KosDynYieldFrom_KIND(yf),
      KosDynYieldFrom_PENDING_ACTION(yf), (KosOp *) insn));
}

KOS_YF_DRIVE(kos_iter) {
  KosDynYieldFrom *yf = (KosDynYieldFrom *) KosEvalState_CurrentNode(es);
  PyObject *delegate = KosDynYieldFrom_DELEGATE(yf);
  KosIterSlots *islots;
  (void) value;

  if (! KosIter_CheckSlots(delegate, &islots)) {
    PyErr_SetString(PyExc_RuntimeError,
                    "yield-from delegate lost its iter suite");
    RESULT_RAISE();
  }
  RESULT_RELAY(es, yield_from_drive_kos_iter(
      es, yf, delegate, islots, KosDynYieldFrom_PENDING_ACTION(yf),
      (KosOp *) insn));
}

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


/**
  The phase resume. Armed on the delegating frame at every suspension
  of the delegation and run by the prologue's delivery step with what
  arrived: the sent value after an emitted yield, the awaited result
  after an await, the inner value a flattened delegate handed up, or
  NULL when a transfer plan's throw injection is the delivery. Consumes
  the delivery into the node's pending action and tail-calls the drive
  the node elected at push. value is owned; state is unused (the node
  is dyn_top above the frame).
*/
static KOS_NOINLINE KOS_PRESERVE_NONE KosOpResult yield_from_resume(
    KosEvalState *es,
    void *state,
    PyObject *value) {

  KosDynNode *dyn_top = KosEvalState_CurrentNode(es);
  KosDynYieldFrom *yf;
  KosThreadFn drive;
  (void) state;

  if (unlikely(! dyn_top || ! KosDynNode_IS_YIELD_FROM(dyn_top))) {
    Py_XDECREF(value);
    PyErr_SetString(PyExc_SystemError,
                    "yield-from resume without its node at dyn_top");
    return EVAL_RAISE();
  }
  yf = (KosDynYieldFrom *) dyn_top;

  /* EMIT: a flattened koskari-gen delegate handed us a value at the
     iteration node. Re-emit it as this generator's own yield; the send
     that answers it returns here in EXPECT_SEND. */
  if (KosDynYieldFrom_PHASE(yf) == KOS_YF_PHASE_EMIT) {
    if (unlikely(! value)) {
      PyErr_SetString(PyExc_SystemError, "yield-from emit with no value");
      return EVAL_RAISE();
    }
    RESULT_RELAY(es, yield_from_yield_value(es, yf, value));
  }

  {
    PyObject *exc = NULL;
    PyObject *exc_type = NULL;

    if (KosEvalState_TakeInjectThrow(es, &exc_type, &exc)) {
      /* a transfer plan's throw: ownership of both refs transfers */
      Py_XDECREF(value);
      Py_XDECREF(KosDynYieldFrom_PENDING_ARG(yf));
      KosDynYieldFrom_PENDING_ARG(yf) = exc;
      Py_XDECREF(KosDynYieldFrom_PENDING_ARG_TYPE(yf));
      KosDynYieldFrom_PENDING_ARG_TYPE(yf) = exc_type;
      KosDynYieldFrom_PENDING_ACTION(yf) = KOS_YF_THROW;

    } else if (KosDynYieldFrom_PHASE(yf) == KOS_YF_PHASE_EXPECT_SEND) {
      /* the value sent to the delegating generator; None when the
         consumer only stepped */
      if (! value) {
        value = Py_None;
        Py_INCREF(value);
      }
      Py_XDECREF(KosDynYieldFrom_PENDING_ARG(yf));
      KosDynYieldFrom_PENDING_ARG(yf) = value;
      KosDynYieldFrom_PENDING_ACTION(yf) = KOS_YF_SEND;

    } else if (KosDynYieldFrom_PHASE(yf) == KOS_YF_PHASE_EXPECT_AWAIT) {
      if (unlikely(! value)) {
        PyErr_SetString(PyExc_RuntimeError,
                        "yield-from resume with no await result");
        return EVAL_RAISE();
      }
      Py_XDECREF(KosDynYieldFrom_PENDING_ARG(yf));
      KosDynYieldFrom_PENDING_ARG(yf) = value;
      KosDynYieldFrom_PENDING_ACTION(yf) = KOS_YF_STEP;

    } else {
      /* DRIVING with nothing expected: the pending action stands */
      Py_XDECREF(value);
    }
  }
  KosDynYieldFrom_PHASE_SET(yf, KOS_YF_PHASE_DRIVING);

  drive = (KosThreadFn) KosDynYieldFrom_DRIVE(yf);
  if (unlikely(! drive)) {
    PyErr_SetString(PyExc_RuntimeError, "yield-from node has no drive");
    return EVAL_RAISE();
  }
  TAILCALL(drive, es, NULL, NULL);
}


/**
  YIELD_FROM: delegate to a sub-iterator or sub-generator. Runs once
  per delegation: TOS is the yield-from expression; normalize it to
  (delegate, kind), push the yield_from node with the drive its kind
  elects and the phase resume, and tail-call the drive for the first
  step. Every later delivery to this frame while the delegation is in
  flight runs the resume, never this handler: the pc stays past the
  instruction, and the drive continues there when the delegate is
  exhausted.
*/
KOS_OP_HANDLER_COLD(YIELD_FROM) {
  KosDynNode *dyn_top = KosEvalState_CurrentNode(es);
  PyObject *subexpr;
  PyObject *delegate = NULL;
  uint8_t kind = KOS_YF_SYNC_ITER; /* overwritten by yield_from_normalize */

  if (unlikely(dyn_top && KosDynNode_IS_YIELD_FROM(dyn_top))) {
    PyErr_SetString(PyExc_SystemError,
                    "YIELD_FROM entered with its node still on the chain");
    RESULT_RAISE();
  }

  subexpr = KosEvalState_StackPopRef(es);
  if (! subexpr)
    RESULT_RAISE();

  if (yield_from_normalize(subexpr, &delegate, &kind) < 0) {
    Py_DECREF(subexpr);
    RESULT_RAISE();
  }
  Py_DECREF(subexpr);

  if (! kos_dyn_push_yield_from(es, delegate, kind)) {
    Py_DECREF(delegate);
    RESULT_RAISE();
  }
  Py_DECREF(delegate);

  {
    KosDynYieldFrom *yf = (KosDynYieldFrom *) KosEvalState_CurrentNode(es);
    KosThreadFn drive = yield_from_drive_for_kind(kind);

    KosDynYieldFrom_PENDING_ACTION(yf) = KOS_YF_STEP;
    KosDynYieldFrom_PHASE_SET(yf, KOS_YF_PHASE_DRIVING);
    KosDynYieldFrom_DRIVE(yf) = (void *) drive;
    KosDynYieldFrom_RESUME(yf) = (void *) yield_from_resume;
    /* es->pc already sits past this instruction, and acting_pc is it */
    TAILCALL(drive, es, insn, NULL);
  }
}


// The end.
