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
  koskari/lib/eval/eval_chain.c

  Transfer plan and delimited-return paths; included into eval.c.

  :author: Christopher O'Brien <obriencj@preoccupied.net>
  :license: GNU General Public License v3
*/


/**
   Single entry for eval-driven chain transitions: return walk, unwind, yield
   walk, loop transfer, local raise. Fills out status and raw result codes.
   Callers: eval.c opcode and event dispatch.

   Error paths return -1; caller propagates PyErr.
*/
static inline int kos_dyn_transition_internal(
    KosEvalState *es,
    const KosTransitionReq *req,
    KosTransitionResult *out) {

  if (! es || ! req || ! out) {
    PyErr_SetString(PyExc_RuntimeError,
                    "kos_dyn_transition: invalid arguments");
    return -1;
  }

  out->status = KOS_TRANS_STATUS_OK;
  out->raw = 0;

  switch (req->kind) {
  case KOS_TRANS_WALK_RETURN_TO_FRAME: {
    if (! req->target_frame || ! req->payload) {
      PyErr_SetString(PyExc_RuntimeError,
                      "kos_dyn_transition: WALK_RETURN_TO_FRAME needs "
                      "target_frame and payload");
      return -1;
    }
    int r = kos_dyn_walk_return_to_frame(es, req->target_frame,
                                        req->payload);
    out->raw = r;
    if (r == KOS_DYN_UNWIND_FINALLY_TAKING_RETURN)
      out->status = KOS_TRANS_STATUS_FINALLY_TAKING_RETURN;
    else if (r == KOS_DYN_UNWIND_ASYNC_STARTED)
      out->status = KOS_TRANS_STATUS_ASYNC_UNWIND;
    else if (r < 0) {
      out->status = KOS_TRANS_STATUS_ERROR;
      return -1;
    }
    return 0;
  }
  case KOS_TRANS_UNWIND_TO_FRAME: {
    if (! req->target_frame) {
      PyErr_SetString(PyExc_RuntimeError,
                      "kos_dyn_transition: UNWIND_TO_FRAME needs "
                      "target_frame");
      return -1;
    }
    int r = kos_dyn_unwind_to_frame(es, req->target_frame,
                                    req->xfer_event);
    out->raw = r;
    if (r == KOS_DYN_UNWIND_ASYNC_STARTED)
      out->status = KOS_TRANS_STATUS_ASYNC_UNWIND;
    else if (r < 0) {
      out->status = KOS_TRANS_STATUS_ERROR;
      return -1;
    }
    return 0;
  }
  case KOS_TRANS_UNWIND_TO_NODE: {
    if (! req->target_node) {
      PyErr_SetString(PyExc_RuntimeError,
                      "kos_dyn_transition: UNWIND_TO_NODE needs "
                      "target_node");
      return -1;
    }
    int r = kos_dyn_unwind_to_node(es, req->target_node,
                                  req->xfer_event);
    out->raw = r;
    if (r == KOS_DYN_UNWIND_ASYNC_STARTED)
      out->status = KOS_TRANS_STATUS_ASYNC_UNWIND;
    else if (r < 0) {
      out->status = KOS_TRANS_STATUS_ERROR;
      return -1;
    }
    return 0;
  }
  case KOS_TRANS_WALK_HANDLE_YIELD: {
    int r = kos_dyn_walk_handle_yield(es);
    out->raw = r;
    if (r < 0) {
      out->status = KOS_TRANS_STATUS_ERROR;
      return -1;
    }
    return 0;
  }
  case KOS_TRANS_TRANSFER_TO_LOOP: {
    if (! req->payload) {
      PyErr_SetString(PyExc_RuntimeError,
                      "kos_dyn_transition: TRANSFER_TO_LOOP needs "
                      "payload");
      return -1;
    }
    int r = kos_dyn_transfer_to_loop(es, req->xfer_event, req->payload);
    out->raw = r;
    if (r == KOS_DYN_UNWIND_ASYNC_STARTED)
      out->status = KOS_TRANS_STATUS_ASYNC_UNWIND;
    else if (r < 0) {
      out->status = KOS_TRANS_STATUS_ERROR;
      return -1;
    }
    return 0;
  }
  default:
    PyErr_Format(PyExc_RuntimeError,
                "kos_dyn_transition: unknown kind %d",
                (int) req->kind);
    out->status = KOS_TRANS_STATUS_ERROR;
    return -1;
  }
}


