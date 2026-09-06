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
  koskari/lib/eval/eval_state_python_bridge.c

  Await/push_or_await, callable install, iter next, and EvaluatorToken;
  included into eval.c.

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
   Push val, or if val is awaitable run eager_step and either push the
   result or suspend (KOS_RES_AWAIT). Use only for (1) the result of a
   call (CALL opcode: callable returned a coroutine) and (2) the AWAIT
   opcode (explicitly awaiting TOS). All other paths must use KosEvalState_StackPUSH
   so we do not auto-await values that are merely loaded or computed.

   CALL* bytecode uses push_or_await_fused instead: same logic plus an optional
   POP,1 peephole so KosEvalState_PushOrAwait and this function stay free of that
   cost on every non-opcode caller (e.g. defer_drive, KosEvalState_Call).

   val is consumed (new reference).

   :return: EVAL_NORMAL, EVAL_AWAIT, or EVAL_RAISE
*/
static KosOpResult push_or_await_plain(
    KosEvalState *es,
    PyObject *val,
    int await_defer) {

  /* Awaitability gate first: a type-level probe (two loads), so the
     dominant non-awaitable result skips everything below -- including
     the Defer subtype walk, which is sound to hoist because KosDefer
     defines am_await and therefore always passes this gate. */
  PyTypeObject *type = Py_TYPE(val);
  SAYF("[eval] push_or_await entry is_awaitable=%d\n",
       (type->tp_as_async && type->tp_as_async->am_await) ? 1 : 0);
  if (! type->tp_as_async || ! type->tp_as_async->am_await) {
    return (KosEvalState_StackPUSH(es, val) < 0) ? EVAL_RAISE() : EVAL_NORMAL();
  }

  /* Defer is not auto-awaited; push as plain value (explicit AWAIT drives it). */
  if (! await_defer && KosDefer_Check(val)) {
    return (KosEvalState_StackPUSH(es, val) < 0) ? EVAL_RAISE() : EVAL_NORMAL();
  }

  /* asyncio futures are handles; implicit sites push them plain. */
  if (! await_defer) {
    int isfut = kos_isfuture(val);
    if (isfut < 0) {
      Py_DECREF(val);
      return EVAL_RAISE();
    }
    if (isfut) {
      return (KosEvalState_StackPUSH(es, val) < 0) ? EVAL_RAISE() : EVAL_NORMAL();
    }
  }

  int suspended = 0;
  PyObject *yielded = NULL;
  PyObject *result = KosEvalState_EagerStepAwaitable(es, val, &suspended, &yielded);
  Py_DECREF(val);

  if (result) {
    return (KosEvalState_StackPUSH(es, result) < 0) ? EVAL_RAISE() : EVAL_NORMAL();
  }

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

  return push_or_await_plain(es, val, 0);
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


/**
   Opcode-only variant of push_or_await: same semantics, plus peephole elision
   when the next bytecode is POP with argument 1 (discard TOS). In that case
   the value is released and the frame PC is advanced past that POP without a
   stack push. Used only from CALL* handlers where the frame PC is already the
   next instruction index after fetch. Do not use from defer_drive or other
   paths that pop the stack in C before the next opcode — use
   KosEvalState_PushOrAwait.

   val is consumed (new reference).

   :return: EVAL_NORMAL, EVAL_AWAIT, or EVAL_RAISE
*/

#define OP_TRY_FUSE_POP1_AFTER_PUSH(es, val)                                  \
  do {                                                                        \
    uint32_t opfuse_pc = KosEvalState_PC(es);                                 \
    KosOp *opfuse_nx = &KosEvalState_OPS(es)[opfuse_pc];                      \
    if (opfuse_nx->op == KOS_OP_POP && opfuse_nx->arg == 1) {                 \
      Py_DECREF(val);                                                         \
      KosEvalState_SET_PC(es, opfuse_pc + 1u);                                \
      return EVAL_NORMAL();                                                   \
    }                                                                         \
  } while (0)


static inline KosOpResult push_or_await_fused(
    KosEvalState *es,
    PyObject *val) {

  /* Awaitability gate first: a type-level probe (two loads), so the
     dominant non-awaitable result skips everything below -- including
     the Defer subtype walk, which is sound to hoist because KosDefer
     defines am_await and therefore always passes this gate. */
  PyTypeObject *type = Py_TYPE(val);
  SAYF("[eval] push_or_await_fused entry is_awaitable=%d\n",
       (type->tp_as_async && type->tp_as_async->am_await) ? 1 : 0);
  if (! type->tp_as_async || ! type->tp_as_async->am_await) {
    OP_TRY_FUSE_POP1_AFTER_PUSH(es, val);
    return (KosEvalState_StackPUSH(es, val) < 0) ? EVAL_RAISE() : EVAL_NORMAL();
  }

  /* Defer is not auto-awaited; push as plain value (explicit AWAIT drives it). */
  if (KosDefer_Check(val)) {
    OP_TRY_FUSE_POP1_AFTER_PUSH(es, val);
    return (KosEvalState_StackPUSH(es, val) < 0) ? EVAL_RAISE() : EVAL_NORMAL();
  }

  /* asyncio futures are handles; implicit sites push them plain. */
  int isfut = kos_isfuture(val);
  if (isfut < 0) {
    Py_DECREF(val);
    return EVAL_RAISE();
  }
  if (isfut) {
    OP_TRY_FUSE_POP1_AFTER_PUSH(es, val);
    return (KosEvalState_StackPUSH(es, val) < 0) ? EVAL_RAISE() : EVAL_NORMAL();
  }

  int suspended = 0;
  PyObject *yielded = NULL;
  PyObject *result = KosEvalState_EagerStepAwaitable(es, val, &suspended, &yielded);
  Py_DECREF(val);

  if (result) {
    OP_TRY_FUSE_POP1_AFTER_PUSH(es, result);
    return (KosEvalState_StackPUSH(es, result) < 0) ? EVAL_RAISE() : EVAL_NORMAL();
  }

  if (suspended && yielded) {
    KosEvalState_MarkSuspendedNotRunning(es);
    SAYF("[eval] push_or_await_fused set KOS_RES_AWAIT (suspended=%d yielded=%d)\n",
         suspended, yielded ? 1 : 0);
    return EVAL_AWAIT(yielded);
  }

  return EVAL_RAISE();
}

#undef OP_TRY_FUSE_POP1_AFTER_PUSH


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

  return push_or_await_fused(es, val);
}


/**
  Prepare frame stack for a KosCallable install per wants_frame / tail_call.
  Inlines effective wants_frame resolution (hook vs static flag).

  A callable that wants no frame leaves the frame stack untouched, tail
  call or not. It either delivers a value onto the caller's stack, and
  the RETURN that follows every TAIL_CALL then retires the frame, or it
  forwards the call with the tail flag intact to a target whose own
  prep reuses this same frame. Popping the caller here would make that
  forwarded prep act on the caller's parent instead.
*/
static inline int do_frame_prep(
    KosEvalState *es,
    PyObject *callable,
    KosCallSlots *slots,
    int tail_call) {

  int wants = KosCallSlots_WantsFrame(slots, callable);

  KosDynFrame *frame = KosEvalState_FRAME(es);

  if (wants && tail_call) {
    /* Uninstall discardable extents left open above the frame before
       the reuse; their POP_* rows are unreachable on the tail path. */
    if (kos_dyn_teardown_to_frame(es, frame) < 0)
      return -1;
    kos_dynframe_reset_for_tail(frame);
    KosEvalState_CODE(es) = NULL;
    KosEvalState_NAMESPACE(es) = NULL;
    KosEvalState_BLOCK(es) = &KosDynFrame_STACK(frame);
    (es)->pc = 0;
    (es)->acting_pc = 0;
    return 0;
  }

  if (wants && ! tail_call) {
    if (kos_dyn_push_frame_empty(es) == NULL)
      return -1;
    return 0;
  }

  return 0;
}


