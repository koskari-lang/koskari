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
   koskari/lib/eval/eval_state.c

   KosEvalState and related implementation. Included verbatim into
   eval.c (single translation unit).

   :author: Christopher O'Brien <obriencj@preoccupied.net>
   :license: GNU General Public License v3
   :ai-assistant: Composer via Cursor
*/


void KosEvalState_Init(KosEvalState *es, KosContext *ctx) {

  KOS_TRACE_EVAL_SYNC_BEGIN();

  KosEvalState_CONTEXT(es) = ctx;
  KosEvalState_FRAME(es) = KosContext_FRAME_TOP(ctx);
  KosEvalState_BLOCK(es) = KosContext_BLOCK(ctx);
  KosEvalState_NAMESPACE(es) = KosContext_NAMESPACE(ctx);
  if (! KosEvalState_NAMESPACE(es) && KosEvalState_FRAME(es))
    KosEvalState_NAMESPACE(es) = KosDynFrame_NS(KosEvalState_FRAME(es));

  if (KosEvalState_FRAME(es)) {
    KosEvalState_CODE(es) = KosDynFrame_CODE(KosEvalState_FRAME(es));
    (es)->pc = KosDynFrame_PC(KosEvalState_FRAME(es));
    (es)->acting_pc = KosDynFrame_ACTING_PC(KosEvalState_FRAME(es));
  } else {
    KosEvalState_CODE(es) = NULL;
    (es)->pc = 0;
    (es)->acting_pc = 0;
  }

  KOS_TRACE_EVAL_SYNC_END(KOS_TEVS_init);
}


void KosEvalState_SyncFromContext(KosEvalState *es) {

  KosContext *ctx;

  KOS_TRACE_EVAL_SYNC_BEGIN();

  ctx = KosEvalState_CONTEXT(es);
  if (! ctx)
    goto done;

  {
    KosDynFrame *ft = KosContext_FRAME_TOP(ctx);
    KosStackBlock **bl = KosContext_BLOCK(ctx);
    PyObject *ns = KosContext_NAMESPACE(ctx);
    KosDynFrame *ef = KosEvalState_FRAME(es);
    if (! ns && ft)
      ns = KosDynFrame_NS(ft);

    if (ef == ft &&
        KosEvalState_BLOCK(es) == bl &&
        KosEvalState_NAMESPACE(es) == ns) {
      if (! ft) {
        if (! KosEvalState_CODE(es) && ! (es)->pc && ! (es)->acting_pc)
          goto done;
        KosEvalState_CODE(es) = NULL;
        (es)->pc = 0;
        (es)->acting_pc = 0;
        goto done;
      }
      if (KosEvalState_CODE(es) == KosDynFrame_CODE(ft))
        goto done;
      KosEvalState_CODE(es) = KosDynFrame_CODE(ft);
      (es)->pc = KosDynFrame_PC(ft);
      (es)->acting_pc = KosDynFrame_ACTING_PC(ft);
      goto done;
    }

    KosEvalState_FRAME(es) = ft;
    KosEvalState_BLOCK(es) = bl;
    KosEvalState_NAMESPACE(es) = ns;
    if (ft) {
      KosEvalState_CODE(es) = KosDynFrame_CODE(ft);
      (es)->pc = KosDynFrame_PC(ft);
      (es)->acting_pc = KosDynFrame_ACTING_PC(ft);
    } else {
      KosEvalState_CODE(es) = NULL;
      (es)->pc = 0;
      (es)->acting_pc = 0;
    }
  }

done:
  KOS_TRACE_EVAL_SYNC_END(KOS_TEVS_sync_from_context);
}


KosDynNode *KosEvalState_InnermostStackOwner(KosEvalState *es) {
  KosContext *ctx = KosEvalState_CONTEXT(es);
  KosDynNode *n = KosContext_DYN_TOP(ctx);
  while (n) {
    if (KosDynNode_STACK(n))
      return n;
    n = KosDynNode_PARENT(n);
  }
  return NULL;
}


