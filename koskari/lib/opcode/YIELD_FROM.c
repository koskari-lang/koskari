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
     We drive it in-process via koskari_eval; no Python protocol. */
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
   iteration re-dispatches to YIELD_FROM (used when suspending).
*/
static void yield_from_rollback_pc(KosContext *ctx, KosOp *insn) {

  KosDynFrame *frame = CURRENT_FRAME(ctx);
  KosCode *code = KosDynFrame_CODE(frame);
  uint32_t pc = (uint32_t)(insn - KosCode_CODE(code));
  KosDynFrame_PC(frame) = pc;
}


/**
   Fetch the value from the currently set StopIteration exception.
   Call only when PyErr_ExceptionMatches(PyExc_StopIteration) is true.
   Consumes the exception. Returns a new reference (value or Py_None).
*/
static PyObject *yield_from_fetch_stop_iteration_value(void) {

  PyObject *value = NULL;
  PyGen_FetchStopIterationValue(&value);
  if (! value)
    value = Py_None;
  Py_INCREF(value);
  return value;
}


/**
   Emit KOS_EV_YIELD using the value at TOS. Call after pushing the
   yielded value onto the frame stack. Sets DEAD and returns 1.
*/
static int yield_from_emit_yield(
    KosContext *ctx,
    KosDynFrame *frame,
    KosEvent *out_ev,
    KosOp *insn) {

  yield_from_rollback_pc(ctx, insn);
  {
    PyObject *const *tos = KosStackBlock_TailPtr(
        KosDynFrame_STACK(frame), 1);
    out_ev->kind = KOS_EV_YIELD;
    out_ev->payload = tos ? *tos : NULL;
    Py_XINCREF(out_ev->payload);
  }
  KosContext_FLAGS(ctx) &= ~KOS_CTX_RUNNING;
  KosContext_FLAGS(ctx) |= KOS_CTX_DEAD;
  return 1;
}


/**
   Complete YIELD_FROM with the delegate's final value: pop the yield_from
   node, push final_val onto the frame stack, return 0 so the loop continues
   and the next opcode sees the value. Takes ownership of final_val.
   Use this for delegate exhaustion; do not emit a boundary event.
*/
static int yield_from_complete_with_value(
    KosContext *ctx,
    KosDynYieldFrom *yf,
    KosDynFrame *frame,
    PyObject *final_val) {

  kos_dyn_pop_yield_from(ctx, yf);
  if (KosStackBlock_PushRef(&KosDynFrame_STACK(frame), final_val) < 0) {
    Py_DECREF(final_val);
    return -1;
  }
  Py_DECREF(final_val);
  return 0;
}