/**
   The result of an install slot that ran on a prepared frame: a frame
   the slot filled is a callee entry, and so a cooperation point
   (kos_call_installed); a slot that wanted no frame delivered a value
   and its result stands
*/
static inline KosOpResult kos_call_slot_result(
    KosEvalState *es,
    KosCallSlots *slots,
    PyObject *callable,
    KosOpResult r) {

  if (r.kind == KOS_RES_NORMAL && KosCallSlots_WantsFrame(slots, callable))
    return kos_call_installed(es);
  return r;
}


/**
   Tail-call preamble for a plain-Python callable: pop the frame when it
   is dyn_top, or defer delivery to a normal RETURN when extent nodes are
   still open above it.  Sets *tail_defer_return accordingly.

   :return: 0 on success, -1 on error
*/
static int tail_prep_py_call(
    KosEvalState *es,
    int tail_call,
    int *tail_defer_return) {

  *tail_defer_return = 0;
  if (! tail_call)
    return 0;

  {
    KosDynFrame *frame = KosEvalState_FRAME(es);
    KosDynNode *const parent = KosDynNode_PARENT((KosDynNode *) frame);
    if (! parent)
      return 0;

    if (KosEvalState_DYN_TOP(es) == (KosDynNode *) frame)
      return kos_dyn_pop_frame(es);
  }

  /* Extent nodes are still open above the frame; skip the tail pop and
     deliver the result through a normal RETURN so they unwind properly. */
  *tail_defer_return = 1;
  return 0;
}


/**
   Deliver a plain-Python call result: push-or-await, then map tail-call
   completion to RETURN (deferred) or CONTROL_RETURNED.  result consumed.
*/
static KosOpResult deliver_py_call_result(
    KosEvalState *es,
    PyObject *result,
    int tail_call,
    int tail_defer_return) {

  KosOpResult por = push_or_await_plain(es, result, 0);
  if (por.kind == KOS_RES_NORMAL && tail_call) {
    if (tail_defer_return) {
      PyObject *rv = KosEvalState_StackPopRef(es);
      if (! rv)
        return EVAL_RAISE();
      return EVAL_RETURN(rv);
    }
    return EVAL_CONTROL_RETURNED();
  }
  return por;
}


static inline KosOpResult kos_call_invoke_body(
    KosEvalState *es,
    KosSegment **caller_slot,
    size_t window,
    PyObject *callable,
    PyObject *const *argv,
    Py_ssize_t nargs,
    PyObject *kwnames,
    unsigned flags);


KosOpResult KosEvalState_Call(
    KosEvalState *es,
    PyObject *callable,
    PyObject *const *args,
    Py_ssize_t nargs,
    PyObject *kwnames,
    int tail_call) {

  KosCallSlots *slots;

  /* Exact simple-arity KosFunction: the shared call core, whose
     parked-frame reuse the legacy install path lacks. */
  if (Py_TYPE(callable) == KOS_EVALUATED_PYTYPE(KosFunctionType)
      && ! tail_call && ! kwnames
      && nargs + 1 == KosFunction_SIMPLE_ARITY1(callable)
      && KosFunction_BIND_SELF_STATE(callable) == 2) {
    KosSegment *dummy = NULL;
    KosOpResult r = kos_call_invoke_body(es, &dummy, 0, callable, args,
                                         nargs, kwnames, 0);
    if (r.kind != KOS_RES_NORMAL)
      return r;
    if (KosEvalState_PLAN(es))
      return EVAL_REPROLOGUE();
    if (KosEvalState_FRAME(es)
        && KosDynFrame_RESUME_FN(KosEvalState_FRAME(es)))
      return EVAL_REPROLOGUE();
    return EVAL_NORMAL();
  }

  if (! KosCallable_CheckSlots(callable, &slots)) {
    if (KosInstanceCall_Check(callable)) {
      /* Instance with a KosCallable __call__: dispatch the unbound __call__
         with self injected as the first positional -- no bound-method,
         transient-string, or descriptor-bind allocation. */
      PyObject *call_fn = kos_instance_call_target(callable);
      if (call_fn) {
        Py_ssize_t nkw = kwnames ? PyTuple_GET_SIZE(kwnames) : 0;
        Py_ssize_t ntotal = nargs + nkw;
        PyObject *small[8];
        PyObject **inj = small;
        if (ntotal + 1 > (Py_ssize_t) (sizeof(small) / sizeof(small[0]))) {
          inj = PyMem_Malloc((size_t) (ntotal + 1) * sizeof(PyObject *));
          if (! inj)
            return EVAL_RAISE();
        }
        inj[0] = callable;
        if (ntotal)
          memcpy(inj + 1, args, (size_t) ntotal * sizeof(PyObject *));
        KosOpResult r = KosEvalState_Call(es, call_fn, inj, nargs + 1,
                                          kwnames, tail_call);
        if (inj != small)
          PyMem_Free(inj);
        return r;
      }
    }
    int tail_defer_return = 0;
    if (tail_prep_py_call(es, tail_call, &tail_defer_return) < 0)
      return EVAL_RAISE();
    /* nargs matches KosCallable_Call: positional count only. PEP 590:
       keyword values follow args[nargs .. nargs + nkw - 1]. */
    PyObject *result = (nargs == 0 && ! kwnames)
        ? PyObject_CallNoArgs(callable)
        : PyObject_Vectorcall(callable, args, (size_t) nargs, kwnames);
    if (! result)
      return EVAL_RAISE();
    return deliver_py_call_result(es, result, tail_call, tail_defer_return);
  }

  if (do_frame_prep(es, callable, slots, tail_call) < 0)
    return EVAL_RAISE();

  return kos_call_slot_result(
      es, slots, callable,
      KosCallable_CallSlots(es, slots, callable, args, nargs, kwnames,
                            tail_call));
}


/**
   A CPS entry's value-in-hand outcome: settled, report RETURN with
   val and arm nothing -- the calling resume tail-calls resume with
   it; not yet settled, settle one step through KosEvalState_SettleStage
   and arm resume on caller for the trip that delivers it. val is
   consumed.
*/
static KosOpResult kos_entry_value(
    KosEvalState *es,
    KosDynFrame *caller,
    PyObject *val,
    KosResumeFn resume,
    void *state) {

  switch (KosEval_ResultIsValue(val)) {
    case 1:
      return EVAL_RETURN(val);

    case 0: {
      KosOpResult pr = KosEvalState_SettleStage(es, val);
      if (pr.kind == KOS_RES_REPROLOGUE || pr.kind == KOS_RES_AWAIT) {
        KosDynFrame_RESUME_FN(caller) = (void *) resume;
        KosDynFrame_RESUME_STATE(caller) = state;
      }
      return pr;
    }

    default:
      Py_DECREF(val);
      return EVAL_RAISE();
  }
}


