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
   The first step of a Python awaitable, through kos_await_first_step.
   A completion is returned. A suspension stores the await iterator on
   ctx for the resume, hands back the yield in *yielded with
   *suspended set, and returns NULL. An error returns NULL with
   neither set.
*/
static PyObject *eager_step_awaitable(
    PyObject *awaitable,
    KosContext *ctx,
    int *suspended,
    PyObject **yielded) {

  PyObject *await_iter = NULL;
  PyObject *result;

  *suspended = 0;
  *yielded = NULL;

  if (! awaitable) {
    PyErr_SetString(PyExc_TypeError,
                    "eager_step_awaitable: awaitable is NULL");
    return NULL;
  }

  result = kos_await_first_step(awaitable, &await_iter, yielded);
  if (result)
    return result;
  if (! await_iter)
    return NULL;

  *suspended = 1;
  Py_XSETREF(KosContext_PY_AWAIT_ITER(ctx), await_iter);
  return NULL;
}


static inline PyObject *KosContext_EagerStepAwaitable(
    KosContext *ctx,
    PyObject *awaitable,
    int *suspended,
    PyObject **yielded) {

  return eager_step_awaitable(awaitable, ctx, suspended, yielded);
}


/**
   The awaitable for one step of an async iterator: am_anext's result,
   which the eager step resolves once. New reference, or NULL on error
   or when the type has no am_anext, with no exception set in that
   case.
*/
static PyObject *get_await_next(PyObject *val) {
  PyTypeObject *type = Py_TYPE(val);
  unaryfunc anext = NULL;

  if (type->tp_as_async)
    anext = type->tp_as_async->am_anext;

  if (! anext)
    return NULL;

  return anext(val);
}


/* === Demand extent detection === */


