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

  /* Defer is not auto-awaited; push as plain value (explicit AWAIT drives it). */
  if (! await_defer && KosDefer_Check(val)) {
    return (KosEvalState_StackPUSH(es, val) < 0) ? EVAL_RAISE() : EVAL_NORMAL();
  }

  PyTypeObject *type = Py_TYPE(val);
  SAYF("[eval] push_or_await entry is_awaitable=%d\n",
       (type->tp_as_async && type->tp_as_async->am_await) ? 1 : 0);
  if (! type->tp_as_async || ! type->tp_as_async->am_await) {
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
    KosCode *opfuse_code = KosEvalState_CODE(es);                             \
    uint32_t opfuse_pc = KosEvalState_PC(es);                                 \
    KosOp *opfuse_nx = &KosCode_CODE(opfuse_code)[opfuse_pc];                 \
    if (KosOp_OP(opfuse_nx) == KOS_OP_POP && KosOp_ARG(opfuse_nx) == 1) {     \
      Py_DECREF(val);                                                         \
      KosEvalState_SET_PC(es, opfuse_pc + 1u);                                \
      return EVAL_NORMAL();                                                   \
    }                                                                         \
  } while (0)


static inline KosOpResult push_or_await_fused(
    KosEvalState *es,
    PyObject *val) {

  /* Defer is not auto-awaited; push as plain value (explicit AWAIT drives it). */
  if (KosDefer_Check(val)) {
    OP_TRY_FUSE_POP1_AFTER_PUSH(es, val);
    return (KosEvalState_StackPUSH(es, val) < 0) ? EVAL_RAISE() : EVAL_NORMAL();
  }

  PyTypeObject *type = Py_TYPE(val);
  SAYF("[eval] push_or_await_fused entry is_awaitable=%d\n",
       (type->tp_as_async && type->tp_as_async->am_await) ? 1 : 0);
  if (! type->tp_as_async || ! type->tp_as_async->am_await) {
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


/* === Parameterized binop core === */

/* Shared out-of-line body for the fast-slot in-place family.  Forced
   noinline so the op-type switch is a genuine runtime (polymorphic) branch,
   matching the eventual FAST_OBJECT_INPLACE_BINOP handler reading op from its
   operand.  Binary (count 1) for the gate; reducing generalization is later.
   Returns 0/-1 for kos_op_result_from_push_int.  Operator set is
   KOS_BINOP_LIST (binops.h). */
static KOS_NOINLINE int kos_fast_inplace_binop(
    KosEvalState *es, KosBinop op, size_t idx) {

  PyObject *ns = KosEvalState_NAMESPACE(es);
  PyObject *obj = kos_ns_fast_deref(ns, idx);
  if (unlikely(! obj))
    return -1;

  PyObject *rhs = KosEvalState_StackPopRef(es);
  if (unlikely(! rhs)) {
    Py_DECREF(obj);
    return -1;
  }

  PyObject *result = NULL;
  switch (op) {
#define KOS_BINOP_INPLACE_CASE(NAME, LOP, RFN, IFN)                    \
    case KOS_BINOP_##NAME:                                            \
      if (kos_long_binop_fast(LOP, obj, rhs, &result) == 0)          \
        result = IFN(obj, rhs);                                       \
      break;
    KOS_BINOP_LIST(KOS_BINOP_INPLACE_CASE)
#undef KOS_BINOP_INPLACE_CASE
    default:
      PyErr_SetString(PyExc_SystemError, "bad binop op-type");
  }

  Py_DECREF(obj);
  Py_DECREF(rhs);
  if (unlikely(! result))
    return -1;

  int res = kos_ns_fast_store(ns, idx, result);
  Py_DECREF(result);
  return res;
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
*/
static inline int do_frame_prep(
    KosEvalState *es,
    PyObject *callable,
    PyTypeObject *typ,
    int tail_call) {

  KosCallableTypeObject *ctyp = (KosCallableTypeObject *) typ;
  int wants;

  if (ctyp->tp_kos_wants_frame_fn)
    wants = ctyp->tp_kos_wants_frame_fn(callable);
  else
    wants = ctyp->tp_kos_wants_frame;

  KosDynFrame *frame = KosEvalState_FRAME(es);
  KosDynNode *const parent = KosDynNode_PARENT((KosDynNode *) frame);

  if (wants && tail_call) {
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

  if (! wants && tail_call && parent
      && KosEvalState_DYN_TOP(es) == (KosDynNode *) frame)
    return kos_dyn_pop_frame(es);

  return 0;
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


KosOpResult KosEvalState_Call(
    KosEvalState *es,
    PyObject *callable,
    PyObject *const *args,
    Py_ssize_t nargs,
    PyObject *kwnames,
    int tail_call) {

  if (! KosCallable_Check(callable)) {
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

  PyTypeObject *typ = Py_TYPE(callable);
  if (do_frame_prep(es, callable, typ, tail_call) < 0)
    return EVAL_RAISE();

  return KosCallable_Call(es, callable, args, nargs, kwnames, tail_call);
}


KosOpResult KosEvalState_CallArgs(
    KosEvalState *es,
    PyObject *callable,
    PyObject *args_tuple,
    PyObject *kwds_dict,
    int tail_call) {

  if (! KosCallable_Check(callable)) {
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

  PyTypeObject *typ = Py_TYPE(callable);
  if (do_frame_prep(es, callable, typ, tail_call) < 0)
    return EVAL_RAISE();

  return KosCallable_CallArgs(es, callable, args_tuple, kwds_dict, tail_call);
}


KosOpResult KosEvalState_CallNoArgs(
    KosEvalState *es,
    PyObject *callable,
    int tail_call) {

  if (! KosCallable_Check(callable)) {
    if (KosInstanceCall_Check(callable)) {
      PyObject *call_fn = kos_instance_call_target(callable);
      if (call_fn) {
        /* self is the sole positional. */
        PyObject *self_arg[1];
        self_arg[0] = callable;
        return KosEvalState_Call(es, call_fn, self_arg, 1, NULL, tail_call);
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

  PyTypeObject *typ = Py_TYPE(callable);
  if (do_frame_prep(es, callable, typ, tail_call) < 0)
    return EVAL_RAISE();

  return KosCallable_CallNoArgs(es, callable, tail_call);
}


/* === Python iter next (NEXT / ITER_NEXT opcodes) === */

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