/**
   Refresh FRAME_TOP and BLOCK after DYN_TOP / chain changes, then apply
   them to es without a full KosEvalState_SyncFromContext traversal.
*/
void KosEvalState_RefreshDynCaches(KosEvalState *es) {
    KosContext *ctx;
    KosDynNode *node;

  KOS_TRACE_EVAL_SYNC_BEGIN();

  ctx = KosEvalState_CONTEXT(es);

  if (! ctx)
    goto done;

  KosDynFrame *ft = NULL;
  KosStackBlock **bl = NULL;
  PyObject *ns = NULL;

  node = KosContext_DYN_TOP(ctx);
  while (node) {
    if (! bl && KosDynNode_STACK(node))
      bl = &KosDynNode_STACK(node);

    if (! ns && KosDynNode_NS(node))
      ns = KosDynNode_NS(node);

    if (! ft && KosDynNode_IS_FRAME(node)) {
      ft = (KosDynFrame *) node;
      SAYF("[gen] refresh_dyn_caches dyn_top=%p -> frame_top=%p (%s)\n",
           (void *) KosContext_DYN_TOP(ctx), (void *) node,
           kos_dynnode_kind_name(KosDynNode_KIND(node)));
    }

    if (ft && bl && ns)
      break;

    node = KosDynNode_PARENT(node);
  }

  KosContext_FRAME_TOP(ctx) = ft;
  KosContext_BLOCK(ctx) = bl;
  KosContext_NAMESPACE(ctx) = ns;

  // TODO: if we get here, we need to verify that we have valid data and didn't
  // just run out of nodes

  if (! ft)
    SAYF("[gen] refresh_dyn_caches dyn_top=%p -> frame_top=NULL (no frame)\n",
         (void *) KosContext_DYN_TOP(ctx));

  KosEvalState_ApplyDynCaches(es);

done:
  KOS_TRACE_EVAL_SYNC_END(KOS_TEVS_refresh_dyn_caches);
}


void KosEvalState_ApplyDynCaches(KosEvalState *es) {

  KosContext *ctx;
  KosDynFrame *ft;
  KosStackBlock **bl;
  PyObject *ns;
  KosDynFrame *ef;

  KOS_TRACE_EVAL_SYNC_BEGIN();

  if (! es)
    goto done;

  ctx = KosEvalState_CONTEXT(es);
  if (! ctx)
    goto done;

  ft = KosContext_FRAME_TOP(ctx);
  bl = KosContext_BLOCK(ctx);
  ns = KosContext_NAMESPACE(ctx);
  ef = KosEvalState_FRAME(es);

  if (ef != ft) {
    if (ef)
      KosEvalState_FlushLiveRegsToFrame(es);
    KosEvalState_FRAME(es) = ft;
    KosEvalState_BLOCK(es) = bl;
    KosEvalState_NAMESPACE(es) = ns;
    if (ft) {
      KosEvalState_CODE(es) = KosDynFrame_CODE(ft);
      es->pc = KosDynFrame_PC(ft);
      es->acting_pc = KosDynFrame_ACTING_PC(ft);
    } else {
      KosEvalState_CODE(es) = NULL;
      es->pc = 0;
      es->acting_pc = 0;
    }
    goto done;
  }

  KosEvalState_BLOCK(es) = bl;
  KosEvalState_NAMESPACE(es) = ns;

done:
  KOS_TRACE_EVAL_SYNC_END(KOS_TEVS_apply_dyn_caches);
}


static inline PyObject *
kos_evalstate_get_constant(const KosEvalState *es, uint32_t index) {

  KosCode *code = KosEvalState_CODE(es);

  return code ? KosCode_GET_CONST(code, index) : NULL;
}

#define KosEvalState_GetConstant(es, index) \
  kos_evalstate_get_constant((es), (index))


static KosStackBlock **kos_evalstate_parent_stack_slot(KosEvalState *es) {

  KosDynNode *owner = KosEvalState_InnermostStackOwner(es);
  KosDynNode *node;
  if (! owner) {
    PyErr_SetString(PyExc_RuntimeError,
                    "KosEvalState_ParentStack*: no current stack owner");
    return NULL;
  }

  for (node = KosDynNode_PARENT(owner); node; node = KosDynNode_PARENT(node)) {
    if (KosDynNode_STACK(node))
      return &KosDynNode_STACK(node);
  }

  PyErr_SetString(PyExc_RuntimeError,
                  "KosEvalState_ParentStack*: no parent stack owner");
  return NULL;
}


int KosEvalState_ParentStackPushRef(KosEvalState *es, PyObject *val) {

  KosStackBlock **slot = kos_evalstate_parent_stack_slot(es);
  if (! slot)
    return -1;
  return KosStackBlock_PushRef(slot, val);
}