KosOpResult KosEvalState_CallResult(
    KosEvalState *es,
    PyObject *callable,
    PyObject *const *args,
    Py_ssize_t nargs,
    PyObject *kwnames,
    int tail_call,
    KosResumeFn resume,
    void *state) {

  KosDynFrame *caller = KosEvalState_FRAME(es);
  KosOpResult r;

#if KOS_EVAL_SAFETYNET
  if (unlikely(tail_call && resume)) {
    PyErr_SetString(PyExc_SystemError,
                    "tail call with a resume: no caller frame to resume");
    return EVAL_RAISE();
  }
#endif

  r = KosEvalState_Call(es, callable, args, nargs, kwnames, tail_call);

  if (! resume)
    return r;

  switch (r.kind) {
    case KOS_RES_NORMAL:
      if (KosEvalState_FRAME(es) == caller) {
        /* Nothing was installed: the callable ran to a value that
           Call landed on the caller's own stack (a plain callable's
           result, a generator function's generator). Take it back
           and report it in hand. */
        PyObject *val = KosEvalState_StackPopRef(es);
        if (unlikely(! val))
          return EVAL_RAISE();
        return kos_entry_value(es, caller, val, resume, state);
      }
      /* fall through: callee installed and current */
    case KOS_RES_REPROLOGUE:
      /* callee installed above caller, or a plan armed by the install;
         caller's own frame is unchanged and still reachable -- its
         eventual return delivers here. */
      KosDynFrame_RESUME_FN(caller) = (void *) resume;
      KosDynFrame_RESUME_STATE(caller) = state;
      return r;

    case KOS_RES_AWAIT:
      /* the call's own result needed awaiting; the AWAITED revive
         delivers into resume once it settles. */
      KosDynFrame_RESUME_FN(caller) = (void *) resume;
      KosDynFrame_RESUME_STATE(caller) = state;
      return r;

    case KOS_RES_RETURN:
      return kos_entry_value(es, caller, r.val, resume, state);

    default:
      return r;  /* RAISE / FATAL: relay untouched */
  }
}


/* === kos_call_invoke: the shared call core (call-unification Phase 2) ===

   The entry for the CALL_* opcode prologues: kos_call_invoke inlines
   into each handler (window math and self-arg prepend), delegating to
   kos_call_invoke_body, which picks the out-of-line class dispatch
   specialised for the handler's flags word.  argv is
   always a BORROWED read vector (vectorcall convention).  With
   KOS_CALL_ARGV_WINDOW, argv additionally names the caller's stack tail
   (nargs + kwname values, plus the callable itself under
   KOS_CALL_WINDOW_CALLABLE); the core owns dropping that window from
   the caller's block, sequenced per install class:

   - classes that deliver a result onto the CALLER's block (generator
     agen creation, plain-Python push-or-await, and every legacy install
     that may) drop the window BEFORE the delivery;
   - the frame-install class delivers nothing onto the caller's block,
     so it drops the window LAST, keeping the callable and args alive
     through the whole install with no extra refcounting.

   Legacy classes (KosCallables other than exact KosFunction, and
   instance-call injection) preserve today's steal-first ordering by
   spilling the window into an owned vector before delegating to
   KosEvalState_Call.  Tail calls ride the same entry under
   KOS_CALL_TAIL (see the flag notes below). */

#define KOS_CALL_PLAIN            0x0u
#define KOS_CALL_ARGV_WINDOW      0x1u
#define KOS_CALL_WINDOW_CALLABLE  0x2u
/* One extra window slot beneath argv (an on-stack receiver): dropped
   with the window, never the call target.  The receiver reaches the
   callee only via self_arg injection. */
#define KOS_CALL_WINDOW_UNDER     0x4u
/* Tail call: the caller's frame is reused.  Exact non-yielding
   KosFunctions take the bind-before-reset subpath (the frame reset's
   block-clear IS the window drop); everything else spills and keeps
   KosEvalState_Call's tail semantics. */
#define KOS_CALL_TAIL             0x8u
/* Internal, set by kos_call_invoke when self_arg was injected: argv is
   the prepend buffer, not the block tail, so the steal-bind fast path
   (which moves references directly off the block) must not run. */
#define KOS_CALL_ARGV_PREPENDED   0x10u
/* Caller transfers its callable reference to the core, which consumes
   it on every path: an install that stamps the frame's FN tag takes the
   reference (no fresh incref), and every other exit decrefs.  Handlers
   pass their fast-slot / resolver reference this way instead of
   dropping it after the invoke, saving a refcount round trip per call
   on the install paths.  The flag is a per-site literal, so the checks
   fold away in the inlined prologue. */
#define KOS_CALL_CALLABLE_OWNED   0x20u

#define KOS_CALL_SPILL_FAST 16


/**
   True when the live frame's namespace is provably private, so a
   self-tail call may rebind it in place.  Mirrors the privacy test in
   KosFunction_TryParkFrame: a captured, revealed, or dynamically
   extended namespace stays live-visible and takes the ordinary
   namespace rebuild instead.
*/
static inline int kos_tail_ns_reusable(PyObject *ns) {
  return ns
      && KosNamespace_CheckExact(ns)
      && Py_REFCNT(ns) == 1
      && ! PyObject_GC_IsTracked(ns)
      && KosNamespace_BINDS(ns) == NULL;
}


/* The class dispatch proper.  Always inlined into the per-flag bodies
   below, each of which passes a literal flags word, so every flag test
   in here folds away and each body carries only its own arms. */