int KosEvalState_Transition(
    KosEvalState *es,
    const KosTransitionReq *req,
    KosTransitionResult *out) {

  return kos_dyn_transition_internal(es, req, out);
}



/* --- Transfer plan and delimited return --- */

/**
  Clear the context's transfer plan and free it.  Plan pointer is nulled
  before freeing so any re-entrant eval (e.g. from Py_XDECREF(continuation_ref)
  triggering Python dealloc/__del__) does not see the plan while it is freed.
*/
static void KosEvalState_DisarmTransferPlan(KosEvalState *es) {
  KosContext *ctx = KosEvalState_CONTEXT(es);
  KosTransferPlan *p = KosEvalState_PLAN(es);
  KosEvalState_PLAN(es) = NULL;
  KosContext_FLAGS(ctx) &= ~KOS_CTX_IN_TRANSFER;
  kos_free_transfer_plan(p);
}


void KosEvalState_SetPendingReturn(
    KosEvalState *es,
    PyObject *val,
    int to_parent) {

  KosContext *ctx = KosEvalState_CONTEXT(es);

  KosContext_PENDING_RETURN_VALUE(ctx) = val;
  KosContext_PENDING_RETURN_TO_PARENT(ctx) = to_parent;
}


/**
  Consume a pending KOS_INJECT_THROW: ownership of both refs transfers
  to the caller (slots are nulled without decref).

  :return: 1 when a throw was taken, 0 when none pending
*/
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


