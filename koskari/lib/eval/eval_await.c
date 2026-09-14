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
  koskari/lib/eval/eval_await.c

  Awaiting a Python awaitable from the evaluator: the eager first step,
  the demand-extent suspension denial, push_or_await and its fused
  CALL* variant, and the PUSH result helpers. Included into eval.c as a
  segment (see the eval/ includes there).

  :author: Christopher O'Brien <obriencj@preoccupied.net>
  :license: GNU General Public License v3
*/




/**
   Attempt eager-step of a Python awaitable.

   This tries to get __await__() and call send(None) once.
   If the awaitable completes immediately (StopIteration), the value
   is returned. If it yields, we store the iterator and return the
   yielded value with *suspended = 1.

   :param awaitable: the Python awaitable object
   :param ctx: context to store await state if suspended
   :param suspended: output flag, set to 1 if we need to suspend
   :param yielded: output, the yielded value if suspended

   :return: completed value on immediate completion, NULL on suspend or error
*/
static PyObject *eager_step_awaitable(
    PyObject *awaitable,
    KosContext *ctx,
    int *suspended,
    PyObject **yielded) {

  *suspended = 0;
  *yielded = NULL;

  if (! awaitable) {
    PyErr_SetString(PyExc_TypeError,
                    "eager_step_awaitable: awaitable is NULL");
    return NULL;
  }

  /* Get __await__ */
  PyObject *await_iter = NULL;

  PyObject *await_method = PyObject_GetAttr(awaitable, str_await);
  if (! await_method) {
    /* Maybe it's already an iterator? */
    PyErr_Clear();
    await_iter = awaitable;
    Py_INCREF(await_iter);

  } else {
    await_iter = PyObject_CallNoArgs(await_method);
    Py_DECREF(await_method);
    if (! await_iter)
      return NULL;
  }

  SAYF("[eval] eager_step_awaitable before send(None) (await_iter=%d)\n",
       await_iter ? 1 : 0);

  /* Try first send(None) */
  PyObject *result = PyObject_CallMethodObjArgs(await_iter, str_send,
                                                Py_None, NULL);

  if (result) {
    /* Yielded a value - need to suspend */
    *suspended = 1;

    /* Store yielded value */
    *yielded = result;

    /* Store iterator for later resumption */
    Py_XDECREF(KosContext_PY_AWAIT_ITER(ctx));
    KosContext_PY_AWAIT_ITER(ctx) = await_iter;

    /* signal suspend */
    return NULL;
  }

  /* Check for StopIteration (immediate completion) */
  if (PyErr_ExceptionMatches(PyExc_StopIteration)) {
    PyObject *value = NULL;
    if (PyGen_FetchStopIterationValue(&value) < 0) {
      /* FetchStopIterationValue failed and set a new exception; propagate. */
      Py_DECREF(await_iter);
      return NULL;
    }
    Py_DECREF(await_iter);

    if (! value) {
      value = Py_None;
      Py_INCREF(value);
    }

    return value;
  }

  /* Other exception */
  Py_DECREF(await_iter);
  return NULL;
}


static inline PyObject *KosEvalState_EagerStepAwaitable(
    KosEvalState *es,
    PyObject *awaitable,
    int *suspended,
    PyObject **yielded) {

  return eager_step_awaitable(awaitable, KosEvalState_CONTEXT(es), suspended, yielded);
}


/**
   Get the awaitable for one step of an async iterator (__anext__).
   Mirror of sibilant-ceval get_await_next: tp_as_async->am_anext then
   PyObject_GetAwaitable. Caller must Py_DECREF the result.

   :return: new reference to awaitable, or NULL on error or if not async
*/
static PyObject *get_await_next(PyObject *val) {
  PyTypeObject *type = Py_TYPE(val);
  unaryfunc anext = NULL;

  if (type->tp_as_async)
    anext = type->tp_as_async->am_anext;

  if (! anext)
    return NULL;

  PyObject *thing = anext(val);
  if (! thing)
    return NULL;

  PyObject *awaitable = PyObject_GetAwaitable(thing);
  Py_DECREF(thing);
  return awaitable;
}