int KosEvalState_ParentStackPUSH(KosEvalState *es, PyObject *val) {

  KosStackBlock **slot = kos_evalstate_parent_stack_slot(es);
  if (! slot) {
    Py_XDECREF(val);
    return -1;
  }
  return KosStackBlock_PUSH(slot, val);
}

/* --- Transfer plan and delimited return --- */

/**
  Clear the context's transfer plan and free it.  Plan pointer is nulled
  before freeing so any re-entrant eval (e.g. from Py_XDECREF(continuation_ref)
  triggering Python dealloc/__del__) does not see the plan while it is freed.
  For PUSH_SHIFT unwind plans, transfers continuation_ref to the prompt's
  pending_shift_wrapper so the opcode can push the handler on re-entry.
*/
static void clear_and_free_transfer_plan(KosContext *ctx) {
  KosTransferPlan *p = KosContext_PLAN(ctx);
  KosContext_PLAN(ctx) = NULL;
  KosContext_FLAGS(ctx) &= ~KOS_CTX_IN_TRANSFER;
  if (p && KosTransferPlan_CONTINUATION_REF(p)
      && KosTransferPlan_ORIGINAL_TARGET(p)
      && KosDynNode_IS_PROMPT(KosTransferPlan_ORIGINAL_TARGET(p))) {
    KosDynPrompt *prompt = (KosDynPrompt *) KosTransferPlan_ORIGINAL_TARGET(p);
    KosDynPrompt_PENDING_SHIFT_WRAPPER(prompt) = KosTransferPlan_CONTINUATION_REF(p);
    KosTransferPlan_CONTINUATION_REF(p) = NULL;
  }
  kos_free_transfer_plan(p);
}


static inline void KosEvalState_DisarmTransferPlan(KosEvalState *es) {

  if (! es)
    return;
  clear_and_free_transfer_plan(KosEvalState_CONTEXT(es));
}


void KosEvalState_SetPendingReturn(
    KosEvalState *es,
    PyObject *val,
    int to_parent) {

  KosContext *ctx = KosEvalState_CONTEXT(es);

  KosContext_PENDING_RETURN_VALUE(ctx) = val;
  KosContext_PENDING_RETURN_TO_PARENT(ctx) = to_parent;
}


void KosEvalState_SetUnwindTargetFrame(KosEvalState *es, KosDynFrame *frame) {

  KosContext *ctx = KosEvalState_CONTEXT(es);

  KosContext_UNWIND_TARGET_FRAME(ctx) = frame;
}


static inline int KosEvalState_TakeInjectThrow(
    KosEvalState *es,
    PyObject **type_out,
    PyObject **value_out) {

  KosContext *ctx = KosEvalState_CONTEXT(es);

  if (KosContext_INJECT_MODE(ctx) != KOS_INJECT_THROW)
    return 0;
  *value_out = KosContext_INJECT_VALUE(ctx);
  *type_out = KosContext_INJECT_TYPE(ctx);
  KosContext_INJECT_MODE(ctx) = KOS_INJECT_NONE;
  KosContext_INJECT_VALUE(ctx) = NULL;
  KosContext_INJECT_TYPE(ctx) = NULL;
  return 1;
}


static inline PyObject *KosEvalState_GlobalsNamespace(KosEvalState *es) {

  KosContext *ctx = KosEvalState_CONTEXT(es);

  kos_dyn_globals_frame(es);
  return KosDynFrame_NS(KosContext_GLOBALS_FRAME(ctx));
}


int KosEvalState_SetFrameCodeAndNamespace(
    KosEvalState *es,
    KosDynFrame *frame,
    KosCode *code,
    PyObject *ns) {

  KosContext *ctx = KosEvalState_CONTEXT(es);

  Py_XDECREF(KosDynFrame_NS(frame));
  KosDynFrame_NS(frame) = ns;
  KosDynFrame_CODE(frame) = code;
  KosContext_NAMESPACE(ctx) = ns;
  KosEvalState_SyncFromContext(es);
  return 0;
}


void KosEvalState_PythonAwaitClearIter(KosEvalState *es) {
  KosContext *ctx = KosEvalState_CONTEXT(es);
  Py_CLEAR(KosContext_PY_AWAIT_ITER(ctx));
}