KOS_OP_INLINE KosOpResult kos_call_invoke_impl(
        KosEvalState *es,
        KosSegment **caller_slot,
        size_t window,
        PyObject *callable,
        PyObject *const *argv,
        Py_ssize_t nargs,
        PyObject *kwnames,
        unsigned flags) {

  Py_ssize_t nkw = kwnames ? PyTuple_GET_SIZE(kwnames) : 0;

  /* Class: exact KosFunction, tail position.  Bind-before-reset: the
     namespace is prepared FROM the still-intact window, then the frame
     reset's block-clear performs the window drop.  Requires the window
     to live in the reused frame's own block and non-yielding code;
     anything else falls to the legacy spill below (which preserves
     KosEvalState_Call's tail semantics exactly). */
  if (unlikely(flags & KOS_CALL_TAIL)) {
    KosDynFrame *frame = KosEvalState_FRAME(es);

    if (Py_TYPE(callable) == KOS_EVALUATED_PYTYPE(KosFunctionType)
        && frame
        && caller_slot == &KosDynFrame_STACK(frame)) {
      KosCode *code = (KosCode *) KosFunction_CODE(callable);

      if (! (code && KosCode_YIELDS(code))) {
        PyObject *frame_ns;

        /* Discardable extents open above the frame (a tail call from
           inside let / loop / foreach) uninstall now: the reused frame
           never reaches their POP_* rows.  The window is unaffected --
           the guard above pins it to the frame's own block. */
        if (unlikely(kos_dyn_teardown_to_frame(es, frame) < 0)) {
          if (window)
            kos_stack_drop_noerr_fast(caller_slot, window);
          if (flags & KOS_CALL_CALLABLE_OWNED)
            Py_DECREF(callable);
          return EVAL_RAISE();
        }

        /* Self-tail at simple arity with a provably private namespace:
           rebind the live frame's own namespace in place -- release the
           raw locals, steal the arg span back into slots 0..nargs-1, and
           clear the block residue.  No namespace build, no frame reset;
           callable == FN means the frame's own ref keeps callable (and
           code) alive across the block clear. */
        if (callable == (PyObject *) KosDynFrame_FN(frame)
            && ! kwnames
            && ! (flags & KOS_CALL_ARGV_PREPENDED)
            && nargs + 1 == KosFunction_SIMPLE_ARITY1(callable)
            && KosFunction_BIND_SELF_STATE(callable) == 2
            && ! KosDynFrame_RESUME_FN(frame)
            && kos_tail_ns_reusable(KosDynFrame_NS(frame))) {
          PyObject *ns = KosDynFrame_NS(frame);

          /* Verify the arg span BEFORE releasing the raw locals: a
             steal underrun after the clear would leave the still
             installed frame with a wiped namespace.  Impossible by
             classification; if it ever fires, the raise (delegated to
             the formals checker so the report has one home) happens
             with the namespace untouched. */
          if (unlikely(nargs
                       && (! *caller_slot
                           || (size_t) nargs
                               > KosSegment_LEN(*caller_slot)))) {
            (void) KosFormals_SimpleStealApplyBlock(ns, *caller_slot,
                                                    nargs);
            if (window)
              kos_stack_drop_noerr_fast(caller_slot, window);
            if (flags & KOS_CALL_CALLABLE_OWNED)
              Py_DECREF(callable);
            return EVAL_RAISE();
          }

          KosNamespace_ClearRawFast(ns);
          (void) KosFormals_SimpleStealApplyBlock(ns, *caller_slot, nargs);
          KosStack_Clear(&KosDynFrame_STACK(frame));
          KOS_TRACE_EVENTS_INC(KOS_TE_CALL_TAIL_INPLACE);
          es->pc = 0;
          es->acting_pc = 0;
          /* the frame's FN reference keeps callable alive */
          if (flags & KOS_CALL_CALLABLE_OWNED)
            Py_DECREF(callable);
          return kos_call_installed(es);
        }

        if (likely(! kwnames
                   && nargs + 1 == KosFunction_SIMPLE_ARITY1(callable))) {
          frame_ns = KosFunction_SimpleCallNamespaceFromVector(
              callable, argv, nargs);
        } else {
          frame_ns = KosFunction_PrepareCallNamespaceFromVector(
              callable, argv, nargs, kwnames);
        }
        if (unlikely(! frame_ns)) {
          if (window)
            kos_stack_drop_noerr_fast(caller_slot, window);
          if (flags & KOS_CALL_CALLABLE_OWNED)
            Py_DECREF(callable);
          return EVAL_RAISE();
        }

        /* The callable is window-resident: the reset's block-clear
           releases it.  Hold it (and thereby code) across the reset,
           then transfer the reference into the FN tag -- the reset
           cleared the previous one -- so a following self-tail can
           rebind in place and release can attempt the park.  An owned
           caller reference serves as the hold and transfers directly. */
        if (! (flags & KOS_CALL_CALLABLE_OWNED))
          Py_INCREF(callable);
        kos_dynframe_reset_for_tail(frame);
        (void) KosEvalState_SetFrameCodeAndNamespace(es, frame, code,
                                                     frame_ns);
        KosDynFrame_FN(frame) = callable;
        es->pc = 0;
        es->acting_pc = 0;
        return kos_call_installed(es);
      }
    }
    goto legacy;
  }

  /* Class: exact KosFunction -- the dominant call target. */
  if (likely(Py_TYPE(callable) == KOS_EVALUATED_PYTYPE(KosFunctionType))) {
    KosCode *code = (KosCode *) KosFunction_CODE(callable);
    PyObject *frame_ns;

    /* A generator function call NEVER begins evaluating its code: it
       binds the arguments and completes immediately with the new
       AsyncGenerator as the call's value.  This gate stands ahead of
       every install arm below, so none of them can see yielding code
       no matter how the call classifies.  The agen lands on the
       caller's block, so the window drops first; NewFromCodeAndNs
       steals frame_ns. */
    if (unlikely(code && KosCode_YIELDS(code))) {
      if (! kwnames
          && nargs + 1 == KosFunction_SIMPLE_ARITY1(callable)
          && KosFunction_BIND_SELF_STATE(callable) == 2) {
        frame_ns = KosFunction_SimpleCallNamespaceFromVector(
            callable, argv, nargs);
      } else {
        frame_ns = KosFunction_PrepareCallNamespaceFromVector(
            callable, argv, nargs, kwnames);
      }
      if (unlikely(! frame_ns)) {
        if (window)
          kos_stack_drop_noerr_fast(caller_slot, window);
        if (flags & KOS_CALL_CALLABLE_OWNED)
          Py_DECREF(callable);
        return EVAL_RAISE();
      }
      {
        PyObject *agen = KosAsyncGen_NewFromCodeAndNs(code, frame_ns);
        if (window)
          kos_stack_drop_noerr_fast(caller_slot, window);
        if (flags & KOS_CALL_CALLABLE_OWNED)
          Py_DECREF(callable);
        if (unlikely(! agen))
          return EVAL_RAISE();
        return KosEvalState_StackPUSH(es, agen) < 0
            ? EVAL_RAISE() : EVAL_NORMAL();
      }
    }

    if (likely(! kwnames
               && nargs + 1 == KosFunction_SIMPLE_ARITY1(callable))) {
      /* Simple call: exact arity against plain positional formals on
         non-yielding code (classified at construction, and the
         generator gate above stands regardless).  Binds by slot
         index; the general formals machine is bypassed.  Any
         mismatch falls through to the general path, which owns the
         arity error messages.

         When argv is the block tail (window, no self-arg prepend) and
         the function has no self-bind, the args steal-move off the
         block into the namespace -- the stolen span leaves the window,
         so only the residue (a WINDOW_CALLABLE slot) drops after the
         install. */
      if (likely((flags & KOS_CALL_ARGV_WINDOW)
                 && ! (flags & KOS_CALL_ARGV_PREPENDED)
                 && KosFunction_BIND_SELF_STATE(callable) == 2)) {
        KosDynFrame *cached = KosFunction_CACHED_FRAME(callable);

        if (likely(cached)) {
          /* Cache hit: steal-bind into the parked namespace first -- on
             failure nothing is stolen and the frame stays parked. */
          if (unlikely(KosFormals_SimpleStealApplyBlock(
                  KosDynFrame_NS(cached), *caller_slot, nargs) < 0)) {
            if (window)
              kos_stack_drop_noerr_fast(caller_slot, window);
            if (flags & KOS_CALL_CALLABLE_OWNED)
              Py_DECREF(callable);
            return EVAL_RAISE();
          }
          KosFunction_CACHED_FRAME(callable) = NULL;
          window -= (size_t) nargs;
          kos_dyn_push_ready_frame(es, cached);
          if (! (flags & KOS_CALL_CALLABLE_OWNED))
            Py_INCREF(callable);
          KosDynFrame_FN(cached) = callable;
          KOS_TRACE_EVENTS_INC(KOS_TE_CALL_FRAME_CACHED);
          if (window)
            kos_stack_drop_noerr_fast(caller_slot, window);
          return kos_call_installed(es);
        }

        frame_ns = KosFunction_SimpleCallNamespaceStealBlock(
            callable, *caller_slot, nargs);
        if (unlikely(! frame_ns)) {
          /* nothing stolen; the whole window is still on the block */
          if (window)
            kos_stack_drop_noerr_fast(caller_slot, window);
          if (flags & KOS_CALL_CALLABLE_OWNED)
            Py_DECREF(callable);
          return EVAL_RAISE();
        }
        window -= (size_t) nargs;

        /* Cache miss: install as usual, then tag the frame for the
           park attempt at release. */
        {
          KosDynFrame *frame = kos_dyn_push_frame(es, code, frame_ns);
          if (unlikely(! frame)) {
            if (window)
              kos_stack_drop_noerr_fast(caller_slot, window);
            if (flags & KOS_CALL_CALLABLE_OWNED)
              Py_DECREF(callable);
            return EVAL_RAISE();
          }
          if (! (flags & KOS_CALL_CALLABLE_OWNED))
            Py_INCREF(callable);
          KosDynFrame_FN(frame) = callable;
          KOS_TRACE_EVENTS_INC(KOS_TE_CALL_SIMPLE_STEAL);
          if (window)
            kos_stack_drop_noerr_fast(caller_slot, window);
          return kos_call_installed(es);
        }
      }

      /* Plain vector (no stack window), no self-bind: the parked
         frame binds directly from argv -- references taken, slots
         filled, ready-push. A miss installs and tags the frame so its
         release parks it. */
      if (likely(! (flags & KOS_CALL_ARGV_WINDOW)
                 && KosFunction_BIND_SELF_STATE(callable) == 2)) {
        KosDynFrame *cached = KosFunction_CACHED_FRAME(callable);

        if (likely(cached)) {
          for (Py_ssize_t i = 0; i < nargs; i++)
            Py_INCREF(argv[i]);
          (void) KosFormals_SimpleStealApply(
              KosDynFrame_NS(cached), argv, nargs);
          KosFunction_CACHED_FRAME(callable) = NULL;
          kos_dyn_push_ready_frame(es, cached);
          if (! (flags & KOS_CALL_CALLABLE_OWNED))
            Py_INCREF(callable);
          KosDynFrame_FN(cached) = callable;
          KOS_TRACE_EVENTS_INC(KOS_TE_CALL_FRAME_CACHED);
          return kos_call_installed(es);
        }

        frame_ns = KosFunction_SimpleCallNamespaceFromVector(
            callable, argv, nargs);
        if (unlikely(! frame_ns)) {
          if (flags & KOS_CALL_CALLABLE_OWNED)
            Py_DECREF(callable);
          return EVAL_RAISE();
        }
        KOS_TRACE_EVENTS_INC(KOS_TE_CALL_SIMPLE_VECTOR);
        {
          KosDynFrame *frame = kos_dyn_push_frame(es, code, frame_ns);
          if (unlikely(! frame)) {
            if (flags & KOS_CALL_CALLABLE_OWNED)
              Py_DECREF(callable);
            return EVAL_RAISE();
          }
          if (! (flags & KOS_CALL_CALLABLE_OWNED))
            Py_INCREF(callable);
          KosDynFrame_FN(frame) = callable;
          return kos_call_installed(es);
        }
      }

      frame_ns = KosFunction_SimpleCallNamespaceFromVector(
          callable, argv, nargs);
      if (unlikely(! frame_ns)) {
        if (window)
          kos_stack_drop_noerr_fast(caller_slot, window);
        if (flags & KOS_CALL_CALLABLE_OWNED)
          Py_DECREF(callable);
        return EVAL_RAISE();
      }
      KOS_TRACE_EVENTS_INC(KOS_TE_CALL_SIMPLE_VECTOR);
      goto fn_frame_install;
    }

    /* General path: the generator gate above already peeled yielding
       code off, so this always installs a frame. */
    frame_ns = KosFunction_PrepareCallNamespaceFromVector(
        callable, argv, nargs, kwnames);

    if (unlikely(! frame_ns)) {
      if (window)
        kos_stack_drop_noerr_fast(caller_slot, window);
      if (flags & KOS_CALL_CALLABLE_OWNED)
        Py_DECREF(callable);
      return EVAL_RAISE();
    }

fn_frame_install:

    /* Frame install: nothing lands on the caller's block, so the drop
       runs last and the window keeps callable and args alive for the
       whole install.  kos_dyn_push_frame installs code and frame_ns in
       one pass (consuming frame_ns even on failure), so the live
       registers are loaded once, with their final values. */
    {
      KosDynFrame *frame = kos_dyn_push_frame(es, code, frame_ns);
      if (unlikely(! frame)) {
        if (window)
          kos_stack_drop_noerr_fast(caller_slot, window);
        if (flags & KOS_CALL_CALLABLE_OWNED)
          Py_DECREF(callable);
        return EVAL_RAISE();
      }
      if (window)
        kos_stack_drop_noerr_fast(caller_slot, window);
      if (flags & KOS_CALL_CALLABLE_OWNED)
        Py_DECREF(callable);
      return kos_call_installed(es);
    }
  }

  /* Class: plain Python callable -- vectorcall reading argv in place;
     the result lands on the caller's block, so the window drops first. */
  if (! KosCallable_Check(callable) && ! KosInstanceCall_Check(callable)) {
    PyObject *result = (nargs == 0 && ! kwnames)
        ? PyObject_CallNoArgs(callable)
        : PyObject_Vectorcall(callable, argv, (size_t) nargs, kwnames);
    if (window)
      kos_stack_drop_noerr_fast(caller_slot, window);
    if (flags & KOS_CALL_CALLABLE_OWNED)
      Py_DECREF(callable);
    return kos_op_result_PUSH_OR_AWAIT(es, result);
  }

  /* Legacy classes: preserve steal-first ordering.  Spill the window
     into an owned vector (refs transfer out of the block), delegate,
     release.  Tail flows land here too (py tails need
     tail_prep_py_call; other-callable and deferred tails keep
     KosEvalState_Call's semantics). */
legacy:
  if (window) {
    PyObject *small[KOS_CALL_SPILL_FAST];
    PyObject **vals = small;
    KosOpResult r;

    if (window > KOS_CALL_SPILL_FAST) {
      vals = PyMem_Malloc(window * sizeof(PyObject *));
      if (unlikely(! vals)) {
        if (flags & KOS_CALL_CALLABLE_OWNED)
          Py_DECREF(callable);
        return EVAL_RAISE();
      }
    }
    if (unlikely(KosEvalState_StackPopTailPtr(es, window, vals) < 0)) {
      if (vals != small)
        PyMem_Free(vals);
      if (flags & KOS_CALL_CALLABLE_OWNED)
        Py_DECREF(callable);
      return EVAL_RAISE();
    }

    /* vals is the ownership ledger for the spilled window entries and
       the vector the call reads: a tail call pops the caller's frame
       before the callee runs, and the block storage argv points into
       goes with it. A prepend buffer is the handler's own local, so it
       stays valid, and the objects it names are held by vals. */
    {
      PyObject *target;
      PyObject *const *call_argv;
      if (flags & KOS_CALL_WINDOW_CALLABLE) {
        target = vals[0];
        call_argv = vals + 1;
      } else {
        target = callable;
        /* whatever sits below the arguments in the window (a receiver
           slot) is skipped by size, so the body need not read the
           flag that put it there */
        call_argv = (flags & KOS_CALL_ARGV_PREPENDED)
            ? argv
            : vals + (window - (size_t) (nargs + nkw));
      }
      r = KosEvalState_Call(es, target,
                            (nargs + nkw) ? call_argv : NULL,
                            nargs, kwnames,
                            (flags & KOS_CALL_TAIL) ? 1 : 0);
    }

    for (size_t i = 0; i < window; i++)
      Py_DECREF(vals[i]);
    if (vals != small)
      PyMem_Free(vals);
    if (flags & KOS_CALL_CALLABLE_OWNED)
      Py_DECREF(callable);
    return r;
  }

  {
    KosOpResult r = KosEvalState_Call(es, callable, argv, nargs, kwnames,
                                      (flags & KOS_CALL_TAIL) ? 1 : 0);
    if (flags & KOS_CALL_CALLABLE_OWNED)
      Py_DECREF(callable);
    return r;
  }
}


