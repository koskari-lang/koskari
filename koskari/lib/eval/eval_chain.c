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
    KosTransitionResult *out,
    KosEvent *out_ev) {

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
                                        req->payload, out_ev);
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
                                    req->xfer_event, out_ev);
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
                                  req->xfer_event, out_ev);
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
    int r = kos_dyn_walk_handle_yield(es, out_ev);
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
    int r = kos_dyn_transfer_to_loop(es, req->xfer_event, req->payload,
                                     out_ev);
    out->raw = r;
    if (r == KOS_DYN_UNWIND_ASYNC_STARTED)
      out->status = KOS_TRANS_STATUS_ASYNC_UNWIND;
    else if (r < 0) {
      out->status = KOS_TRANS_STATUS_ERROR;
      return -1;
    }
    return 0;
  }
  case KOS_TRANS_TRY_LOCAL_RAISE: {
    int r = kos_dyn_try_local_raise_body(es, out_ev);
    out->raw = r;
    if (r < 0) {
      out->status = KOS_TRANS_STATUS_ERROR;
      return -1;
    }
    if (r == 1)
      out->status = KOS_TRANS_STATUS_HANDLER_APPLIED;
    else
      out->status = KOS_TRANS_STATUS_NO_LOCAL_HANDLER;
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
    KosTransitionResult *out,
    KosEvent *out_ev) {

  return kos_dyn_transition_internal(es, req, out, out_ev);
}



/* --- Transfer plan and delimited return --- */

/**
  Clear the context's transfer plan and free it.  Plan pointer is nulled
  before freeing so any re-entrant eval (e.g. from Py_XDECREF(continuation_ref)
  triggering Python dealloc/__del__) does not see the plan while it is freed.
  For PUSH_SHIFT unwind plans, transfers continuation_ref to the prompt's
  pending_shift_wrapper so the opcode can push the handler on re-entry.
*/
static void clear_and_free_transfer_plan(KosEvalState *es) {
  KosContext *ctx = KosEvalState_CONTEXT(es);
  KosTransferPlan *p = KosEvalState_PLAN(es);
  KosEvalState_PLAN(es) = NULL;
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
  if (unlikely(! es))
    return;
  clear_and_free_transfer_plan(es);
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
  Disarm the active transfer plan and free any suspended outers on plan_stack.
  Does not clear PyErr or mutate dyn_top / frame_top / stack.
*/
static void kos_abandon_transfer_nest(KosEvalState *es) {

  KosEvalState_DisarmTransferPlan(es);
  kos_free_transfer_plan_stack(es);
}


/**
  Abandon any transfer nest, then dispatch PyErr through local handlers.

  :return: 1 if a handler was applied, -1 if unhandled raise or fatal error
*/
static int abandon_xfer_and_try_local(KosEvalState *es, KosEvent *out_ev) {

  kos_abandon_transfer_nest(es);
  int tl = try_local_exception_handler(es, out_ev);
  if (tl > 0)
    return 1;
  if (tl < 0)
    return -1;
  return -1;
}


/**
  Run the current transfer plan to completion.

  :return: 0 on success (plan cleared or suspended on await),
           1 if plan abandoned and a local handler was applied,
           -1 if a step failed with no handler or on fatal error
*/
static int run_transfer_plan_to_completion(
    KosEvalState *es,
    KosEvent *out_ev) {

  while (KosEvalState_PLAN(es)) {
    SAYF("[eval] transfer step %zu/%zu\n",
         (size_t) KosTransferPlan_ACTION_INDEX(KosEvalState_PLAN(es)),
         (size_t) KosTransferPlan_ACTION_COUNT(KosEvalState_PLAN(es)));
    int plan_res = kos_run_transfer_step(es, out_ev);

    if (plan_res < 0) {
      SAY("[eval] transfer step failed -> abandon + try_local\n");
      return abandon_xfer_and_try_local(es, out_ev);
    }

    if (plan_res == KOS_XFER_SUSPENDED_AWAIT) {
      SAY("[eval] transfer step suspended on await\n");
      return 0;  /* do not clear plan; driver will resume and feed inject */
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
    KosTransferPlan *outer = KosEvalState_PLAN(es);
    size_t outer_ac = KosTransferPlan_ACTION_COUNT(outer);
    KosTransferAction *inject_a = &KosTransferPlan_ACTIONS(outer)[outer_ac - 1];
    KosDynNode  *target       = KosTransferPlan_ORIGINAL_TARGET(outer);
    PyObject    *inject_val   = KosTransferAction_INJECT_VALUE(inject_a);
    int          inject_throw = KosTransferAction_INJECT_IS_THROW(inject_a);
    uint32_t     rpc          = KosTransferPlan_RESUME_PC(outer);
    KosStackBlock *rstack     = KosTransferPlan_RESUME_STACK(outer);
    uint32_t     graft_id     = KosTransferPlan_GRAFT_POINT_ID(outer);
    /* Take a ref on inject_val before DisarmTransferPlan releases the plan's
       copy, and steal resume_stack so the plan does not release it. */
    Py_XINCREF(inject_val);
    KosTransferPlan_RESUME_STACK(outer) = NULL;

    KosDynNode *target_keep = NULL;
    if (target) {
      KosDynNode *bottom = target;
      while (bottom && KosDynNode_PARENT(bottom))
        bottom = KosDynNode_PARENT(bottom);
      KosDynNode *stop = bottom ? KosDynNode_PARENT(bottom) : NULL;
      target_keep = kos_clone_chain_segment(target, stop);
      if (! target_keep) {
        Py_XDECREF(inject_val);
        if (rstack)
          KosStackBlock_ReleaseChain(rstack);
        return abandon_xfer_and_try_local(es, out_ev);
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
    if (kos_build_direct_install_plan(es, target_keep, inject_val, inject_throw,
                                      graft_id, &new_plan) < 0) {
      Py_XDECREF(inject_val);
      if (rstack)
        KosStackBlock_ReleaseChain(rstack);
      kos_dynchain_free(target_keep, NULL);
      return abandon_xfer_and_try_local(es, out_ev);
    }
    /* kos_build_direct_install_plan incremented inject_val; release our ref. */
    Py_XDECREF(inject_val);
    KosTransferPlan_RESUME_PC(new_plan) = rpc;
    KosTransferPlan_RESUME_FRAME(new_plan) = NULL;
    KosTransferPlan_RESUME_STACK(new_plan) = rstack;
    KosEvalState_ArmTransferPlan(es, new_plan);
    /* Continue to drain the new plan in the next loop iteration. */
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
  if (KosEvalState_DYN_TOP(es) == (KosDynNode *) delim) {
    if (kos_dyn_pop_delim(es, delim) < 0)
      return -1;
  }
  {
    int xfer = run_transfer_plan_to_completion(es, out_ev);
    if (xfer < 0)
      return -1;
  }
  return 0;
}
