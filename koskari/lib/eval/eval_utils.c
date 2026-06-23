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

  if (! await_iter) {
    PyErr_SetString(PyExc_TypeError,
                    "eager_step_awaitable: __await__() returned NULL");
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
   paths are intercepted centrally in eval_phase_post_opcode_tail.
*/
static int kos_eval_demand_deny_suspend(KosEvalState *es, const char *msg) {
  if (! kos_eval_in_demand(es))
    return 0;
  kos_demand_set_sync_violation(msg);
  return 1;
}


/**
   Push val, or if val is awaitable run eager_step and either push the
   result or suspend (KOS_EV_AWAIT). Use only for (1) the result of a
   call (CALL opcode: callable returned a coroutine) and (2) the AWAIT
   opcode (explicitly awaiting TOS). All other paths must use KosEvalState_StackPUSH
   so we do not auto-await values that are merely loaded or computed.

   CALL* bytecode uses op_push_or_await instead: same logic plus an optional
   POP,1 peephole so KosEvalState_PushOrAwait and this function stay free of that
   cost on every non-opcode caller (e.g. defer_drive, KosEvalState_Call).

   val is consumed (new reference).

   :return: 0 continue, 1 stop (AWAIT), -1 error
*/
static int push_or_await_common(
    KosEvalState *es,
    KosEvent *out_ev,
    PyObject *val,
    int await_defer) {

  /* Defer is not auto-awaited; push as plain value (explicit AWAIT drives it). */
  if (! await_defer && KosDefer_Check(val)) {
    return KosEvalState_StackPUSH(es, val);
  }

  PyTypeObject *type = Py_TYPE(val);
  SAYF("[eval] push_or_await entry is_awaitable=%d\n",
       (type->tp_as_async && type->tp_as_async->am_await) ? 1 : 0);
  if (! type->tp_as_async || ! type->tp_as_async->am_await) {
    return KosEvalState_StackPUSH(es, val);
  }

  int suspended = 0;
  PyObject *yielded = NULL;
  PyObject *result = KosEvalState_EagerStepAwaitable(es, val, &suspended, &yielded);
  Py_DECREF(val);

  if (result) {
    return KosEvalState_StackPUSH(es, result);
  }

  if (suspended && yielded) {
    KosEvalState_MarkSuspendedNotRunning(es);
    KosEvent_KIND(out_ev) = KOS_EV_AWAIT;
    KosEvent_PAYLOAD(out_ev) = yielded;
    SAYF("[eval] push_or_await set KOS_EV_AWAIT (suspended=%d yielded=%d)\n",
         suspended, yielded ? 1 : 0);
    return 1;
  }

  return -1;
}


int KosEvalState_PushOrAwait(
    KosEvalState *es,
    KosEvent *out_ev,
    PyObject *val) {

  return push_or_await_common(es, out_ev, val, 0);
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

   :return: 0 continue, 1 stop (AWAIT), -1 error
*/

#define OP_TRY_FUSE_POP1_AFTER_PUSH(es, val)                                  \
  do {                                                                        \
    KosCode *opfuse_code = KosEvalState_CODE(es);                             \
    uint32_t opfuse_pc = KosEvalState_PC(es);                                 \
    KosOp *opfuse_nx = &KosCode_CODE(opfuse_code)[opfuse_pc];                 \
    if (KosOp_OP(opfuse_nx) == KOS_OP_POP && KosOp_ARG(opfuse_nx) == 1) {     \
      Py_DECREF(val);                                                         \
      KosEvalState_SET_PC(es, opfuse_pc + 1u);                                \
      return 0;                                                               \
    }                                                                         \
  } while (0)


static inline int op_push_or_await(
    KosEvalState *es,
    KosEvent *out_ev,
    PyObject *val) {

  /* Defer is not auto-awaited; push as plain value (explicit AWAIT drives it). */
  if (KosDefer_Check(val)) {
    OP_TRY_FUSE_POP1_AFTER_PUSH(es, val);
    return KosEvalState_StackPUSH(es, val);
  }

  PyTypeObject *type = Py_TYPE(val);
  SAYF("[eval] op_push_or_await entry is_awaitable=%d\n",
       (type->tp_as_async && type->tp_as_async->am_await) ? 1 : 0);
  if (! type->tp_as_async || ! type->tp_as_async->am_await) {
    OP_TRY_FUSE_POP1_AFTER_PUSH(es, val);
    return KosEvalState_StackPUSH(es, val);
  }

  int suspended = 0;
  PyObject *yielded = NULL;
  PyObject *result = KosEvalState_EagerStepAwaitable(es, val, &suspended, &yielded);
  Py_DECREF(val);

  if (result) {
    OP_TRY_FUSE_POP1_AFTER_PUSH(es, result);
    return KosEvalState_StackPUSH(es, result);
  }

  if (suspended && yielded) {
    KosEvalState_MarkSuspendedNotRunning(es);
    KosEvent_KIND(out_ev) = KOS_EV_AWAIT;
    KosEvent_PAYLOAD(out_ev) = yielded;
    SAYF("[eval] op_push_or_await set KOS_EV_AWAIT (suspended=%d yielded=%d)\n",
         suspended, yielded ? 1 : 0);
    return 1;
  }

  return -1;
}

#undef OP_TRY_FUSE_POP1_AFTER_PUSH


static inline KosOpOutcome kos_op_outcome_PUSH(
    KosEvalState *es,
    PyObject *val) {

  if (unlikely(! val))
    return KOS_OP_POST_ERR();

  int r = KosEvalState_StackPUSH(es, val);
  if (likely(r == 0))
    return KOS_OP_RUN();

  return KOS_OP_POST_ERR();
}


static inline KosOpOutcome kos_op_outcome_PUSH_OR_AWAIT(
    KosEvalState *es,
    KosEvent *out_ev,
    PyObject *val) {

  if (unlikely(! val))
    return KOS_OP_POST_ERR();

  int r = op_push_or_await(es, out_ev, val);
  if (likely(r == 0))
    return KOS_OP_RUN();

  if (r == 1)
    return KOS_OP_POST_SUSPEND();

  if (r < 0)
    return KOS_OP_POST_ERR();

  return KOS_OP_RUN();
}


static inline KosOpOutcome kos_op_outcome_from_push_int(int r) {
  if (r < 0)
    return KOS_OP_POST_ERR();
  if (r == 1)
    return KOS_OP_POST_SUSPEND();
  return KOS_OP_RUN();
}


/**
  Map legacy opcode-helper int to KosOpOutcome (0 run, 1 suspend, 2 control,
  -1 err).
*/
static inline KosOpOutcome kos_op_outcome_from_int_step(int r) {
  if (r < 0)
    return KOS_OP_POST_ERR();
  if (r == 1)
    return KOS_OP_POST_SUSPEND();
  if (r == KOS_OP_RES_CONTROL_RETURNED)
    return KOS_OP_POST_CONTROL();
  if (r == KOS_OP_RES_PARENT_RETURN_DELIVER)
    return KOS_OP_POST_PARENT_RETURN();
  return KOS_OP_RUN();
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


/**
  Map legacy int-step result to KosOpResult.  When r==1, extract kind and
  payload from ev (KOS_EV_YIELD or KOS_EV_AWAIT); ev is cleared on exit.
  When r==4, extract PARENT_RETURN payload from ev.
*/
static inline KosOpResult kos_op_result_from_int_step(int r, KosEvent *ev) {
  switch (r) {
  case 0:
    return EVAL_NORMAL();
  case 1: {
    KosEventKind k = ev ? KosEvent_KIND(ev) : KOS_EV_NONE;
    PyObject *val = ev ? KosEvent_PAYLOAD(ev) : NULL;
    if (ev) {
      KosEvent_PAYLOAD(ev) = NULL;
      KosEvent_KIND(ev) = KOS_EV_NONE;
    }
    if (k == KOS_EV_YIELD)
      return EVAL_YIELD(val);
    if (k == KOS_EV_AWAIT)
      return EVAL_AWAIT(val);
    Py_XDECREF(val);
    PyErr_SetString(PyExc_RuntimeError,
                    "kos_op_result_from_int_step: unexpected suspend event kind");
    return EVAL_RAISE();
  }
  case KOS_OP_RES_CONTROL_RETURNED:
    return EVAL_CONTROL_RETURNED();
  case KOS_OP_RES_PARENT_RETURN_DELIVER: {
    PyObject *val = ev ? KosEvent_PAYLOAD(ev) : NULL;
    if (ev)
      KosEvent_PAYLOAD(ev) = NULL;
    return EVAL_PARENT_RETURN(val);
  }
  default:
    /* Unexpected int-step value; no valid PyErr or event to recover from. */
    return EVAL_FATAL();
  }
}


/* KosOpResult variant of kos_op_outcome_PUSH_OR_AWAIT. */
static inline KosOpResult kos_op_result_PUSH_OR_AWAIT(
    KosEvalState *es,
    PyObject *val) {

  if (unlikely(! val))
    return EVAL_RAISE();

  KosEvent _push_ev = {0};
  int r = op_push_or_await(es, &_push_ev, val);
  if (likely(r == 0))
    return EVAL_NORMAL();

  if (r == 1) {
    PyObject *awaitable = KosEvent_PAYLOAD(&_push_ev);
    KosEvent_PAYLOAD(&_push_ev) = NULL;
    return EVAL_AWAIT(awaitable);
  }

  return EVAL_RAISE();
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

  if (! wants && tail_call && parent)
    return kos_dyn_pop_frame(es);

  return 0;
}


int KosEvalState_Call(
    KosEvalState *es,
    KosEvent *out_ev,
    PyObject *callable,
    PyObject *const *args,
    Py_ssize_t nargs,
    PyObject *kwnames,
    int tail_call) {

  if (! KosCallable_Check(callable)) {
    if (tail_call) {
      KosDynFrame *frame = KosEvalState_FRAME(es);
      KosDynNode *const parent = KosDynNode_PARENT((KosDynNode *) frame);
      if (parent && kos_dyn_pop_frame(es) < 0)
        return -1;
    }
    /* nargs matches KosCallable_Call: positional count only. PEP 590:
       keyword values follow args[nargs .. nargs + nkw - 1]. */
    PyObject *result = (nargs == 0 && ! kwnames)
        ? PyObject_CallNoArgs(callable)
        : PyObject_Vectorcall(callable, args, (size_t) nargs, kwnames);
    if (! result)
      return -1;
    int r = push_or_await_common(es, out_ev, result, 0);
    if (r == 0 && tail_call)
      r = KOS_OP_RES_CONTROL_RETURNED;
    return r;
  }

  PyTypeObject *typ = Py_TYPE(callable);
  if (do_frame_prep(es, callable, typ, tail_call) < 0)
    return -1;

  int r = KosCallable_Call(es, out_ev, callable, args, nargs, kwnames,
                           tail_call);
  if (r == 0 && tail_call)
    r = KOS_OP_RES_CONTROL_RETURNED;
  return r;
}


int KosEvalState_CallArgs(
    KosEvalState *es,
    KosEvent *out_ev,
    PyObject *callable,
    PyObject *args_tuple,
    PyObject *kwds_dict,
    int tail_call) {

  if (! KosCallable_Check(callable)) {
    if (tail_call) {
      KosDynFrame *frame = KosEvalState_FRAME(es);
      KosDynNode *const parent = KosDynNode_PARENT((KosDynNode *) frame);
      if (parent && kos_dyn_pop_frame(es) < 0)
        return -1;
    }
    PyObject *result = NULL;

    if (args_tuple) {
      result = PyObject_Call(callable, args_tuple, kwds_dict);

    } else if (kwds_dict) {
      PyObject *empty_args = PyTuple_New(0);
      if (! empty_args)
        return -1;
      result = PyObject_Call(callable, empty_args, kwds_dict);
      Py_DECREF(empty_args);

    } else {
      result = PyObject_CallNoArgs(callable);
    }

    if (! result)
      return -1;

    int r = push_or_await_common(es, out_ev, result, 0);
    if (r == 0 && tail_call)
      r = KOS_OP_RES_CONTROL_RETURNED;

    return r;
  }

  PyTypeObject *typ = Py_TYPE(callable);
  if (do_frame_prep(es, callable, typ, tail_call) < 0)
    return -1;

  int r = KosCallable_CallArgs(es, out_ev, callable, args_tuple, kwds_dict,
                               tail_call);
  if (r == 0 && tail_call)
    r = KOS_OP_RES_CONTROL_RETURNED;
  return r;
}


int KosEvalState_CallNoArgs(
    KosEvalState *es,
    KosEvent *out_ev,
    PyObject *callable,
    int tail_call) {

  if (! KosCallable_Check(callable)) {
    if (tail_call) {
      KosDynFrame *frame = KosEvalState_FRAME(es);
      KosDynNode *const parent = KosDynNode_PARENT((KosDynNode *) frame);
      if (parent && kos_dyn_pop_frame(es) < 0)
        return -1;
    }
    PyObject *result = PyObject_CallNoArgs(callable);
    if (! result)
      return -1;
    int r = push_or_await_common(es, out_ev, result, 0);
    if (r == 0 && tail_call)
      r = KOS_OP_RES_CONTROL_RETURNED;
    return r;
  }

  PyTypeObject *typ = Py_TYPE(callable);
  if (do_frame_prep(es, callable, typ, tail_call) < 0)
    return -1;

  int r = KosCallable_CallNoArgs(es, out_ev, callable, tail_call);
  if (r == 0 && tail_call)
    r = KOS_OP_RES_CONTROL_RETURNED;
  return r;
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
    KosEvent *out_ev,
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
    KosEvent_KIND(out_ev) = KOS_EV_AWAIT;
    KosEvent_PAYLOAD(out_ev) = yielded;
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
    KosEvent *out_ev,
    PyObject *iter,
    PyObject **result_out) {

  (void) es;
  (void) out_ev;

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
    KosEvent *out_ev,
    PyObject *iter,
    PyObject **result_out) {

  /* Koskari async gen is handled in NEXT/ITER_NEXT via in-loop attach; never here. */

  PyObject *awaitable = get_await_next(iter);
  if (awaitable) {
    KosGetNextResult r = next_from_python_async_iter(es, out_ev, iter,
                                                      result_out, awaitable);
    return r;
  }

  if (PyIter_Check(iter))
    return next_from_python_sync_iter(es, out_ev, iter, result_out);

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