/**
   Drive one step of a sync-iterator delegate (PyIter_Next). Returns
   0 continue, 1 event (YIELD), -1 error. Exhaustion completes with
   yield_from_complete_with_value (push final value, return 0).
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
    return yield_from_emit_yield(ctx, frame, out_ev, insn);
  }
  if (! PyErr_Occurred())
    return -1;
  if (! PyErr_ExceptionMatches(PyExc_StopIteration))
    return -1;
  {
    PyObject *final_val = yield_from_fetch_stop_iteration_value();
    return yield_from_complete_with_value(ctx, yf, frame, final_val);
  }
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
    PyObject *meth = PyUnicode_FromString("throw");
    if (meth && exc) {
      PyObject *res = PyObject_CallMethodObjArgs(delegate, meth,
          exc, Py_None, Py_None, NULL);
      Py_DECREF(meth);
      if (res) {
        KosDynYieldFrom_PENDING_ACTION(yf) = KOS_YF_STEP;
        Py_XDECREF(KosDynYieldFrom_PENDING_ARG(yf));
        KosDynYieldFrom_PENDING_ARG(yf) = NULL;
        if (KosStackBlock_PushRef(&KosDynFrame_STACK(frame), res) < 0) {
          Py_DECREF(res);
          return -1;
        }
        Py_DECREF(res);
        return yield_from_emit_yield(ctx, frame, out_ev, insn);
      }
    }
    kos_yieldfrom_close_delegate(yf);
    kos_dyn_pop_yield_from(ctx, yf);
    return -1;
  }
  if (action == KOS_YF_CLOSE) {
    PyObject *meth = PyUnicode_FromString("close");
    if (meth) {
      PyObject *res = PyObject_CallMethodObjArgs(delegate, meth, NULL);
      Py_DECREF(meth);
      Py_XDECREF(res);
    }
    PyErr_Clear();
    kos_dyn_pop_yield_from(ctx, yf);
    PyErr_SetNone(PyExc_GeneratorExit);
    return -1;
  }
  {
    PyObject *meth = PyUnicode_FromString("send");
    if (! meth)
      return -1;
    PyObject *res = PyObject_CallMethodObjArgs(delegate, meth, send_val, NULL);
    Py_DECREF(meth);
    if (PyErr_ExceptionMatches(PyExc_StopIteration)) {
      Py_XDECREF(res);
      PyObject *final_val = yield_from_fetch_stop_iteration_value();
      return yield_from_complete_with_value(ctx, yf, frame, final_val);
    }
    if (res) {
      if (KosStackBlock_PushRef(&KosDynFrame_STACK(frame), res) < 0) {
        Py_DECREF(res);
        return -1;
      }
      Py_DECREF(res);
      return yield_from_emit_yield(ctx, frame, out_ev, insn);
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
    return yield_from_emit_yield(ctx, frame, out_ev, insn);
  }

  /* Throw(StopAsyncIteration): delegate was closed with that; pop,
     push the value, return 0 so YIELD_FROM completes with it. */
  if (action == KOS_YF_THROW && KosDynYieldFrom_PENDING_ARG(yf)) {
    PyObject *exc = KosDynYieldFrom_PENDING_ARG(yf);
    if (PyObject_IsInstance(exc, (PyObject *) PyExc_StopAsyncIteration)) {
      PyObject *final_val = StopAsyncIteration_VALUE(exc);
      Py_XDECREF(KosDynYieldFrom_PENDING_ARG(yf));
      KosDynYieldFrom_PENDING_ARG(yf) = NULL;
      KosDynYieldFrom_PENDING_ACTION(yf) = KOS_YF_STEP;
      kos_dyn_pop_yield_from(ctx, yf);
      if (KosStackBlock_PushRef(&KosDynFrame_STACK(frame), final_val) < 0) {
        Py_DECREF(final_val);
        return -1;
      }
      Py_DECREF(final_val);
      return 0;
    }
  }

  /* Build the awaitable for this step: __anext__, asend, athrow, or aclose. */
  PyObject *awaitable = NULL;
  if (action == KOS_YF_STEP || (action == KOS_YF_SEND && ! KosDynYieldFrom_PENDING_ARG(yf))) {
    awaitable = get_await_next(delegate);
  } else if (kind == KOS_YF_PY_ASYNC_GEN && action == KOS_YF_SEND) {
    PyObject *send_val = KosDynYieldFrom_PENDING_ARG(yf);
    PyObject *name = PyUnicode_FromString("asend");
    if (name) {
      PyObject *tmp = PyObject_CallMethodObjArgs(delegate, name, send_val, NULL);
      Py_DECREF(name);
      if (tmp) {
        awaitable = PyObject_GetAwaitable(tmp);
        Py_DECREF(tmp);
      }
    }
  } else if (kind == KOS_YF_PY_ASYNC_GEN && action == KOS_YF_THROW) {
    PyObject *exc = KosDynYieldFrom_PENDING_ARG(yf);
    PyObject *meth = PyUnicode_FromString("athrow");
    if (meth && exc) {
      PyObject *typ = (PyObject *) Py_TYPE(exc);
      PyObject *tmp = PyObject_CallMethodObjArgs(delegate, meth, typ, exc,
                                                 Py_None, NULL);
      Py_DECREF(meth);
      if (tmp) {
        awaitable = PyObject_GetAwaitable(tmp);
        Py_DECREF(tmp);
      }
    } else {
      Py_XDECREF(meth);
    }
  } else if (kind == KOS_YF_PY_ASYNC_GEN && action == KOS_YF_CLOSE) {
    PyObject *meth = PyUnicode_FromString("aclose");
    if (meth) {
      PyObject *tmp = PyObject_CallMethodObjArgs(delegate, meth, NULL);
      Py_DECREF(meth);
      if (tmp) {
        awaitable = PyObject_GetAwaitable(tmp);
        Py_DECREF(tmp);
      }
    }
  } else if (action == KOS_YF_CLOSE) {
    /* PY_ASYNC_ITER has no aclose; treat as exhausted with None. */
    kos_dyn_pop_yield_from(ctx, yf);
    if (KosStackBlock_PushRef(&KosDynFrame_STACK(frame), Py_None) < 0)
      return -1;
    return 0;
  }

  if (! awaitable) {
    if (PyErr_ExceptionMatches(PyExc_StopAsyncIteration)) {
      PyObject *exc = PyErr_GetRaisedException();
      PyObject *final_val = StopAsyncIteration_VALUE(exc);
      Py_DECREF(exc);
      kos_dyn_pop_yield_from(ctx, yf);
      if (KosStackBlock_PushRef(&KosDynFrame_STACK(frame), final_val) < 0) {
        Py_DECREF(final_val);
        return -1;
      }
      Py_DECREF(final_val);
      return 0;
    }
    return -1;
  }

  int suspended = 0;
  PyObject *yielded = NULL;
  PyObject *result = eager_step_awaitable(awaitable, ctx, &suspended, &yielded);

  if (suspended && yielded) {
    KosDynYieldFrom_INFLIGHT(yf) = awaitable;
    yield_from_rollback_pc(ctx, insn);
    out_ev->kind = KOS_EV_AWAIT;
    out_ev->payload = yielded;
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
    return yield_from_emit_yield(ctx, frame, out_ev, insn);
  }
  if (PyErr_ExceptionMatches(PyExc_StopAsyncIteration)) {
    PyObject *exc = PyErr_GetRaisedException();
    PyObject *final_val = StopAsyncIteration_VALUE(exc);
    Py_DECREF(exc);
    kos_dyn_pop_yield_from(ctx, yf);
    if (KosStackBlock_PushRef(&KosDynFrame_STACK(frame), final_val) < 0) {
      Py_DECREF(final_val);
      return -1;
    }
    Py_DECREF(final_val);
    return 0;
  }
  return -1;
}