PyObject *KosEvalState_PythonAwaitIter(KosEvalState *es) {
  return KosContext_PY_AWAIT_ITER(KosEvalState_CONTEXT(es));
}


void KosEvalState_PythonResumeSet(KosEvalState *es, PyObject *val, uint8_t kind) {
  KosContext *ctx = KosEvalState_CONTEXT(es);

  Py_XDECREF(KosContext_PY_RESUME_INJECT(ctx));
  KosContext_PY_RESUME_INJECT(ctx) = val;
  KosContext_PY_RESUME_KIND(ctx) = kind;
}


static inline void KosEvalState_PythonAwaitSetIter(
    KosEvalState *es,
    PyObject *iter) {

  KosContext *ctx = KosEvalState_CONTEXT(es);

  Py_XDECREF(KosContext_PY_AWAIT_ITER(ctx));
  KosContext_PY_AWAIT_ITER(ctx) = iter;
}


static inline int KosEvalState_PythonResumeTake(
    KosEvalState *es,
    PyObject **out_val,
    uint8_t *out_kind) {

  KosContext *ctx = KosEvalState_CONTEXT(es);

  if (out_val)
    *out_val = KosContext_PY_RESUME_INJECT(ctx);
  if (out_kind)
    *out_kind = KosContext_PY_RESUME_KIND(ctx);
  KosContext_PY_RESUME_INJECT(ctx) = NULL;
  return (out_val && *out_val != NULL) ? 1 : 0;
}


static inline void KosEvalState_EnterDomainScope(KosEvalState *es) {
  kos_domain_push_scope(KosEvalState_CONTEXT(es));
}


static inline void KosEvalState_LeaveDomainScope(KosEvalState *es) {
  kos_domain_pop_scope(KosEvalState_CONTEXT(es));
}


KosEvaluatorToken *KosEvalState_Token(const KosEvalState *es) {
  if (! es)
    return NULL;
  return KosContext_TOKEN(KosEvalState_CONTEXT(es));
}


void KosEvalState_ArmTransferPlan(KosEvalState *es, KosTransferPlan *plan) {
  KosContext *ctx = KosEvalState_CONTEXT(es);
  KosTransferPlan *old = KosContext_PLAN(ctx);

  if (old)
    kos_free_transfer_plan(old);
  KosContext_PLAN(ctx) = plan;
  KosContext_FLAGS(ctx) |= KOS_CTX_IN_TRANSFER;
}


static inline int KosEvalState_HasActiveTransferPlan(const KosEvalState *es) {

  KosContext *ctx;

  if (! es)
    return 0;
  ctx = KosEvalState_CONTEXT(es);
  return KosContext_PLAN(ctx) != NULL;
}


/**
  Run the current transfer plan to completion.  Returns 0 on success (plan
  cleared or was NULL).  Returns -1 if a step failed (out_ev set to RAISE,
  caller should return 0 from koskari_eval).
*/
static int run_transfer_plan_to_completion(
    KosEvalState *es,
    KosEvent *out_ev) {

  KosContext *ctx = KosEvalState_CONTEXT(es);

  while (KosContext_PLAN(ctx)) {
    SAYF("[eval] transfer step %zu/%zu\n",
         (size_t) KosTransferPlan_ACTION_INDEX(KosContext_PLAN(ctx)),
         (size_t) KosTransferPlan_ACTION_COUNT(KosContext_PLAN(ctx)));
    int plan_res = kos_run_transfer_step(es, out_ev);

    if (plan_res < 0) {
      SAY("[eval] transfer step failed -> KOS_EV_RAISE\n");
      kos_eval_emit_raise_from_pyerr(ctx, out_ev, es);
      KosEvalState_DisarmTransferPlan(es);
      return -1;
    }

    if (plan_res == KOS_XFER_SUSPENDED_AWAIT) {
      SAY("[eval] transfer step suspended on await\n");
      return 0;  /* do not clear plan; driver will resume and feed inject */
    }

    if (plan_res == 0)
      continue;

    SAYF("[eval] transfer complete dyn_top=%p\n",
         (void *) KosContext_DYN_TOP(ctx));
    KosEvalState_DisarmTransferPlan(es);
  }

  return 0;
}


