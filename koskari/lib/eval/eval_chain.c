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

   Error paths return -1 without syncing es; KosEvalState_Transition performs
   at most one KosEvalState_SyncFromContext on -1 when es is non-NULL.
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

int r = kos_dyn_transition_internal(es, req, out, out_ev);

if (r < 0)
  KosEvalState_SyncFromContext(es);
return r;
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
  if (unlikely(! es))
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
