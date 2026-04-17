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
  if (KosCoroutine_Check(obj)
#ifdef PyCoro_CheckExact
      || PyCoro_CheckExact(obj)
#endif
      ) {
    PyErr_SetString(PyExc_TypeError,
                    "coroutine object is not async iterable");
    return -1;
  }

  /* --- Koskari async gen ---
     Flattened into the same eval loop (see yield_from_drive_kos_async_gen). */
  if (KosPyAsyncGen_Check(obj)) {
    Py_INCREF(obj);
    *delegate_out = obj;
    *kind_out = KOS_YF_KOS_ASYNC_GEN;
    return 0;
  }

  /* --- Python sync generator ---
     Has send/throw/close; we use send(None) for step, send(val) for send. */
  if (PyGen_Check(obj)) {
    Py_INCREF(obj);
    *delegate_out = obj;
    *kind_out = KOS_YF_SYNC_GEN;
    return 0;
  }

  /* --- Python async iterator / async gen ---
     Has __anext__; async gen also has asend/athrow/aclose. We use
     __anext__ for step and asend/athrow/aclose when needed. */
  if (PyObject_HasAttrString(obj, "__anext__")) {
    Py_INCREF(obj);
    *delegate_out = obj;
    *kind_out = PyObject_HasAttrString(obj, "asend")
        ? KOS_YF_PY_ASYNC_GEN
        : KOS_YF_PY_ASYNC_ITER;
    return 0;
  }

  /* --- Sync iterator ---
     PyIter_Next for step; no send/close. */
  if (PyIter_Check(obj)) {
    Py_INCREF(obj);
    *delegate_out = obj;
    *kind_out = KOS_YF_SYNC_ITER;
    return 0;
  }

  /* --- Callable (e.g. generator function) ---
     Call once to get the actual iterator/gen; then classify as above.
     Reject if the call returns a coroutine. */
  if (PyCallable_Check(obj)) {
    PyObject *gen = PyObject_CallFunctionObjArgs(obj, NULL);
    if (! gen)
      return -1;

    if (KosCoroutine_Check(gen)
#ifdef PyCoro_CheckExact
        || PyCoro_CheckExact(gen)
#endif
        ) {
      Py_DECREF(gen);
      PyErr_SetString(PyExc_TypeError,
                      "coroutine object is not async iterable");
      return -1;
    }

    if (PyGen_Check(gen)) {
      *delegate_out = gen;
      *kind_out = KOS_YF_SYNC_GEN;
      return 0;
    }

    if (PyObject_HasAttrString(gen, "__anext__")) {
      *delegate_out = gen;
      *kind_out = PyObject_HasAttrString(gen, "asend")
          ? KOS_YF_PY_ASYNC_GEN
          : KOS_YF_PY_ASYNC_ITER;
      return 0;
    }

    if (KosPyAsyncGen_Check(gen)) {
      *delegate_out = gen;
      *kind_out = KOS_YF_KOS_ASYNC_GEN;
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
   Set frame PC back to the YIELD_FROM instruction so the next loop
   iteration re-dispatches to YIELD_FROM (used when suspending).  Records
   rollback_pc on the yield-from node for flattened Koskari async gen
   boundary delivery.
*/
static void yield_from_rollback_pc(
    KosContext *ctx, KosOp *insn, KosDynYieldFrom *yf) {

  KosDynFrame *frame = CURRENT_FRAME(ctx);
  KosCode *code = KosDynFrame_CODE(frame);
  uint32_t pc = (uint32_t)(insn - KosCode_CODE(code));

  KosDynFrame_PC(frame) = pc;
  if (yf)
    KosDynYieldFrom_ROLLBACK_PC(yf) = pc;
}


/**
   Emit KOS_EV_YIELD using the value at TOS. Call after pushing the
   yielded value onto the frame stack. Requires at least one value on
   the frame stack (TOS). On success sets phase to EXPECT_SEND, DEAD, and
   returns 1. On failure (no TOS or tail read error) returns -1 with an
   exception set; does not change phase or PC.
*/
static int yield_from_emit_yield(
    KosContext *ctx,
    KosDynFrame *frame,
    KosDynYieldFrom *yf,
    KosEvent *out_ev,
    KosOp *insn) {

  PyObject *const *tos = KosStackBlock_TailPtr(
      KosDynFrame_STACK(frame), 1);
  if (! tos) {
    if (! PyErr_Occurred())
      PyErr_SetString(PyExc_RuntimeError,
                      "yield-from emit_yield: missing TOS after push");
    return -1;
  }

  KosDynYieldFrom_PHASE_SET(yf, KOS_YF_PHASE_EXPECT_SEND);
  yield_from_rollback_pc(ctx, insn, yf);
  KosEvent_KIND(out_ev) = KOS_EV_YIELD;
  KosEvent_PAYLOAD(out_ev) = *tos;
  Py_INCREF(KosEvent_PAYLOAD(out_ev));
  SAYF("[yf] emit_yield dyn_top=%p frame=%p payload=%p -> KOS_EV_YIELD\n",
       (void *) KosContext_DYN_TOP(ctx), (void *) frame,
       (void *) KosEvent_PAYLOAD(out_ev));

  KosContext_FLAGS(ctx) &= ~KOS_CTX_RUNNING;
  KosContext_FLAGS(ctx) |= KOS_CTX_DEAD;
  return 1;
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
   Drive one step of a sync-iterator delegate (PyIter_Next). Returns
   0 continue, 1 event (YIELD), -1 error. Exhaustion completes with
   Exhaustion: discard StopIteration value, pop yield_from, no stack push.
*/
static int yield_from_drive_sync_iter(
    KosContext *ctx,
    KosDynFrame *frame,
    KosDynYieldFrom *yf,
    PyObject *delegate,
    uint8_t action,
    KosEvent *out_ev,
    KosOp *insn) {

  if (action != KOS_YF_STEP && action != KOS_YF_SEND) {
    if (action == KOS_YF_CLOSE)
      ;  /* no-op for sync iter */
    if (action == KOS_YF_THROW) {
      Py_XDECREF(KosDynYieldFrom_PENDING_ARG(yf));
      KosDynYieldFrom_PENDING_ARG(yf) = NULL;
      KosDynYieldFrom_PENDING_ACTION(yf) = KOS_YF_STEP;
    }
  }

  if (action == KOS_YF_SEND && KosDynYieldFrom_PENDING_ARG(yf) != Py_None) {
    PyErr_SetString(PyExc_TypeError,
                    "send() to sync iterator only accepts None");
    return -1;
  }

  PyObject *n = PyIter_Next(delegate);
  if (n) {
    if (KosStackBlock_PushRef(&KosDynFrame_STACK(frame), n) < 0) {
      Py_DECREF(n);
      return -1;
    }
    Py_DECREF(n);
    return yield_from_emit_yield(ctx, frame, yf, out_ev, insn);
  }

  /* PyIter_Next returns NULL with no exception set on normal exhaustion.
     StopIteration (with optional .value) is only set for some iterators /
     gen.send paths; when present, discard the value per yield-from rules. */
  if (PyErr_Occurred()) {
    if (! PyErr_ExceptionMatches(PyExc_StopIteration))
      return -1;
    yield_from_discard_stop_iteration_value();
    kos_dyn_pop_yield_from(ctx, yf);
    return 0;
  }

  kos_dyn_pop_yield_from(ctx, yf);
  return 0;
}


/**
   Drive one step of a sync-generator delegate (send/throw/close).
   Check exhaustion (StopIteration) before treating non-NULL send result
   as a yielded value. Returns 0 continue, 1 event, -1 error.
*/
static int yield_from_drive_sync_gen(
    KosContext *ctx,
    KosDynFrame *frame,
    KosDynYieldFrom *yf,
    PyObject *delegate,
    uint8_t action,
    KosEvent *out_ev,
    KosOp *insn) {

  PyObject *send_val = Py_None;
  if (action == KOS_YF_SEND && KosDynYieldFrom_PENDING_ARG(yf))
    send_val = KosDynYieldFrom_PENDING_ARG(yf);

  if (action == KOS_YF_THROW) {
    PyObject *exc = KosDynYieldFrom_PENDING_ARG(yf);
    if (exc) {
      PyObject *res = PyObject_CallMethodObjArgs(delegate, str_throw,
          exc, Py_None, Py_None, NULL);
      if (res) {
        KosDynYieldFrom_PENDING_ACTION(yf) = KOS_YF_STEP;
        Py_XDECREF(KosDynYieldFrom_PENDING_ARG(yf));
        KosDynYieldFrom_PENDING_ARG(yf) = NULL;
        if (KosStackBlock_PushRef(&KosDynFrame_STACK(frame), res) < 0) {
          Py_DECREF(res);
          return -1;
        }
        Py_DECREF(res);
        return yield_from_emit_yield(ctx, frame, yf, out_ev, insn);
      }
    }
    kos_yieldfrom_close_delegate(yf);
    kos_dyn_pop_yield_from(ctx, yf);
    return -1;
  }

  if (action == KOS_YF_CLOSE) {
    PyObject *res = PyObject_CallMethodObjArgs(delegate, str_close, NULL);
    Py_XDECREF(res);
    PyErr_Clear();
    kos_dyn_pop_yield_from(ctx, yf);
    PyErr_SetNone(PyExc_GeneratorExit);
    return -1;
  }

  {
    PyObject *res = PyObject_CallMethodObjArgs(delegate, str_send, send_val, NULL);
    if (PyErr_ExceptionMatches(PyExc_StopIteration)) {
      Py_XDECREF(res);
      yield_from_discard_stop_iteration_value();
      kos_dyn_pop_yield_from(ctx, yf);
      return 0;
    }

    if (res) {
      if (KosStackBlock_PushRef(&KosDynFrame_STACK(frame), res) < 0) {
        Py_DECREF(res);
        return -1;
      }
      Py_DECREF(res);
      return yield_from_emit_yield(ctx, frame, yf, out_ev, insn);
    }
    return -1;
  }
}


/**
   Drive one step of a Python async-iterator or async-generator
   (__anext__ / asend / athrow / aclose). kind is KOS_YF_PY_ASYNC_ITER
   or KOS_YF_PY_ASYNC_GEN. Returns 0 continue, 1 event (YIELD/AWAIT), -1 error.
*/
static int yield_from_drive_py_async(
    KosContext *ctx,
    KosDynFrame *frame,
    KosDynYieldFrom *yf,
    PyObject *delegate,
    uint8_t kind,
    uint8_t action,
    KosEvent *out_ev,
    KosOp *insn) {

  /* Resuming from await: we had suspended with INFLIGHT set; the
     result was stashed in PENDING_ARG. Push it and emit YIELD. */
  if (KosDynYieldFrom_INFLIGHT(yf)) {
    PyObject *aw_result = KosDynYieldFrom_PENDING_ARG(yf);
    Py_XDECREF(KosDynYieldFrom_INFLIGHT(yf));
    KosDynYieldFrom_INFLIGHT(yf) = NULL;
    KosDynYieldFrom_PENDING_ARG(yf) = NULL;
    KosDynYieldFrom_PENDING_ACTION(yf) = KOS_YF_STEP;

    if (! aw_result) {
      PyErr_SetString(PyExc_RuntimeError,
                      "yield-from resume with no await result");
      return -1;
    }

    if (KosStackBlock_PushRef(&KosDynFrame_STACK(frame), aw_result) < 0) {
      Py_DECREF(aw_result);
      return -1;
    }

    Py_DECREF(aw_result);
    return yield_from_emit_yield(ctx, frame, yf, out_ev, insn);
  }

  /* Throw(StopAsyncIteration): delegate was closed with that; discard value. */
  if (action == KOS_YF_THROW && KosDynYieldFrom_PENDING_ARG(yf)) {
    PyObject *exc = KosDynYieldFrom_PENDING_ARG(yf);
    if (PyObject_IsInstance(exc, (PyObject *) PyExc_StopAsyncIteration)) {
      PyObject *final_val = StopAsyncIteration_VALUE(exc);
      Py_XINCREF(final_val);
      Py_XDECREF(KosDynYieldFrom_PENDING_ARG(yf));
      KosDynYieldFrom_PENDING_ARG(yf) = NULL;
      KosDynYieldFrom_PENDING_ACTION(yf) = KOS_YF_STEP;
      kos_dyn_pop_yield_from(ctx, yf);
      Py_XDECREF(final_val);
      return 0;
    }
  }

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
    kos_dyn_pop_yield_from(ctx, yf);
    return 0;
  }

  if (! awaitable) {
    if (PyErr_ExceptionMatches(PyExc_StopAsyncIteration)) {
      PyObject *exc = PyErr_GetRaisedException();
      PyObject *final_val = StopAsyncIteration_VALUE(exc);
      Py_XINCREF(final_val);
      Py_DECREF(exc);
      kos_dyn_pop_yield_from(ctx, yf);
      Py_XDECREF(final_val);
      return 0;
    }
    return -1;
  }

  int suspended = 0;
  PyObject *yielded = NULL;
  PyObject *result = eager_step_awaitable(awaitable, ctx, &suspended, &yielded);

  if (suspended && yielded) {
    KosDynYieldFrom_INFLIGHT(yf) = awaitable;
    KosDynYieldFrom_PHASE_SET(yf, KOS_YF_PHASE_EXPECT_AWAIT);
    yield_from_rollback_pc(ctx, insn, yf);
    KosEvent_KIND(out_ev) = KOS_EV_AWAIT;
    KosEvent_PAYLOAD(out_ev) = yielded;
    KosContext_FLAGS(ctx) &= ~KOS_CTX_RUNNING;
    KosContext_FLAGS(ctx) |= KOS_CTX_DEAD;
    return 1;
  }

  Py_DECREF(awaitable);
  if (result) {
    KosDynYieldFrom_PENDING_ACTION(yf) = KOS_YF_STEP;
    Py_XDECREF(KosDynYieldFrom_PENDING_ARG(yf));
    KosDynYieldFrom_PENDING_ARG(yf) = NULL;
    if (KosStackBlock_PushRef(&KosDynFrame_STACK(frame), result) < 0) {
      Py_DECREF(result);
      return -1;
    }
    Py_DECREF(result);
    return yield_from_emit_yield(ctx, frame, yf, out_ev, insn);
  }

  if (PyErr_ExceptionMatches(PyExc_StopAsyncIteration)) {
    PyObject *exc = PyErr_GetRaisedException();
    PyObject *final_val = StopAsyncIteration_VALUE(exc);
    Py_XINCREF(final_val);
    Py_DECREF(exc);
    kos_dyn_pop_yield_from(ctx, yf);
    Py_XDECREF(final_val);
    return 0;
  }
  return -1;
}


/**
   Drive one step of a Koskari async-generator delegate (flattened into the
   same koskari_eval).  Handles resume-from-await inject, close,
   throw(StopAsyncIteration), and stepping.  Returns 0 continue, -1 error.
*/
static int yield_from_drive_kos_async_gen(
    KosContext *ctx,
    KosDynFrame *frame,
    KosDynYieldFrom *yf,
    PyObject *delegate,
    uint8_t action,
    KosEvent *out_ev,
    KosOp *insn) {

  KosPyAsyncGen *agen = (KosPyAsyncGen *) delegate;

  PyObject *yf_send_val = NULL;
  PyObject *yf_throw_exc = NULL;
  PyObject *yf_throw_typ = NULL;

  (void) frame;
  (void) out_ev;
  (void) insn;

  /* Resuming from await: pass the result as send_val to the inner context */
  if (KosDynYieldFrom_INFLIGHT(yf)) {
    PyObject *aw_result = KosDynYieldFrom_PENDING_ARG(yf);
    Py_XDECREF(KosDynYieldFrom_INFLIGHT(yf));
    KosDynYieldFrom_INFLIGHT(yf) = NULL;
    KosDynYieldFrom_PENDING_ARG(yf) = NULL;
    KosDynYieldFrom_PENDING_ACTION(yf) = KOS_YF_STEP;
    if (! aw_result) {
      PyErr_SetString(PyExc_RuntimeError,
                      "yield-from resume with no await result");
      return -1;
    }
    yf_send_val = aw_result;
    Py_INCREF(yf_send_val);
  }

  if (action == KOS_YF_CLOSE) {
    agen->closed = 1;
    kos_dyn_pop_yield_from(ctx, yf);
    PyErr_SetNone(PyExc_GeneratorExit);
    return -1;
  }

  if (action == KOS_YF_THROW && KosDynYieldFrom_PENDING_ARG(yf)) {
    PyObject *exc = KosDynYieldFrom_PENDING_ARG(yf);
    if (PyObject_IsInstance(exc, (PyObject *) PyExc_StopAsyncIteration)) {
      PyObject *final_val = StopAsyncIteration_VALUE(exc);
      Py_XINCREF(final_val);
      Py_XDECREF(KosDynYieldFrom_PENDING_ARG(yf));
      KosDynYieldFrom_PENDING_ARG(yf) = NULL;
      KosDynYieldFrom_PENDING_ACTION(yf) = KOS_YF_STEP;
      kos_dyn_pop_yield_from(ctx, yf);
      Py_XDECREF(final_val);
      return 0;
    }
  }

  if (agen->closed) {
    kos_dyn_pop_yield_from(ctx, yf);
    return 0;
  }

  if (! agen->gen || ! KosDynGen_ACTIVE((KosDynGen *) agen->gen)) {
    kos_dyn_pop_yield_from(ctx, yf);
    return 0;
  }

  if (action == KOS_YF_SEND && KosDynYieldFrom_PENDING_ARG(yf)) {
    yf_send_val = KosDynYieldFrom_PENDING_ARG(yf);
    Py_INCREF(yf_send_val);
  }

  if (action == KOS_YF_THROW && KosDynYieldFrom_PENDING_ARG(yf)) {
    yf_throw_typ = KosDynYieldFrom_PENDING_ARG_TYPE(yf);
    yf_throw_exc = KosDynYieldFrom_PENDING_ARG(yf);
    if (yf_throw_typ)
      Py_INCREF(yf_throw_typ);
    Py_INCREF(yf_throw_exc);
  }

  {
    PyObject *pending = KosDynYieldFrom_PENDING_ARG(yf);
    PyObject *pending_typ = KosDynYieldFrom_PENDING_ARG_TYPE(yf);
    KosDynYieldFrom_PENDING_ARG(yf) = NULL;
    KosDynYieldFrom_PENDING_ARG_TYPE(yf) = NULL;
    KosDynYieldFrom_PENDING_ACTION(yf) = KOS_YF_STEP;
    if (action == KOS_YF_THROW) {
      if (pending)
        Py_DECREF(pending);
      if (pending_typ)
        Py_DECREF(pending_typ);
    }
  }

  agen->in_progress = 1;

  if (yf_throw_exc)
    PyErr_SetObject(yf_throw_typ, yf_throw_exc);

  if (op_attach_koskari_agen_frame_for_yield_from(ctx, agen, yf) < 0) {
    Py_XDECREF(yf_send_val);
    if (yf_throw_typ) {
      Py_DECREF(yf_throw_typ);
      Py_DECREF(yf_throw_exc);
    }
    agen->in_progress = 0;
    agen->closed = 1;
    return -1;
  }

  if (yf_throw_typ) {
    Py_DECREF(yf_throw_typ);
    Py_DECREF(yf_throw_exc);
  }

  if (! PyErr_Occurred()) {
    PyObject *popped = KosStackBlock_PopRef(
        &KosDynFrame_STACK(KosContext_FRAME_TOP(ctx)));
    PyObject *pushv;

    if (! popped)
      return -1;
    Py_DECREF(popped);
    pushv = yf_send_val ? yf_send_val : Py_None;
    if (KosStackBlock_PushRef(
            &KosDynFrame_STACK(KosContext_FRAME_TOP(ctx)), pushv) < 0) {
      Py_XDECREF(yf_send_val);
      return -1;
    }
    Py_XDECREF(yf_send_val);
  } else {
    Py_XDECREF(yf_send_val);
  }

  return 0;
}


/**
   YIELD_FROM: delegate to a sub-iterator or sub-generator; PC-stable
   state machine.

   First entry: TOS is the yield-from expression; we normalize to
   (delegate, kind), push the yield_from dyn node, roll back PC, and
   re-dispatch so the next iteration runs this handler again and drives
   the delegate.  Subsequent entries: we have the yield_from node; we
   consume any resume inject, then drive one step (or close/throw)
   according to delegate kind.
*/

KOS_OP_HANDLER(YIELD_FROM) {

  KosDynNode *dyn_top = KosContext_DYN_TOP(ctx);
  KosDynFrame *frame = CURRENT_FRAME(ctx);

  if (! KosDynNode_IS_YIELD_FROM(dyn_top)) {
    /* 1. First entry: TOS is the yield-from expression result. Pop it,
       normalize to (delegate, kind), push the yield_from node, set
       PHASE=DRIVING, PENDING_ACTION=STEP, roll back PC, return 0. */
    PyObject *subexpr = KosStackBlock_PopRef(&KosDynFrame_STACK(frame));
    if (! subexpr)
      return KOS_OP_POST_ERR();

    PyObject *delegate = NULL;
    uint8_t kind = KOS_YF_SYNC_ITER; /* overwritten by yield_from_normalize */
    if (yield_from_normalize(subexpr, &delegate, &kind) < 0) {
      Py_DECREF(subexpr);
      return KOS_OP_POST_ERR();
    }

    Py_DECREF(subexpr);
    if (! kos_dyn_push_yield_from(ctx, delegate, kind)) {
      Py_DECREF(delegate);
      return KOS_OP_POST_ERR();
    }

    Py_DECREF(delegate);
    {
      KosDynYieldFrom *yf_new = (KosDynYieldFrom *) KosContext_DYN_TOP(ctx);
      KosDynYieldFrom_PENDING_ACTION(yf_new) = KOS_YF_STEP;
      KosDynYieldFrom_PHASE_SET(yf_new, KOS_YF_PHASE_DRIVING);
      yield_from_rollback_pc(ctx, insn, yf_new);
    }
    return KOS_OP_RUN();
  }

  KosDynYieldFrom *yf = (KosDynYieldFrom *) dyn_top;
  uint8_t action = KosDynYieldFrom_PENDING_ACTION(yf);
  uint8_t kind = KosDynYieldFrom_KIND(yf);

  /* 2. INJECT_MODE == KOS_INJECT_THROW: consume inject, set PENDING_ACTION=THROW,
     fall through to drive. */
  if (KosContext_INJECT_MODE(ctx) == KOS_INJECT_THROW) {
    PyObject *exc = KosContext_INJECT_VALUE(ctx);
    PyObject *exc_type = KosContext_INJECT_TYPE(ctx);
    KosContext_INJECT_MODE(ctx) = KOS_INJECT_NONE;
    KosContext_INJECT_VALUE(ctx) = NULL;
    KosContext_INJECT_TYPE(ctx) = NULL;
    Py_XDECREF(KosDynYieldFrom_PENDING_ARG(yf));
    KosDynYieldFrom_PENDING_ARG(yf) = exc;
    Py_INCREF(exc);
    Py_XDECREF(KosDynYieldFrom_PENDING_ARG_TYPE(yf));
    KosDynYieldFrom_PENDING_ARG_TYPE(yf) = exc_type;
    if (exc_type)
      Py_INCREF(exc_type);
    action = KOS_YF_THROW;
    KosDynYieldFrom_PENDING_ACTION(yf) = action;
    KosDynYieldFrom_PHASE_SET(yf, KOS_YF_PHASE_DRIVING);
  }

  /* 3. PHASE == EXPECT_SEND: pop TOS (send value or stale). If stack had >= 2
     elements, TOS was send_val and next was stale; else TOS was stale only. */
  else if (KosDynYieldFrom_PHASE(yf) == KOS_YF_PHASE_EXPECT_SEND) {
    size_t sz = KosStackBlock_LEN(KosDynFrame_STACK(frame));
    if (sz == 0)
      return KOS_OP_POST_ERR();
    PyObject *first = KosStackBlock_PopRef(&KosDynFrame_STACK(frame));
    if (sz >= 2) {
      Py_XDECREF(KosDynYieldFrom_PENDING_ARG(yf));
      KosDynYieldFrom_PENDING_ARG(yf) = first;
      Py_XDECREF(KosStackBlock_PopRef(&KosDynFrame_STACK(frame)));
    } else {
      Py_XDECREF(first);
      Py_XDECREF(KosDynYieldFrom_PENDING_ARG(yf));
      KosDynYieldFrom_PENDING_ARG(yf) = Py_None;
      Py_INCREF(Py_None);
    }
    action = KOS_YF_SEND;
    KosDynYieldFrom_PENDING_ACTION(yf) = action;
    KosDynYieldFrom_PHASE_SET(yf, KOS_YF_PHASE_DRIVING);
  }

  /* 4. PHASE == EXPECT_AWAIT: pop TOS (await result). Underrun is an error. */
  else if (KosDynYieldFrom_PHASE(yf) == KOS_YF_PHASE_EXPECT_AWAIT) {
    PyObject *aw_result = KosStackBlock_PopRef(&KosDynFrame_STACK(frame));
    if (! aw_result)
      return KOS_OP_POST_ERR();
    Py_INCREF(aw_result);
    Py_XDECREF(KosDynYieldFrom_PENDING_ARG(yf));
    KosDynYieldFrom_PENDING_ARG(yf) = aw_result;
    action = KOS_YF_STEP;
    KosDynYieldFrom_PENDING_ACTION(yf) = action;
    KosDynYieldFrom_PHASE_SET(yf, KOS_YF_PHASE_DRIVING);
  }

  /* 5. Default (DRIVING): use PENDING_ACTION and PENDING_ARG as-is. */
  PyObject *delegate = KosDynYieldFrom_DELEGATE(yf);

  if (kind == KOS_YF_SYNC_ITER)
    return kos_op_outcome_from_int_step(
        yield_from_drive_sync_iter(ctx, frame, yf, delegate, action, out_ev,
                                   insn));
  if (kind == KOS_YF_SYNC_GEN)
    return kos_op_outcome_from_int_step(
        yield_from_drive_sync_gen(ctx, frame, yf, delegate, action, out_ev,
                                  insn));
  if (kind == KOS_YF_PY_ASYNC_ITER || kind == KOS_YF_PY_ASYNC_GEN)
    return kos_op_outcome_from_int_step(
        yield_from_drive_py_async(ctx, frame, yf, delegate, kind, action,
                                  out_ev, insn));
  if (kind == KOS_YF_KOS_ASYNC_GEN)
    return kos_op_outcome_from_int_step(
        yield_from_drive_kos_async_gen(ctx, frame, yf, delegate, action,
                                       out_ev, insn));

  PyErr_SetString(PyExc_RuntimeError, "unexpected delegate kind");
  return KOS_OP_POST_ERR();
}