/**
   Handle delimited k(value) return: build plan, install it, run to completion.
   Caller has popped val; we consume it. Return 0 on success, -1 on error.
*/
static inline int KosEvalState_KReturnDelimited(
    KosEvalState *es,
    KosDynDelim *delim,
    PyObject *val,
    KosEvent *out_ev) {

  KosContext *ctx = KosEvalState_CONTEXT(es);
  KosDynNode *ret_chain = KosDynDelim_RETURN_CHAIN(delim);
  uint32_t ret_pc = KosDynDelim_RETURN_PC(delim);

  SAYF("[RETURN] taking return-target path: return_pc=%u",
       (unsigned) ret_pc);
  KosTransferPlan *plan = NULL;
  if (kos_build_return_plan(es, ret_chain,
                            val, 0, &plan) < 0) {
    Py_DECREF(val);
    return -1;
  }
  Py_DECREF(val);
  KosTransferPlan_RESUME_PC(plan) = ret_pc;
  KosTransferPlan_RESUME_FRAME(plan) = NULL;
  KosTransferPlan_RESUME_STACK(plan) = NULL;
  KosEvalState_ArmTransferPlan(es, plan);
  KosTransferPlan_CONTINUATION_REF(plan) = NULL;
  if (ret_chain) {
    kos_dynchain_free(ret_chain, NULL);
    KosDynDelim_RETURN_CHAIN(delim) = NULL;
  }
  KosDynDelim_MODE(delim) = KOS_DYN_DELIM_MODE_NONE;
  KosDynDelim_RETURN_PC(delim) = (uint32_t)-1;
  KosDynDelim_RETURN_FRAME_NODE(delim) = NULL;
  KosDynDelim_RETURN_NODE(delim) = NULL;
  if (KosContext_DYN_TOP(ctx) == (KosDynNode *) delim)
    kos_dyn_pop_delim(es, delim);
  if (run_transfer_plan_to_completion(es, out_ev) < 0)
    return -1;
  return 0;
}

/* === Drive loop: await / push (uses str_await, str_send from eval.c) === */


/* === Drive loop === */


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


/**
   Push val onto the frame stack. No auto-await; val is used as-is.
   val is consumed (new reference). Use this for all values that are not
   the direct result of a call (e.g. CONST, load, getattr, subscript).

   :return: 0 continue, -1 error
*/
static inline int push_result(KosEvalState *es, PyObject *val) {

  if (unlikely(! val))
    return -1;
  return KosEvalState_StackPUSH(es, val);
}


/**
   Push val, or if val is awaitable run eager_step and either push the
   result or suspend (KOS_EV_AWAIT). Use only for (1) the result of a
   call (CALL opcode: callable returned a coroutine) and (2) the AWAIT
   opcode (explicitly awaiting TOS). All other paths must use push_result
   so we do not auto-await values that are merely loaded or computed.

   CALL* bytecode uses op_push_or_await instead: same logic plus an optional
   POP,1 peephole so kos_push_or_await and this function stay free of that
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


static int push_or_await(KosEvalState *es, KosEvent *out_ev, PyObject *val) {
  return push_or_await_common(es, out_ev, val, 0);
}


/**
   Opcode-only variant of push_or_await: same semantics, plus peephole elision
   when the next bytecode is POP with argument 1 (discard TOS). In that case
   the value is released and the frame PC is advanced past that POP without a
   stack push. Used only from CALL* handlers where the frame PC is already the
   next instruction index after fetch. Do not use from defer_drive or other
   paths that pop the stack in C before the next opcode — use push_or_await.

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


/* Public wrapper for external callers; delegates to static push_or_await. */
int kos_push_or_await(KosEvalState *es, KosEvent *out_ev, PyObject *val) {

  return push_or_await(es, out_ev, val);
}


/* === KosEvalState_Call* (frame prep + KosCallable dispatch) === */