/* One out-of-line body per flags word the call sites use.  A handler
   passes its flags as a literal, so the dispatcher in
   kos_call_invoke_body folds to a direct call of the matching body,
   and the body itself is the class dispatch specialised for that word.
   KOS_CALL_WINDOW_UNDER shapes only the window arithmetic in
   kos_call_invoke and is never read by the body, so a word carrying
   it shares the body of the word without it.  Any other word takes
   the generic body, which keeps flags live. */
#define KOS_CALL_BODY_VARIANT(name, FLAGS)                              \
  static KOS_NOINLINE KosOpResult name(                                 \
      KosEvalState *es,                                                 \
      KosSegment **caller_slot,                                         \
      size_t window,                                                    \
      PyObject *callable,                                               \
      PyObject *const *argv,                                            \
      Py_ssize_t nargs,                                                 \
      PyObject *kwnames) {                                              \
    return kos_call_invoke_impl(es, caller_slot, window, callable,      \
                                argv, nargs, kwnames, (FLAGS));         \
  }

KOS_CALL_BODY_VARIANT(kos_call_body_plain, KOS_CALL_PLAIN)
KOS_CALL_BODY_VARIANT(kos_call_body_window_callable,
                      KOS_CALL_ARGV_WINDOW | KOS_CALL_WINDOW_CALLABLE)