/* === Demand extent detection and suspension denial === */


/**
   Return 1 if the current frame carries KOS_FRAME_DEMAND. O(1): the flag is
   inherited by kos_dyn_push_frame at install time, so the current frame always
   answers for the entire demand extent.
*/
static int kos_eval_in_demand(KosEvalState *es) {
  KosDynFrame *frame = KosEvalState_FRAME(es);
  return frame && (KosDynFrame_FLAGS(frame) & KOS_FRAME_DEMAND);
}


/**
   If execution is under a demand extent, set KoskariSyncViolation on PyErr
   and return 1. Otherwise return 0 (caller continues normally).

   Used only by YIELD_FROM sites whose pre-event state mutations (phase,
   inflight, PC flush) must not occur when denying. All other suspension
   paths are intercepted centrally in eval_event_await / eval_event_yield.
*/
static int kos_eval_demand_deny_suspend(KosEvalState *es, const char *msg) {
  if (! kos_eval_in_demand(es))
    return 0;

  /* The eager step parked the __await__ iterator for a resumption
     that is now refused; nothing will drive it again. */
  KosEvalState_PythonAwaitClearIter(es);
  kos_demand_set_sync_violation(msg);
  return 1;
}


/**
   Duck-typed mirror of asyncio.isfuture: the type provides
   _asyncio_future_blocking and the instance value is not None. Futures
   and Tasks are handles by convention, so implicit auto-await sites
   push them as plain values; only explicit AWAIT drives them.

   The class-level PyType_Lookup pre-filter keeps the common miss (every
   coroutine-returning call) free of exception churn and never consults
   an instance __getattr__ on non-future types.

   :return: 1 (future), 0 (not a future), -1 (error from a raising
       descriptor; PyErr set)
*/
static int kos_isfuture(PyObject *val) {
  if (! PyType_Lookup(Py_TYPE(val), str_asyncio_future_blocking))
    return 0;

  PyObject *blocking = PyObject_GetAttr(val, str_asyncio_future_blocking);
  if (! blocking) {
    if (PyErr_ExceptionMatches(PyExc_AttributeError)) {
      PyErr_Clear();
      return 0;
    }
    return -1;
  }

  int r = (blocking != Py_None);
  Py_DECREF(blocking);
  return r;
}


/**
   CALL* peephole: when the next instruction is POP 1 (discard TOS),
   release val and step past that POP instead of pushing. Returns 1
   when it elided, else 0 with val untouched. Only sound from a CALL*
   handler, where the frame PC is already the next instruction index
   after fetch; a path that pops the stack in C before the next opcode
   must not fuse.
*/
static inline int push_fuse_pop1(KosEvalState *es, PyObject *val) {
  uint32_t pc = KosEvalState_PC(es);
  KosOp *next = &KosEvalState_OPS(es)[pc];

  if (next->op == KOS_OP_POP && next->arg == 1) {
    Py_DECREF(val);
    KosEvalState_SET_PC(es, pc + 1u);
    return 1;
  }
  return 0;
}


/* Push val, or fuse it away when fuse is set and the peephole applies.
   val is consumed. */
static inline KosOpResult push_or_fuse(
    KosEvalState *es,
    PyObject *val,
    int fuse) {

  if (fuse && push_fuse_pop1(es, val))
    return EVAL_NORMAL();
  return (KosEvalState_StackPUSH(es, val) < 0) ? EVAL_RAISE() : EVAL_NORMAL();
}