/**
   Prepare frame stack for a KosCallable install per wants_frame / tail_call.
   Inlines effective wants_frame resolution (hook vs static flag).
*/
static int do_frame_prep(KosEvalState *es, PyObject *callable, PyTypeObject *typ,
                         int tail_call) {

  KosCallableTypeObject *ctyp = (KosCallableTypeObject *) typ;
  int wants;

  if (ctyp->tp_kos_wants_frame_fn)
    wants = ctyp->tp_kos_wants_frame_fn(callable);
  else
    wants = ctyp->tp_kos_wants_frame;

  KosDynFrame *frame = KosEvalState_FRAME(es);
  KosDynNode *parent = KosDynNode_PARENT((KosDynNode *) frame);

  if (wants && tail_call) {
    /* TODO: single API KosEvalState_ResetFrameForTail; SyncFromContext hack */
    kos_dynframe_reset_for_tail(frame);
    KosEvalState_SyncFromContext(es);
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


int KosEvalState_Call(KosEvalState *es, KosEvent *out_ev,
    PyObject *callable,
    PyObject *const *args, Py_ssize_t nargs, PyObject *kwnames,
    int tail_call) {

  if (! KosCallable_Check(callable)) {
    if (tail_call) {
      KosDynFrame *frame = KosEvalState_FRAME(es);
      KosDynNode *parent = KosDynNode_PARENT((KosDynNode *) frame);
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
    int r = kos_push_or_await(es, out_ev, result);
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
      KosDynNode *parent = KosDynNode_PARENT((KosDynNode *) frame);
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

    int r = kos_push_or_await(es, out_ev, result);
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


int KosEvalState_CallNoArgs(KosEvalState *es, KosEvent *out_ev,
    PyObject *callable, int tail_call) {

  if (! KosCallable_Check(callable)) {
    if (tail_call) {
      KosDynFrame *frame = KosEvalState_FRAME(es);
      KosDynNode *parent = KosDynNode_PARENT((KosDynNode *) frame);
      if (parent && kos_dyn_pop_frame(es) < 0)
        return -1;
    }
    PyObject *result = PyObject_CallNoArgs(callable);
    if (! result)
      return -1;
    int r = kos_push_or_await(es, out_ev, result);
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


/* === Koskari call operations (tail koscall) === */

static inline int KosEvalState_TailKoscallReplaceFrame(
    KosEvalState *es,
    KosCode *code,
    PyObject *ns) {

  KosContext *ctx = KosEvalState_CONTEXT(es);

  if (! ctx || ! KosEvalState_FRAME(es)) {
    PyErr_SetString(PyExc_RuntimeError, "no frame for tail_koscall");
    return -1;
  }

  KosDynFrame *old = KosEvalState_FRAME(es);
  KosDynNode *parent = KosDynNode_PARENT((KosDynNode *) old);
  KosDynFrame *frame;

  KosEvalState_FlushLiveRegsToFrame(es);
  KosContext_DYN_TOP(ctx) = parent;

  frame = kos_dynframe_new(parent, code, ns, NULL);
  if (! frame) {
    KosContext_DYN_TOP(ctx) = (KosDynNode *) old;
    KosEvalState_RefreshDynCaches(es);
    return -1;
  }

  kos_dynframe_free(old);

  KosContext_DYN_TOP(ctx) = (KosDynNode *) frame;
  KosContext_CHAIN_VERSION(ctx)++;
  KosEvalState_RefreshDynCaches(es);

  return 0;
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

/* === KosEvaluatorToken (static implementation) === */

/* === KosEvaluatorToken type === */

static uint64_t token_cookie_counter = 0;

static PyObject *token_repr(PyObject *self) {
  KosEvaluatorToken *t = (KosEvaluatorToken *) self;
  return PyUnicode_FromFormat("<KosEvaluatorToken cookie=%llu live_depth=%d>",
                              (unsigned long long) t->cookie,
                              t->live_depth);
}

PyTypeObject KosEvaluatorTokenType = {
  PyVarObject_HEAD_INIT(NULL, 0)
  .tp_name = "koskari.lib.EvaluatorToken",
  .tp_basicsize = sizeof(KosEvaluatorToken),
  .tp_itemsize = 0,
  .tp_repr = token_repr,
  .tp_flags = Py_TPFLAGS_DEFAULT,
};


static KosEvaluatorToken *
evaluator_token_new(void) {

  KosEvaluatorToken *token = PyObject_New(KosEvaluatorToken,
                                          &KosEvaluatorTokenType);

  if (! token)
    return NULL;

  token->cookie = (uint64_t) __sync_add_and_fetch(&token_cookie_counter, 1);
  token->live_depth = 0;
  return token;
}


void KosEvaluatorToken_IncrementLiveDepth(KosEvaluatorToken *token) {
  if (token)
    token->live_depth++;
}


void KosEvaluatorToken_DecrementLiveDepth(KosEvaluatorToken *token) {
  if (token && token->live_depth > 0)
    token->live_depth--;
}


// The end.
