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
   RETURN: arg = 0 (None), 1 (single value), or N (tuple of N values).
   Pop the return value(s) from the stack and stop the context with
   KOS_EV_RETURN.
*/

KOS_OP_HANDLER(RETURN) {
  KosContext *ctx = KosEvalState_CONTEXT(es);
  unsigned opa = KosOp_ARG(insn);
  PyObject *val = NULL;

  /* === Phase 1: Pop return value from stack ===
     opa=0 => Py_None, opa=1 => single value, opa=N => tuple of N values */
  SAYF("[RETURN] opa=%u frame=%p stack=%p len=%zu (pc advanced to %u)",
       opa, (void *) KosEvalState_FRAME(es),
       (void *) KosEvalState_StackHead(es),
       KosEvalState_StackLen(es),
       (unsigned) KosEvalState_PC(es));

  if (opa == 0) {
    val = Py_None;
    Py_INCREF(val);

  } else if (opa == 1) {
    val = KosEvalState_StackPopRef(es);
    if (! val) {
      SAYF("[RETURN] pop underrun: stack=%p len=%zu",
           (void *) KosEvalState_StackHead(es),
           (unsigned long) KosEvalState_StackLen(es));
      PyErr_SetString(PyExc_RuntimeError, "RETURN: stack underrun");
      return KOS_OP_POST_ERR();
    }

  } else {
    val = KosEvalState_StackPopTail(es, (size_t) opa);
    if (! val) {
      SAY("[eval] RETURN PopTail underrun\n");
      PyErr_SetString(PyExc_RuntimeError, "RETURN: stack underrun");
      return KOS_OP_POST_ERR();
    }
  }

  /* === Phase 2a: Delimited continuation return (k(value)) ===
     When inside a reset/prompt, k(value) returns the value to the call site
     of the continuation. This bypasses the normal parent chain. Rare path. */
  KosDynDelim *ret_delim = kos_find_nearest_delim(es, KOS_DYN_DELIM_MODE_K_RETURN);
  if (unlikely(ret_delim
               && KosDynDelim_RETURN_CHAIN(ret_delim) != NULL
               && KosDynDelim_RETURN_PC(ret_delim) != (uint32_t) -1)) {
    int rd = kos_return_delimited(es, ret_delim, val, out_ev);
    if (rd < 0)
      return KOS_OP_POST_ERR();
    return KosContext_PLAN(ctx) ? KOS_OP_PROLOGUE() : KOS_OP_RUN();
  }

  /* === Phase 2b: Frame has parent (normal call return) ===
     We are returning from a callee into its caller. Unwind (run wind/finally
     etc), detach our frame, then deliver the value to the parent.
     Skip when parent is GEN (generator boundary): treat as top-level return. */
  {
    KosDynNode *parent = KosDynNode_PARENT(KosEvalState_FRAME(es));
    if (parent != NULL && ! KosDynNode_IS_GEN(parent)) {

    /* Walk RETURN to frame (handle phase: FINALLY may consume and run). */
    KosTransitionReq treq = {
      .kind = KOS_TRANS_WALK_RETURN_TO_FRAME,
      .target_frame = KosEvalState_FRAME(es),
      .payload = val,
    };
    KosTransitionResult tres = {0};
    if (kos_dyn_transition(es, &treq, &tres, out_ev) < 0) {
      Py_DECREF(val);
      return KOS_OP_POST_ERR();
    }
    int unw = tres.raw;
    if (unw == KOS_DYN_UNWIND_ASYNC_STARTED) {
      KosContext_PENDING_RETURN_VALUE(ctx) = val;
      KosContext_PENDING_RETURN_TO_PARENT(ctx) = 1;
      return KOS_OP_RUN();
    }
    if (unw == KOS_DYN_UNWIND_FINALLY_TAKING_RETURN) {
      /* Delivered to finally block; eval continues at finally (val on block) */
      return KOS_OP_RUN();
    }
    if (unw < 0) {
      Py_DECREF(val);
      return KOS_OP_POST_ERR();
    }

    if (unlikely(kos_dyn_pop_frame(es) < 0)) {
      Py_DECREF(val);
      return KOS_OP_POST_ERR();
    }

    /* Stage B: eval runs iterator-boundary delivery, native handler, stack
       push, and kos_dynnode_on_control_returned via eval_parent_return_deliver. */
    KosEvent_KIND(out_ev) = KOS_EV_RETURN;
    KosEvent_PAYLOAD(out_ev) = val;
    return KOS_OP_POST_PARENT_RETURN();
  }
  }

  /* === Phase 2c: Top-level return (no parent or parent is GEN) ===
     We are the root frame; returning means the evaluator is done. */
  {
    KosTransitionReq treq = {
      .kind = KOS_TRANS_WALK_RETURN_TO_FRAME,
      .target_frame = KosEvalState_FRAME(es),
      .payload = val,
    };
    KosTransitionResult tres = {0};
    if (kos_dyn_transition(es, &treq, &tres, out_ev) < 0) {
      Py_DECREF(val);
      return KOS_OP_POST_ERR();
    }
    int unw = tres.raw;
    if (unw == KOS_DYN_UNWIND_ASYNC_STARTED) {
      KosContext_PENDING_RETURN_VALUE(ctx) = val;
      KosContext_PENDING_RETURN_TO_PARENT(ctx) = 0;
      return KOS_OP_RUN();
    }
    if (unw == KOS_DYN_UNWIND_FINALLY_TAKING_RETURN) {
      return KOS_OP_RUN();
    }
    if (unw < 0) {
      Py_DECREF(val);
      return KOS_OP_POST_ERR();
    }

    /* Emit boundary event: eval is complete. */
    SAYF("[eval] RETURN count=%u -> KOS_EV_RETURN\n", opa);
    SAYF("[eval] RETURN payload: %R\n", val);
    KosEvent_KIND(out_ev) = KOS_EV_RETURN;
    KosEvent_PAYLOAD(out_ev) = val;
    KosContext_FLAGS(ctx) &= ~KOS_CTX_RUNNING;
    KosContext_FLAGS(ctx) |= KOS_CTX_DEAD;
    return KOS_OP_POST_SUSPEND();
  }
}