int KosEvalState_SetFrameCodeAndNamespace(
    KosEvalState *es,
    KosDynFrame *frame,
    KosCode *code,
    PyObject *ns) {

  Py_XDECREF(KosDynFrame_NS(frame));
  KosDynFrame_NS(frame) = ns;
  Py_XINCREF(code);
  Py_XDECREF((PyObject *) KosDynFrame_CODE(frame));
  KosDynFrame_CODE(frame) = code;
  if (KosEvalState_FRAME(es) == frame) {
    KosEvalState_NAMESPACE(es) = ns;
    KosEvalState_CODE(es) = code;
    KosEvalState_BLOCK(es) = &KosDynFrame_STACK(frame);
  }
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


#define KosEvalState_EnterDomainScope(es) \
  kos_domain_push_scope(KosEvalState_CONTEXT(es))


#define KosEvalState_LeaveDomainScope(es) \
  kos_domain_pop_scope(KosEvalState_CONTEXT(es))


KosEvaluatorToken *KosEvalState_Token(const KosEvalState *es) {
  if (! es)
    return NULL;
  return KosContext_TOKEN(KosEvalState_CONTEXT(es));
}


void KosEvalState_ArmTransferPlan(KosEvalState *es, KosTransferPlan *plan) {
  KosContext *ctx = KosEvalState_CONTEXT(es);
  KosTransferPlan *old = KosEvalState_PLAN(es);

  if (old)
    kos_free_transfer_plan(old);
  KosEvalState_PLAN(es) = plan;
  KosContext_FLAGS(ctx) |= KOS_CTX_IN_TRANSFER;
}


static inline int KosEvalState_HasActiveTransferPlan(const KosEvalState *es) {

  if (! es)
    return 0;
  return KosEvalState_PLAN(es) != NULL;
}


/**
  True when the current (failed) plan step is a KosDynCCRaise sentinel
  re-emit from kos_build_cc_unwind_plan (foreign apply signal delivery).
*/
static int xfer_plan_step_is_cc_raise_reemit(KosEvalState *es) {

  KosTransferPlan *plan = KosEvalState_PLAN(es);
  if (! plan)
    return 0;

  size_t idx = KosTransferPlan_ACTION_INDEX(plan);
  if (idx >= KosTransferPlan_ACTION_COUNT(plan))
    return 0;

  KosTransferAction *action = &KosTransferPlan_ACTIONS(plan)[idx];
  if (KosTransferAction_STEP(action) != KOS_XFER_EXTENT_ACTION)
    return 0;

  KosDynNode *node = KosTransferAction_EXTENT_NODE(action);
  return node && KosDynNode_IS_CC_RAISE(node);
}


/**
  Foreign cc-unwind sentinel step: inject traceback and disarm nest.
  Not a failed transfer; do not route through process_raise (a local
  handler must not swallow another context's continuation signal).

  :return: EVAL_RAISE with PyErr set
*/
static KosOpResult xfer_emit_cc_unwind_signal(KosEvalState *es) {

  KosContext *ctx = KosEvalState_CONTEXT(es);
  kos_eval_prepare_boundary_raise(ctx, es);
  kos_abandon_transfer_nest(es);
  return EVAL_RAISE();
}


/**
  Run the current transfer plan to completion.  Step failures route
  through process_raise (the unified RAISE ingress), except the foreign
  cc-unwind sentinel re-emit and a pending KoskariContinuationApply
  between drain steps (expected in-flight state; keep stepping).

  :return: EVAL_NORMAL on success (plan cleared or suspended on await),
           EVAL_REPROLOGUE if the plan was abandoned and a local handler
           was applied,
           EVAL_AWAIT if abandonment started an async unwind (suspension
           tagged; val is the awaitable),
           EVAL_RAISE / EVAL_FATAL if a step failed with no handler
*/
static KosOpResult run_transfer_plan_to_completion(KosEvalState *es) {

  while (KosEvalState_PLAN(es)) {
    if (PyErr_Occurred()) {
      if (! (KoskariContinuationApply
             && PyErr_ExceptionMatches(KoskariContinuationApply))) {
        SAY("[eval] transfer drain with pending PyErr -> process_raise\n");
        return process_raise(es);
      }
    }

    SAYF("[eval] transfer step %zu/%zu\n",
         (size_t) KosTransferPlan_ACTION_INDEX(KosEvalState_PLAN(es)),
         (size_t) KosTransferPlan_ACTION_COUNT(KosEvalState_PLAN(es)));
    int plan_res = kos_run_transfer_step(es);

    if (plan_res < 0) {
      if (xfer_plan_step_is_cc_raise_reemit(es)) {
        SAY("[eval] cc-unwind sentinel re-emit -> raise\n");
        return xfer_emit_cc_unwind_signal(es);
      }
      SAY("[eval] transfer step failed -> process_raise\n");
      return process_raise(es);
    }

    if (plan_res == KOS_XFER_SUSPENDED_AWAIT) {
      SAY("[eval] transfer step suspended on await\n");
      /* do not clear plan; driver will resume and feed inject */
      return EVAL_NORMAL();
    }

    if (plan_res == 0)
      continue;

    SAYF("[eval] transfer complete dyn_top=%p\n",
         (void *) KosEvalState_DYN_TOP(es));
    KosEvalState_DisarmTransferPlan(es);

    if (! KosEvalState_PLAN_STACK(es))
      break;

    /* An outer transfer was suspended when a wind thunk invoked a nested
       continuation.  That inner transfer just completed.  Rebuild a fresh
       plan from the current chain position to the outer plan's
       original_target using kos_build_direct_install_plan, then continue
       draining the new plan. */
    kos_resume_transfer_plan(es);
    {
      KosTransferPlan *outer = KosEvalState_PLAN(es);

      if (kos_xfer_plan_is_delimited_push(outer)) {
        PyErr_SetString(PyExc_RuntimeError,
                        "nested continuation invoke during an active"
                        " delimited transfer is not supported");
        return process_raise(es);
      }

      size_t outer_ac = KosTransferPlan_ACTION_COUNT(outer);
      KosTransferAction *inject_a =
          &KosTransferPlan_ACTIONS(outer)[outer_ac - 1];
      KosDynNode *target = KosTransferPlan_ORIGINAL_TARGET(outer);
      PyObject *inject_val = KosTransferAction_INJECT_VALUE(inject_a);
      int inject_throw = KosTransferAction_INJECT_IS_THROW(inject_a);
      uint32_t rpc = KosTransferPlan_RESUME_PC(outer);
      KosStackBlock *rstack = KosTransferPlan_RESUME_STACK(outer);
      uint32_t graft_id = KosTransferPlan_GRAFT_POINT_ID(outer);
      KosDynNode *target_keep = NULL;

      Py_XINCREF(inject_val);
      KosTransferPlan_RESUME_STACK(outer) = NULL;

      if (target) {
        KosDynNode *bottom = target;
        KosDynNode *stop;

        while (bottom && KosDynNode_PARENT(bottom))
          bottom = KosDynNode_PARENT(bottom);
        stop = bottom ? KosDynNode_PARENT(bottom) : NULL;
        target_keep = kos_clone_chain_segment(target, stop);
        if (! target_keep) {
          Py_XDECREF(inject_val);
          if (rstack)
            KosStackBlock_ReleaseChain(rstack);
          return process_raise(es);
        }
        bottom = target_keep;
        while (bottom && KosDynNode_PARENT(bottom))
          bottom = KosDynNode_PARENT(bottom);
        KosDynNode_PARENT(bottom) = NULL;
      }

      KosEvalState_DisarmTransferPlan(es);

      if (! target_keep) {
        Py_XDECREF(inject_val);
        if (rstack)
          KosStackBlock_ReleaseChain(rstack);
        break;
      }

      KosTransferPlan *new_plan = NULL;
      if (kos_build_direct_install_plan(es, target_keep, inject_val,
                                          inject_throw, graft_id,
                                          &new_plan) < 0) {
        Py_XDECREF(inject_val);
        if (rstack)
          KosStackBlock_ReleaseChain(rstack);
        kos_dynchain_free(target_keep, NULL);
        return process_raise(es);
      }
      Py_XDECREF(inject_val);
      KosTransferPlan_RESUME_PC(new_plan) = rpc;
      KosTransferPlan_RESUME_FRAME(new_plan) = NULL;
      KosTransferPlan_RESUME_STACK(new_plan) = rstack;
      KosEvalState_ArmTransferPlan(es, new_plan);
    }
  }

  return EVAL_NORMAL();
}


/**
   Handle delimited k(value) return: build plan, install it, run to completion.
   Caller has popped val; we consume it. Return 0 on success, -1 on error.
*/
static inline KosOpResult KosEvalState_KReturnDelimited(
    KosEvalState *es,
    KosDynDelim *delim,
    PyObject *val) {

  KosDynNode *ret_chain = KosDynDelim_RETURN_CHAIN(delim);
  uint32_t ret_pc = KosDynDelim_RETURN_PC(delim);

  SAYF("[RETURN] taking return-target path: return_pc=%u",
       (unsigned) ret_pc);
  KosTransferPlan *plan = NULL;
  if (kos_build_return_plan(es, ret_chain,
                            val, 0, &plan) < 0) {
    Py_DECREF(val);
    return EVAL_RAISE();
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
  if (KosEvalState_DYN_TOP(es) == (KosDynNode *) delim) {
    if (kos_dyn_pop_delim(es, delim) < 0)
      return EVAL_RAISE();
  }
  {
    KosOpResult xfer = run_transfer_plan_to_completion(es);
    if (xfer.kind == KOS_RES_REPROLOGUE)
      return EVAL_REPROLOGUE();
    if (xfer.kind != KOS_RES_NORMAL)
      return xfer;  /* AWAIT / RAISE / FATAL */
  }
  return KosEvalState_HasActiveTransferPlan(es) ? EVAL_REPROLOGUE()
                                                : EVAL_NORMAL();
}