/**
   Return 1 if the current frame carries KOS_FRAME_DEMAND. O(1): the flag is
   inherited by kos_dyn_push_frame at install time, so the current frame always
   answers for the entire demand extent. Every async suspension under a
   demand extent is intercepted centrally in eval_event_await.
*/
static int kos_eval_in_demand(KosContext *ctx) {
  KosDynFrame *frame = KosContext_FRAME(ctx);
  return frame && (KosDynFrame_FLAGS(frame) & KOS_FRAME_DEMAND);
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
static inline int push_fuse_pop1(KosContext *ctx, PyObject *val) {
  uint32_t pc = KosContext_PC(ctx);
  KosOp *next = &KosContext_OPS(ctx)[pc];

  if (next->op == KOS_OP_POP && next->arg == 1) {
    Py_DECREF(val);
    KosContext_SET_PC(ctx, pc + 1u);
    return 1;
  }
  return 0;
}


/* Push val, or fuse it away when fuse is set and the peephole applies.
   val is consumed. */
static inline KosOpResult push_or_fuse(
    KosContext *ctx,
    PyObject *val,
    int fuse) {

  if (fuse && push_fuse_pop1(ctx, val))
    return EVAL_NORMAL();
  return (KosContext_StackPUSH(ctx, val) < 0) ? EVAL_RAISE() : EVAL_NORMAL();
}


/**
   The settle half of push_or_await. A plain value comes back as it
   is, and so do a Defer and an asyncio future unless await_defer is
   set. A fresh Koskari coroutine, or a Defer delegating to one under
   an explicit await, is installed as a call frame on the running
   chain. Any other awaitable is eagerly stepped to its result, or
   suspends with the iterator stored on the context. NORMAL with *out
   the settled value (new reference), REPROLOGUE with a callee
   installed whose return delivers the value, AWAIT with the yielded
   value, or RAISE. val is consumed.
*/
static inline KosOpResult kos_settle_value(
    KosContext *ctx,
    PyObject *val,
    int await_defer,
    PyObject **out) {

  /* Awaitability gate first: a type-level probe (two loads), so the
     dominant non-awaitable result skips everything below -- including
     the Defer subtype walk, which is sound to hoist because KosDefer
     defines am_await and therefore always passes this gate. */
  PyTypeObject *type = Py_TYPE(val);
  SAYF("[eval] settle entry is_awaitable=%d\n",
       (type->tp_as_async && type->tp_as_async->am_await) ? 1 : 0);
  if (! type->tp_as_async || ! type->tp_as_async->am_await) {
    /* an explicit await also takes a types.coroutine marked generator,
       as CPython's await does; the eager step below runs it as its own
       iterator. Every other slotless object is a value. */
    if (! (await_defer && PyGen_CheckExact(val)
           && kos_gen_is_iterable_coroutine(val))) {
      *out = val;
      return EVAL_NORMAL();
    }
  }

  /* A Koskari coroutine that has not been started is a program with
     nothing to drive: it installs on this chain as a call frame and
     returns its value the way a callee does. A Defer under an explicit
     await hands over the fresh coroutine it delegates to. A coroutine
     already started keeps its own drive below. */
  if (await_defer && KosDefer_Check(val)) {
    PyObject *fresh = KosDefer_TakeFreshCoroutine(val);
    if (fresh) {
      Py_DECREF(val);
      val = fresh;
      type = Py_TYPE(val);
    }
  }
  if (type == &KosCoroutineType) {
    KosCode *code;
    PyObject *ns;

    if (KosCoroutine_TakeFresh(val, &code, &ns)) {
      KosOpResult r = KosContext_InstallCallFrame(ctx, code, ns, 0);
      Py_DECREF((PyObject *) code);
      Py_DECREF(val);
      return r.kind == KOS_RES_RAISE ? r : EVAL_REPROLOGUE();
    }
  }

  /* Defer is not auto-awaited; a plain value (explicit AWAIT drives it). */
  if (! await_defer && KosDefer_Check(val)) {
    *out = val;
    return EVAL_NORMAL();
  }

  /* asyncio futures are handles; implicit sites take them plain. */
  if (! await_defer) {
    int isfut = kos_isfuture(val);
    if (isfut < 0) {
      Py_DECREF(val);
      return EVAL_RAISE();
    }
    if (isfut) {
      *out = val;
      return EVAL_NORMAL();
    }
  }

  int suspended = 0;
  PyObject *yielded = NULL;
  PyObject *result = KosContext_EagerStepAwaitable(ctx, val, &suspended, &yielded);
  Py_DECREF(val);

  if (result) {
    *out = result;
    return EVAL_NORMAL();
  }

  if (suspended && yielded) {
    KosContext_MarkSuspendedNotRunning(ctx);
    SAYF("[eval] settle set KOS_RES_AWAIT (suspended=%d yielded=%d)\n",
         suspended, yielded ? 1 : 0);
    return EVAL_AWAIT(yielded);
  }

  return EVAL_RAISE();
}


/**
   Push val. An awaitable val is settled first through
   kos_settle_value: its result is pushed, a suspension returns
   KOS_RES_AWAIT, and a fresh Koskari coroutine is installed as a
   callee instead, reported as REPROLOGUE, whose return lands the
   value where the push would have. Use only for the result of a call
   (a CALL opcode whose callable returned a coroutine) and for the
   AWAIT opcode (explicitly awaiting TOS). Every other path must use
   KosContext_StackPUSH, so values merely loaded or computed are
   never auto-awaited.

   await_defer: the explicit AWAIT opcode drives a Defer or an asyncio
   future. Implicit sites push those as plain handles.

   fuse: a CALL* handler passes 1 so a pushed result followed by POP 1
   is elided (push_fuse_pop1). Every non-opcode caller passes 0. Both
   flags are literals at every call site and fold away.

   val is consumed (new reference).

   :return: EVAL_NORMAL, EVAL_REPROLOGUE, EVAL_AWAIT, or EVAL_RAISE
*/
static inline KosOpResult push_or_await(
    KosContext *ctx,
    PyObject *val,
    int await_defer,
    int fuse) {

  PyObject *settled;
  KosOpResult r = kos_settle_value(ctx, val, await_defer, &settled);

  if (r.kind != KOS_RES_NORMAL)
    return r;
  return push_or_fuse(ctx, settled, fuse);
}


KosOpResult KosContext_Settle(KosContext *ctx, PyObject *val, PyObject **out) {
  if (unlikely(! val))
    return EVAL_RAISE();

  return kos_settle_value(ctx, val, 0, out);
}


KosOpResult KosContext_PushOrAwait(KosContext *ctx, PyObject *val) {
  if (unlikely(! val))
    return EVAL_RAISE();

  return push_or_await(ctx, val, 0, 0);
}


KosOpResult KosContext_SettleStage(KosContext *ctx, PyObject *val) {

  PyObject *settled = NULL;
  KosOpResult r = KosContext_Settle(ctx, val, &settled);

  /* REPROLOGUE: an installed callee delivers. AWAIT: the revive
     delivers. RAISE: the walk propagates. */
  if (r.kind != KOS_RES_NORMAL)
    return r;

  KosContext_PENDING_VALUE(ctx) = settled;
  return EVAL_REPROLOGUE();
}


/* KosOpResult variant of kos_op_outcome_PUSH_OR_AWAIT. */
static inline KosOpResult kos_op_result_PUSH_OR_AWAIT(
    KosContext *ctx,
    PyObject *val) {

  if (unlikely(! val))
    return EVAL_RAISE();

  return push_or_await(ctx, val, 0, 1);
}


// The end.