KOS_CALL_BODY_VARIANT(kos_call_body_window_callable_tail,
                      KOS_CALL_ARGV_WINDOW | KOS_CALL_WINDOW_CALLABLE
                      | KOS_CALL_TAIL)
KOS_CALL_BODY_VARIANT(kos_call_body_window_owned,
                      KOS_CALL_ARGV_WINDOW | KOS_CALL_CALLABLE_OWNED)
KOS_CALL_BODY_VARIANT(kos_call_body_window_owned_prepended,
                      KOS_CALL_ARGV_WINDOW | KOS_CALL_CALLABLE_OWNED
                      | KOS_CALL_ARGV_PREPENDED)
KOS_CALL_BODY_VARIANT(kos_call_body_window_owned_tail,
                      KOS_CALL_ARGV_WINDOW | KOS_CALL_TAIL
                      | KOS_CALL_CALLABLE_OWNED)
KOS_CALL_BODY_VARIANT(kos_call_body_window_owned_tail_prepended,
                      KOS_CALL_ARGV_WINDOW | KOS_CALL_TAIL
                      | KOS_CALL_CALLABLE_OWNED | KOS_CALL_ARGV_PREPENDED)

#undef KOS_CALL_BODY_VARIANT


static KOS_NOINLINE KosOpResult kos_call_body_any(
        KosEvalState *es,
        KosSegment **caller_slot,
        size_t window,
        PyObject *callable,
        PyObject *const *argv,
        Py_ssize_t nargs,
        PyObject *kwnames,
        unsigned flags) {

  return kos_call_invoke_impl(es, caller_slot, window, callable,
                              argv, nargs, kwnames, flags);
}


static inline KosOpResult kos_call_invoke_body(
        KosEvalState *es,
        KosSegment **caller_slot,
        size_t window,
        PyObject *callable,
        PyObject *const *argv,
        Py_ssize_t nargs,
        PyObject *kwnames,
        unsigned flags) {

  switch (flags) {
    case KOS_CALL_PLAIN:
      return kos_call_body_plain(es, caller_slot, window, callable,
                                 argv, nargs, kwnames);
    case KOS_CALL_ARGV_WINDOW | KOS_CALL_WINDOW_CALLABLE:
      return kos_call_body_window_callable(es, caller_slot, window,
                                           callable, argv, nargs, kwnames);
    case KOS_CALL_ARGV_WINDOW | KOS_CALL_WINDOW_CALLABLE | KOS_CALL_TAIL:
      return kos_call_body_window_callable_tail(es, caller_slot, window,
                                                callable, argv, nargs,
                                                kwnames);
    case KOS_CALL_ARGV_WINDOW | KOS_CALL_CALLABLE_OWNED:
      return kos_call_body_window_owned(es, caller_slot, window,
                                        callable, argv, nargs, kwnames);
    case KOS_CALL_ARGV_WINDOW | KOS_CALL_WINDOW_UNDER
        | KOS_CALL_CALLABLE_OWNED:
      return kos_call_body_window_owned(es, caller_slot, window,
                                        callable, argv, nargs, kwnames);
    case KOS_CALL_ARGV_WINDOW | KOS_CALL_CALLABLE_OWNED
        | KOS_CALL_ARGV_PREPENDED:
      return kos_call_body_window_owned_prepended(es, caller_slot, window,
                                                  callable, argv, nargs,
                                                  kwnames);
    case KOS_CALL_ARGV_WINDOW | KOS_CALL_TAIL | KOS_CALL_CALLABLE_OWNED:
      return kos_call_body_window_owned_tail(es, caller_slot, window,
                                             callable, argv, nargs,
                                             kwnames);
    case KOS_CALL_ARGV_WINDOW | KOS_CALL_WINDOW_UNDER | KOS_CALL_TAIL
        | KOS_CALL_CALLABLE_OWNED:
      return kos_call_body_window_owned_tail(es, caller_slot, window,
                                             callable, argv, nargs,
                                             kwnames);
    case KOS_CALL_ARGV_WINDOW | KOS_CALL_TAIL | KOS_CALL_CALLABLE_OWNED
        | KOS_CALL_ARGV_PREPENDED:
      return kos_call_body_window_owned_tail_prepended(es, caller_slot,
                                                       window, callable,
                                                       argv, nargs,
                                                       kwnames);
    default:
      return kos_call_body_any(es, caller_slot, window, callable,
                               argv, nargs, kwnames, flags);
  }
}