/**
   Push val, or if val is awaitable run eager_step and either push the
   result or suspend (KOS_RES_AWAIT). Use only for (1) the result of a
   call (CALL opcode: callable returned a coroutine) and (2) the AWAIT
   opcode (explicitly awaiting TOS). All other paths must use
   KosEvalState_StackPUSH so we do not auto-await values that are merely
   loaded or computed.

   await_defer: the explicit AWAIT opcode drives a Defer or an asyncio
   future; implicit sites push those as plain handles.

   fuse: a CALL* handler passes 1 so a pushed result followed by POP 1
   is elided (push_fuse_pop1); every non-opcode caller (defer_drive,
   KosEvalState_Call, KosEvalState_PushOrAwait) passes 0. Both flags
   are literals at every call site and fold away.

   val is consumed (new reference).

   :return: EVAL_NORMAL, EVAL_AWAIT, or EVAL_RAISE
*/
static inline KosOpResult push_or_await(
    KosEvalState *es,
    PyObject *val,
    int await_defer,
    int fuse) {

  /* Awaitability gate first: a type-level probe (two loads), so the
     dominant non-awaitable result skips everything below -- including
     the Defer subtype walk, which is sound to hoist because KosDefer
     defines am_await and therefore always passes this gate. */
  PyTypeObject *type = Py_TYPE(val);
  SAYF("[eval] push_or_await entry is_awaitable=%d\n",
       (type->tp_as_async && type->tp_as_async->am_await) ? 1 : 0);
  if (! type->tp_as_async || ! type->tp_as_async->am_await)
    return push_or_fuse(es, val, fuse);

  /* Defer is not auto-awaited; push as plain value (explicit AWAIT drives it). */
  if (! await_defer && KosDefer_Check(val))
    return push_or_fuse(es, val, fuse);

  /* asyncio futures are handles; implicit sites push them plain. */
  if (! await_defer) {
    int isfut = kos_isfuture(val);
    if (isfut < 0) {
      Py_DECREF(val);
      return EVAL_RAISE();
    }
    if (isfut)
      return push_or_fuse(es, val, fuse);
  }

  int suspended = 0;
  PyObject *yielded = NULL;
  PyObject *result = KosEvalState_EagerStepAwaitable(es, val, &suspended, &yielded);
  Py_DECREF(val);

  if (result)
    return push_or_fuse(es, result, fuse);

  if (suspended && yielded) {
    KosEvalState_MarkSuspendedNotRunning(es);
    SAYF("[eval] push_or_await set KOS_RES_AWAIT (suspended=%d yielded=%d)\n",
         suspended, yielded ? 1 : 0);
    return EVAL_AWAIT(yielded);
  }

  return EVAL_RAISE();
}


KosOpResult KosEvalState_PushOrAwait(KosEvalState *es, PyObject *val) {
  if (unlikely(! val))
    return EVAL_RAISE();

  return push_or_await(es, val, 0, 0);
}


KosOpResult KosEvalState_SettleStage(KosEvalState *es, PyObject *val) {

  KosOpResult pr = KosEvalState_PushOrAwait(es, val);
  PyObject *settled;

  if (pr.kind != KOS_RES_NORMAL)
    return pr;  /* AWAIT: the revive delivers; RAISE: the walk propagates */

  settled = KosEvalState_StackPopRef(es);
  if (unlikely(! settled))
    return EVAL_RAISE();
  KosEvalState_PENDING_VALUE(es) = settled;
  return EVAL_REPROLOGUE();
}


/* Map 0/-1 int result (push, binary op) to KosOpResult. */
static inline KosOpResult kos_op_result_from_push_int(int r) {
  return (r < 0) ? EVAL_RAISE() : EVAL_NORMAL();
}


/* Push val without auto-await (plain stack push only). */
static inline KosOpResult kos_op_result_PUSH(
    KosEvalState *es,
    PyObject *val) {

  if (unlikely(! val))
    return EVAL_RAISE();

  return kos_op_result_from_push_int(KosEvalState_StackPUSH(es, val));
}


/* KosOpResult variant of kos_op_outcome_PUSH_OR_AWAIT. */
static inline KosOpResult kos_op_result_PUSH_OR_AWAIT(
    KosEvalState *es,
    PyObject *val) {

  if (unlikely(! val))
    return EVAL_RAISE();

  return push_or_await(es, val, 0, 1);
}


// The end.