/**
   Drive one step of a Koskari async-generator delegate (koskari_eval).
   Handles resume-from-await inject, close, throw(StopAsyncIteration),
   and stepping. Returns 0 continue, 1 event (YIELD/AWAIT), -1 error.
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

  /* Resuming from await: inject the result into the inner context
     so the next koskari_eval run continues from the AWAIT. */
  if (KosDynYieldFrom_INFLIGHT(yf)) {
    KosContext *inner = agen->ctx;
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
    Py_XDECREF(KosContext_PY_RESUME_INJECT(inner));
    KosContext_PY_RESUME_INJECT(inner) = aw_result;
    KosContext_PY_RESUME_KIND(inner) = KOS_RESUME_AWAIT;
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
      Py_XDECREF(KosDynYieldFrom_PENDING_ARG(yf));
      KosDynYieldFrom_PENDING_ARG(yf) = NULL;
      KosDynYieldFrom_PENDING_ACTION(yf) = KOS_YF_STEP;
      kos_dyn_pop_yield_from(ctx, yf);
      if (KosStackBlock_PushRef(&KosDynFrame_STACK(frame), final_val) < 0) {
        Py_DECREF(final_val);
        return -1;
      }
      Py_DECREF(final_val);
      return 0;
    }
  }

  if (! agen->ctx || agen->closed) {
    kos_dyn_pop_yield_from(ctx, yf);
    if (KosStackBlock_PushRef(&KosDynFrame_STACK(frame), Py_None) < 0)
      return -1;
    return 0;
  }

  if (action == KOS_YF_SEND && KosDynYieldFrom_PENDING_ARG(yf)) {
    KosContext *inner = agen->ctx;
    PyObject *send_val = KosDynYieldFrom_PENDING_ARG(yf);
    Py_XDECREF(KosContext_PY_RESUME_INJECT(inner));
    KosContext_PY_RESUME_INJECT(inner) = send_val;
    KosContext_PY_RESUME_KIND(inner) = KOS_RESUME_YIELD;
  }
  if (action == KOS_YF_THROW && KosDynYieldFrom_PENDING_ARG(yf)) {
    KosContext *inner = agen->ctx;
    PyObject *exc = KosDynYieldFrom_PENDING_ARG(yf);
    Py_XDECREF(KosContext_INJECT_VALUE(inner));
    KosContext_INJECT_VALUE(inner) = exc;
    Py_INCREF(exc);
    KosContext_INJECT_MODE(inner) = KOS_INJECT_THROW;
  }

  agen->in_progress = 1;
  KosEvent ev = {0};
  int res = koskari_eval(agen->ctx, &ev);
  agen->in_progress = 0;

  {
    PyObject *pending = KosDynYieldFrom_PENDING_ARG(yf);
    KosDynYieldFrom_PENDING_ARG(yf) = NULL;
    KosDynYieldFrom_PENDING_ACTION(yf) = KOS_YF_STEP;
    if (action == KOS_YF_THROW && pending)
      Py_DECREF(pending);
  }

  if (res < 0) {
    agen->closed = 1;
    kos_dyn_pop_yield_from(ctx, yf);
    return -1;
  }

  switch (ev.kind) {
  case KOS_EV_YIELD:
    if (KosStackBlock_PushRef(&KosDynFrame_STACK(frame), ev.payload) < 0) {
      Py_DECREF(ev.payload);
      return -1;
    }
    Py_DECREF(ev.payload);
    return yield_from_emit_yield(ctx, frame, out_ev, insn);

  case KOS_EV_RETURN: {
    PyObject *payload = ev.payload ? ev.payload : Py_None;
    Py_INCREF(payload);
    if (ev.payload)
      Py_DECREF(ev.payload);
    return yield_from_complete_with_value(ctx, yf, frame, payload);
  }

  case KOS_EV_AWAIT:
    Py_INCREF(ev.payload);
    KosDynYieldFrom_INFLIGHT(yf) = ev.payload;
    yield_from_rollback_pc(ctx, insn);
    out_ev->kind = KOS_EV_AWAIT;
    out_ev->payload = ev.payload;
    KosContext_FLAGS(ctx) &= ~KOS_CTX_RUNNING;
    KosContext_FLAGS(ctx) |= KOS_CTX_DEAD;
    return 1;

  case KOS_EV_RAISE:
    agen->closed = 1;
    kos_dyn_pop_yield_from(ctx, yf);
    if (ev.payload) {
      PyErr_SetObject((PyObject *) Py_TYPE(ev.payload), ev.payload);
      Py_DECREF(ev.payload);
    } else {
      PyErr_SetNone(PyExc_StopAsyncIteration);
    }
    return -1;

  default:
    agen->closed = 1;
    kos_dyn_pop_yield_from(ctx, yf);
    PyErr_SetString(PyExc_RuntimeError, "unexpected event from Koskari async gen");
    return -1;
  }
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
    /* First entry: TOS is the yield-from expression result. Pop it,
       normalize to (delegate, kind), push the yield_from node, roll
       back PC so the next iteration runs this handler again and we
       drive the delegate. */
    PyObject *subexpr = KosStackBlock_PopRef(&KosDynFrame_STACK(frame));
    if (! subexpr) {
      return -1;
    }
    PyObject *delegate = NULL;
    uint8_t kind = 0;
    if (yield_from_normalize(subexpr, &delegate, &kind) < 0) {
      Py_DECREF(subexpr);
      return -1;
    }
    Py_DECREF(subexpr);
    if (! kos_dyn_push_yield_from(ctx, delegate, kind)) {
      Py_DECREF(delegate);
      return -1;
    }
    Py_DECREF(delegate);
    KosDynYieldFrom_PENDING_ACTION(
        (KosDynYieldFrom *) KosContext_DYN_TOP(ctx)) = KOS_YF_STEP;
    yield_from_rollback_pc(ctx, insn);
    return 0;
  }

  /* Resume: we have a yield_from node. If the loop pushed an inject
     (from a previous yield or await), pop it into the node and set
     action; then drive one step according to kind. */
  KosDynYieldFrom *yf = (KosDynYieldFrom *) dyn_top;
  uint8_t action = KosDynYieldFrom_PENDING_ACTION(yf);

  if (KosContext_PY_RESUME_KIND(ctx) != KOS_RESUME_NONE) {
    uint8_t resume_kind = KosContext_PY_RESUME_KIND(ctx);
    KosContext_PY_RESUME_KIND(ctx) = KOS_RESUME_NONE;
    PyObject *inject = KosStackBlock_PopRef(&KosDynFrame_STACK(frame));
    if (! inject)
      return -1;
    Py_XDECREF(KosDynYieldFrom_PENDING_ARG(yf));
    KosDynYieldFrom_PENDING_ARG(yf) = inject;
    action = (resume_kind == KOS_RESUME_AWAIT) ? KOS_YF_STEP : KOS_YF_SEND;
    KosDynYieldFrom_PENDING_ACTION(yf) = action;
  }
  if (KosContext_INJECT_MODE(ctx) == KOS_INJECT_THROW) {
    PyObject *exc = KosContext_INJECT_VALUE(ctx);
    KosContext_INJECT_MODE(ctx) = KOS_INJECT_NONE;
    KosContext_INJECT_VALUE(ctx) = NULL;
    Py_XDECREF(KosDynYieldFrom_PENDING_ARG(yf));
    KosDynYieldFrom_PENDING_ARG(yf) = exc;
    Py_INCREF(exc);
    action = KOS_YF_THROW;
    KosDynYieldFrom_PENDING_ACTION(yf) = action;
  }

  uint8_t kind = KosDynYieldFrom_KIND(yf);
  PyObject *delegate = KosDynYieldFrom_DELEGATE(yf);

  /* Drive one step by delegate kind. */
  if (kind == KOS_YF_SYNC_ITER)
    return yield_from_drive_sync_iter(ctx, frame, yf, delegate, action, out_ev, insn);
  if (kind == KOS_YF_SYNC_GEN)
    return yield_from_drive_sync_gen(ctx, frame, yf, delegate, action, out_ev, insn);
  if (kind == KOS_YF_PY_ASYNC_ITER || kind == KOS_YF_PY_ASYNC_GEN)
    return yield_from_drive_py_async(ctx, frame, yf, delegate, kind, action, out_ev, insn);
  if (kind == KOS_YF_KOS_ASYNC_GEN)
    return yield_from_drive_kos_async_gen(ctx, frame, yf, delegate, action, out_ev, insn);

  return op_unimplemented_stub(ctx, out_ev, insn);
}