/**
   Install a simple-arity KosFunction call whose receiver arrives out of
   band (unbound method resolution): fast slot 0 binds from self_arg and
   the window's arg span steal-binds into slots 1..nargs, through the
   parked frame when one is available.  The caller has already
   classified the callable -- exact KosFunction, simple arity nargs + 2,
   no self-bind, no kwnames -- and the window holds the args at the
   block tail.  Mirrors the two steal-bind arms of the simple-call
   branch in kos_call_invoke_body.
*/
static KOS_NOINLINE KosOpResult kos_call_invoke_self_steal(
        KosEvalState *es,
        KosSegment **caller_slot,
        size_t window,
        PyObject *callable,
        PyObject *self_arg,
        Py_ssize_t nargs,
        unsigned flags) {

  KosCode *code = (KosCode *) KosFunction_CODE(callable);
  KosDynFrame *cached = KosFunction_CACHED_FRAME(callable);
  PyObject *frame_ns;

  if (likely(cached)) {
    /* Cache hit: steal-bind into the parked namespace first -- on
       failure nothing is stolen and the frame stays parked. */
    if (unlikely(KosFormals_SimpleStealApplySelfBlock(
            KosDynFrame_NS(cached), self_arg, *caller_slot, nargs) < 0)) {
      if (window)
        kos_stack_drop_noerr_fast(caller_slot, window);
      if (flags & KOS_CALL_CALLABLE_OWNED)
        Py_DECREF(callable);
      return EVAL_RAISE();
    }
    KosFunction_CACHED_FRAME(callable) = NULL;
    window -= (size_t) nargs;
    kos_dyn_push_ready_frame(es, cached);
    if (! (flags & KOS_CALL_CALLABLE_OWNED))
      Py_INCREF(callable);
    KosDynFrame_FN(cached) = callable;
    KOS_TRACE_EVENTS_INC(KOS_TE_CALL_FRAME_CACHED);
    if (window)
      kos_stack_drop_noerr_fast(caller_slot, window);
    return kos_call_installed(es);
  }

  frame_ns = KosFunction_SimpleCallNamespaceStealSelfBlock(
      callable, self_arg, *caller_slot, nargs);
  if (unlikely(! frame_ns)) {
    /* nothing stolen; the whole window is still on the block */
    if (window)
      kos_stack_drop_noerr_fast(caller_slot, window);
    if (flags & KOS_CALL_CALLABLE_OWNED)
      Py_DECREF(callable);
    return EVAL_RAISE();
  }
  window -= (size_t) nargs;

  /* Cache miss: install as usual, then tag the frame for the park
     attempt at release. */
  {
    KosDynFrame *frame = kos_dyn_push_frame(es, code, frame_ns);
    if (unlikely(! frame)) {
      if (window)
        kos_stack_drop_noerr_fast(caller_slot, window);
      if (flags & KOS_CALL_CALLABLE_OWNED)
        Py_DECREF(callable);
      return EVAL_RAISE();
    }
    if (! (flags & KOS_CALL_CALLABLE_OWNED))
      Py_INCREF(callable);
    KosDynFrame_FN(frame) = callable;
    KOS_TRACE_EVENTS_INC(KOS_TE_CALL_SIMPLE_STEAL);
    if (window)
      kos_stack_drop_noerr_fast(caller_slot, window);
    return kos_call_installed(es);
  }
}


/**
   Shared call core entry.  self_arg (borrowed, may be NULL) is injected
   as the first positional via a local borrowed-pointer prepend buffer;
   the window accounting is untouched by the injection (self never lives
   on the caller's stack).  An unbound-method call that fits the simple
   steal-bind classification skips the buffer entirely and routes
   through kos_call_invoke_self_steal, keeping the parked-frame path
   reachable.  See the block comment above for the window sequencing
   contract.
*/
static inline KosOpResult kos_call_invoke(
        KosEvalState *es,
        PyObject *callable,
        PyObject *self_arg,
        PyObject *const *argv,
        Py_ssize_t nargs,
        PyObject *kwnames,
        unsigned flags) {

  KosSegment **caller_slot = KosEvalState_BLOCK(es);
  Py_ssize_t nkw = kwnames ? PyTuple_GET_SIZE(kwnames) : 0;
  size_t window = 0;

  if (flags & KOS_CALL_ARGV_WINDOW)
    window = (size_t) (nargs + nkw)
        + ((flags & KOS_CALL_WINDOW_CALLABLE) ? 1u : 0u)
        + ((flags & KOS_CALL_WINDOW_UNDER) ? 1u : 0u);

  if (! self_arg)
    return kos_call_invoke_body(es, caller_slot, window, callable,
                                argv, nargs, kwnames, flags);

  if (likely((flags & KOS_CALL_ARGV_WINDOW))
      && ! (flags & KOS_CALL_TAIL)
      && ! kwnames
      && Py_TYPE(callable) == KOS_EVALUATED_PYTYPE(KosFunctionType)
      && nargs + 2 == KosFunction_SIMPLE_ARITY1(callable)
      /* a generator function never takes a frame-install arm; its
         call constructs the AsyncGenerator through the generator gate
         in kos_call_invoke_body instead */
      && ! KosCode_YIELDS((KosCode *) KosFunction_CODE(callable))
      && KosFunction_BIND_SELF_STATE(callable) == 2)
    return kos_call_invoke_self_steal(es, caller_slot, window, callable,
                                      self_arg, nargs, flags);

  {
    PyObject *pre_small[KOS_CALL_SPILL_FAST];
    PyObject **pre = pre_small;
    Py_ssize_t total = nargs + nkw + 1;
    KosOpResult r;

    if (total > KOS_CALL_SPILL_FAST) {
      pre = PyMem_Malloc((size_t) total * sizeof(PyObject *));
      if (unlikely(! pre)) {
        PyErr_NoMemory();
        if (window)
          kos_stack_drop_noerr_fast(caller_slot, window);
        if (flags & KOS_CALL_CALLABLE_OWNED)
          Py_DECREF(callable);
        return EVAL_RAISE();
      }
    }
    pre[0] = self_arg;
    if (total > 1)
      memcpy(pre + 1, argv, (size_t) (total - 1) * sizeof(PyObject *));

    r = kos_call_invoke_body(es, caller_slot, window, callable,
                             pre, nargs + 1, kwnames,
                             flags | KOS_CALL_ARGV_PREPENDED);
    if (pre != pre_small)
      PyMem_Free(pre);
    return r;
  }
}


KosOpResult KosEvalState_CallArgs(
    KosEvalState *es,
    PyObject *callable,
    PyObject *args_tuple,
    PyObject *kwds_dict,
    int tail_call) {

  KosCallSlots *slots;

  if (! KosCallable_CheckSlots(callable, &slots)) {
    if (KosInstanceCall_Check(callable)) {
      PyObject *call_fn = kos_instance_call_target(callable);
      if (call_fn) {
        /* Prepend self to the positional tuple (the tuple form allocates
           regardless). */
        Py_ssize_t na = args_tuple ? PyTuple_GET_SIZE(args_tuple) : 0;
        PyObject *inj = PyTuple_New(na + 1);
        if (! inj)
          return EVAL_RAISE();
        Py_INCREF(callable);
        PyTuple_SET_ITEM(inj, 0, callable);
        for (Py_ssize_t i = 0; i < na; i++) {
          PyObject *it = PyTuple_GET_ITEM(args_tuple, i);
          Py_INCREF(it);
          PyTuple_SET_ITEM(inj, i + 1, it);
        }
        KosOpResult r = KosEvalState_CallArgs(es, call_fn, inj, kwds_dict,
                                              tail_call);
        Py_DECREF(inj);
        return r;
      }
    }
    int tail_defer_return = 0;
    if (tail_prep_py_call(es, tail_call, &tail_defer_return) < 0)
      return EVAL_RAISE();

    PyObject *result = NULL;

    if (args_tuple) {
      result = PyObject_Call(callable, args_tuple, kwds_dict);

    } else if (kwds_dict) {
      PyObject *empty_args = PyTuple_New(0);
      if (! empty_args)
        return EVAL_RAISE();
      result = PyObject_Call(callable, empty_args, kwds_dict);
      Py_DECREF(empty_args);

    } else {
      result = PyObject_CallNoArgs(callable);
    }

    if (! result)
      return EVAL_RAISE();

    return deliver_py_call_result(es, result, tail_call, tail_defer_return);
  }

  if (do_frame_prep(es, callable, slots, tail_call) < 0)
    return EVAL_RAISE();

  return kos_call_slot_result(
      es, slots, callable,
      KosCallable_CallArgsSlots(es, slots, callable, args_tuple,
                                kwds_dict, tail_call));
}


KosOpResult KosEvalState_CallNoArgs(
    KosEvalState *es,
    PyObject *callable,
    int tail_call) {

  KosCallSlots *slots;

  if (! KosCallable_CheckSlots(callable, &slots)) {
    if (KosInstanceCall_Check(callable)) {
      PyObject *call_fn = kos_instance_call_target(callable);
      if (call_fn) {
        return KosEvalState_Call(es, call_fn, &callable, 1, NULL, tail_call);
      }
    }
    int tail_defer_return = 0;
    if (tail_prep_py_call(es, tail_call, &tail_defer_return) < 0)
      return EVAL_RAISE();

    PyObject *result = PyObject_CallNoArgs(callable);
    if (! result)
      return EVAL_RAISE();
    return deliver_py_call_result(es, result, tail_call, tail_defer_return);
  }

  if (do_frame_prep(es, callable, slots, tail_call) < 0)
    return EVAL_RAISE();

  return kos_call_slot_result(
      es, slots, callable,
      KosCallable_CallArgsSlots(es, slots, callable, NULL, NULL,
                                tail_call));
}


/* === Python iter next (OBJECT_NEXT / ITER_NEXT opcodes) === */


/**
   Acquire an iterator from an iterable, uniform over both protocols
   (the PUSH_FOREACH preference order, shared with OBJECT_ITER so the
   two cannot drift): a Koskari AsyncGenerator passes through as itself
   (native, never demand-wrapped); an object with am_aiter yields its
   async iterator; otherwise the sync protocol (PyObject_GetIter,
   including the sequence fallback).

   :param iterable: borrowed
   :return: new iterator reference, or NULL with PyErr set
*/
PyObject *kos_iter_acquire(PyObject *iterable) {

  if (KosIter_Check(iterable)) {
    Py_INCREF(iterable);
    return iterable;
  }

  if (Py_TYPE(iterable)->tp_as_async
      && Py_TYPE(iterable)->tp_as_async->am_aiter)
    return Py_TYPE(iterable)->tp_as_async->am_aiter(iterable);

  return PyObject_GetIter(iterable);
}


/**
   Close an acquired iterator, best-effort and exception-fenced: a
   provider through its suite's ki_close, a sync iterator through
   close(), an async iterator through aclose() with its awaitable
   demand-driven -- an immediate completion honors the cleanup, a
   genuine suspension takes the cannot-honor fallback of closing the
   in-flight iterator. Any pending exception is preserved.
*/
void kos_iter_close(PyObject *iter) {

  PyObject *ftyp, *fval, *ftb;

  if (! iter)
    return;

  PyErr_Fetch(&ftyp, &fval, &ftb);

  {
    KosIterSlots *islots;

    if (KosIter_CheckSlots(iter, &islots)) {
      (void) islots->ki_close(iter);
      PyErr_Clear();

    } else if (Py_TYPE(iter)->tp_iternext) {
      PyObject *r = PyObject_CallMethod(iter, "close", NULL);
      Py_XDECREF(r);
      PyErr_Clear();

    } else {
      PyObject *aw = PyObject_CallMethod(iter, "aclose", NULL);
      if (aw) {
        PyObject *it = PyObject_GetAwaitable(aw);
        Py_DECREF(aw);
        if (it) {
          PyObject *step = PyObject_CallMethod(it, "send", "O", Py_None);
          if (step) {
            /* Suspend attempt during close; cannot honor further --
               close the iterator so its GeneratorExit cleanup runs. */
            Py_DECREF(step);
            PyObject *r = PyObject_CallMethod(it, "close", NULL);
            Py_XDECREF(r);
          }
          Py_DECREF(it);
        }
      }
      PyErr_Clear();
    }
  }

  PyErr_Restore(ftyp, fval, ftb);
}


/**
   Classify a produced result under ambient async: a non-awaitable, a
   Defer, or a future/task handle is a completed value; anything else
   awaitable must be awaited before use.
*/
int KosEval_ResultIsValue(PyObject *result) {

  PyTypeObject *typ = Py_TYPE(result);

  if (! typ->tp_as_async || ! typ->tp_as_async->am_await)
    return 1;
  if (KosDefer_Check(result))
    return 1;
  return KosEval_IsFuture(result);
}

/**
   Get next value from a Python async iterator using the given __anext__
   awaitable. Caller must have obtained awaitable via get_await_next(iter);
   this helper consumes it (DECREF). *result_out is set with a new
   reference on GET_NEXT_VALUE.
*/
static KosGetNextResult next_from_python_async_iter(
    KosEvalState *es,
    PyObject **await_out,
    PyObject *iter,
    PyObject **result_out,
    PyObject *awaitable) {

  (void) iter;
  int suspended = 0;
  PyObject *yielded = NULL;
  PyObject *result = KosEvalState_EagerStepAwaitable(es, awaitable, &suspended,
                                                     &yielded);
  Py_DECREF(awaitable);

  if (suspended && yielded) {
    KosEvalState_MarkSuspendedNotRunning(es);
    *await_out = yielded;
    return GET_NEXT_SUSPEND;
  }

  if (PyErr_ExceptionMatches(PyExc_StopAsyncIteration)) {
    PyErr_Clear();
    return GET_NEXT_EXHAUSTED;
  }

  if (result) {
    *result_out = result;
    return GET_NEXT_VALUE;
  }

  return GET_NEXT_ERROR;
}


/**
   Get next value from a Python sync iterator (PyIter_Next).
   *result_out is set with a new reference on GET_NEXT_VALUE.
*/
static KosGetNextResult next_from_python_sync_iter(
    KosEvalState *es,
    PyObject **await_out,
    PyObject *iter,
    PyObject **result_out) {

  (void) es;
  (void) await_out;

  PyObject *n = PyIter_Next(iter);
  if (n) {
    *result_out = n;
    return GET_NEXT_VALUE;
  }

  if (PyErr_Occurred())
    return GET_NEXT_ERROR;

  return GET_NEXT_EXHAUSTED;
}


/**
   Dispatcher: get next from iter (Koskari async gen, Python async iter,
   or Python sync iter). Does not push; does not perform GetIter.
   *result_out set only on GET_NEXT_VALUE (caller pushes and DECREF).
*/
static inline KosGetNextResult op_get_next_from_iter(
    KosEvalState *es,
    PyObject **await_out,
    PyObject *iter,
    PyObject **result_out) {

  /* Koskari async gen is handled in NEXT/ITER_NEXT via in-loop attach; never here. */

  PyObject *awaitable = get_await_next(iter);
  if (awaitable) {
    KosGetNextResult r = next_from_python_async_iter(es, await_out, iter,
                                                      result_out, awaitable);
    return r;
  }

  if (PyIter_Check(iter))
    return next_from_python_sync_iter(es, await_out, iter, result_out);

  PyErr_SetString(PyExc_TypeError, "not an iterator");
  return GET_NEXT_ERROR;
}


/**
  Globals
*/
static inline PyObject *KosEvalState_GlobalsNamespace(KosEvalState *es) {

  KosDynFrame *frame = KosEvalState_FRAME(es);
  PyObject *ns;

  if (! frame) {
    PyErr_SetString(PyExc_RuntimeError, "no frame for globals lookup");
    return NULL;
  }

  ns = KosDynFrame_NS(frame);
  if (! ns || ! KosNamespace_Check(ns)) {
    PyErr_SetString(PyExc_RuntimeError,
                    "frame has no namespace for globals lookup");
    return NULL;
  }

  while (KosNamespace_PARENT(ns))
    ns = KosNamespace_PARENT(ns);

  return ns;
}


// The end.
