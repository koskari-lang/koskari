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
  unsigned opa = KosOp_ARG(insn);
  PyObject *val = NULL;

  /* === Phase 1: Pop return value from stack ===
     opa=0 => Py_None, opa=1 => single value, opa=N => tuple of N values */
  SAYF("[RETURN] opa=%u frame=%p stack=%p len=%zu (pc advanced to %u)",
       opa, (void *) CURRENT_FRAME(ctx), (void *) CURRENT_STACK(ctx),
       CURRENT_STACK(ctx) ? KosStackBlock_LEN(CURRENT_STACK(ctx)) : 0,
       (unsigned) CURRENT_PC(ctx));

  if (opa == 0) {
    val = Py_None;
    Py_INCREF(val);

  } else if (opa == 1) {
    val = KosStackBlock_PopRef(&CURRENT_STACK(ctx));
    if (! val) {
      SAYF("[RETURN] pop underrun: stack=%p len=%zu",
           (void *) CURRENT_STACK(ctx),
           CURRENT_STACK(ctx) ? (unsigned long) KosStackBlock_LEN(CURRENT_STACK(ctx)) : 0UL);
      PyErr_SetString(PyExc_RuntimeError, "RETURN: stack underrun");
      return KOS_OP_POST_ERR();
    }

  } else {
    val = KosStackBlock_PopTail(&CURRENT_STACK(ctx), (size_t) opa);
    if (! val) {
      SAY("[eval] RETURN PopTail underrun\n");
      PyErr_SetString(PyExc_RuntimeError, "RETURN: stack underrun");
      return KOS_OP_POST_ERR();
    }
  }

  /* === Phase 2a: Delimited continuation return (k(value)) ===
     When inside a reset/prompt, k(value) returns the value to the call site
     of the continuation. This bypasses the normal parent chain. Rare path. */
  if (unlikely(KosContext_RETURN_TARGET_CHAIN(ctx) != NULL
               && KosContext_RETURN_TARGET_PC(ctx) != (uint32_t) -1)) {
    int rd = kos_return_delimited(ctx, val, out_ev);
    if (rd < 0)
      return KOS_OP_POST_ERR();
    return KosContext_PLAN(ctx) ? KOS_OP_PROLOGUE() : KOS_OP_RUN();
  }

  /* === Phase 2b: Frame has parent (normal call return) ===
     We are returning from a callee into its caller. Unwind (run wind/finally
     etc), detach our frame, then deliver the value to the parent.
     Skip when parent is GEN (generator boundary): treat as top-level return. */
  {
    KosDynNode *parent = KosDynNode_PARENT(CURRENT_FRAME(ctx));
    if (parent != NULL && ! KosDynNode_IS_GEN(parent)) {
    KosDynNext *pending_next = kos_dyn_find_next_above_frame(ctx);
    KosDynForeach *pending_foreach = kos_dyn_find_awaiting_foreach(ctx);
    // KosDynFrame *detached_frame = NULL;

    /* Walk RETURN to frame (handle phase: FINALLY may consume and run). */
    KosTransitionReq treq = {
      .kind = KOS_TRANS_WALK_RETURN_TO_FRAME,
      .target_frame = CURRENT_FRAME(ctx),
      .payload = val,
    };
    KosTransitionResult tres = {0};
    if (kos_dyn_transition(ctx, &treq, &tres, out_ev) < 0) {
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

    /* Detach our frame; parent becomes DYN_TOP. */
    // if (kos_dyn_detach_top_frame(ctx, &detached_frame) < 0) {
    //   Py_DECREF(val);
    //   return KOS_OP_POST_ERR();
    // }
    // kos_dynframe_free(detached_frame);
    if (unlikely(kos_dyn_pop_frame(ctx) < 0)) {
      Py_DECREF(val);
      return KOS_OP_POST_ERR();
    }

    /* Async gen special case: flattened Koskari async gen exhausted (ITER_NEXT
       or standalone NEXT). Rare path. */
    if (unlikely(pending_foreach && ! pending_next)) {
      KosPyAsyncGen *agen =
          (KosPyAsyncGen *) KosDynForeach_ITER(pending_foreach);
      (void) agen;
      agen->closed = 1;
      agen->in_progress = 0;
      KosDynForeach_EXHAUSTED(pending_foreach) = 1;
      CURRENT_PC(ctx) = KosDynForeach_ITER_NEXT_ELSE_PC(pending_foreach);
      KosDynForeach_ITER_STEP_PHASE(pending_foreach) = KOS_FOREACH_ITER_STEP_IDLE;
      Py_DECREF(val);
      return KOS_OP_RUN();
    }
    if (unlikely(pending_next)) {
      KosPyAsyncGen *agen =
          (KosPyAsyncGen *) KosDynNext_AGEN_OBJ((KosDynNode *) pending_next);
      int n = KosDynNext_N((KosDynNode *) pending_next);
      agen->closed = 1;
      agen->in_progress = 0;
      if (n == 2 && KosDynNext_DEFAULT_VAL((KosDynNode *) pending_next)) {
        if (KosStackBlock_PushRef(
                &CURRENT_STACK(ctx),
                KosDynNext_DEFAULT_VAL((KosDynNode *) pending_next)) < 0) {
          Py_DECREF(val);
          kos_dyn_unlink_next_node(ctx, pending_next);
          return KOS_OP_POST_ERR();
        }
        Py_DECREF(val);
      } else if (n == 1) {
        PyErr_SetNone(PyExc_StopIteration);
        Py_DECREF(val);
        kos_dyn_unlink_next_node(ctx, pending_next);
        return KOS_OP_POST_ERR();
      } else {
        Py_DECREF(val);
      }
      kos_dyn_unlink_next_node(ctx, pending_next);
      return KOS_OP_RUN();
    }

    /* Deliver value to parent: native handler takes it, else push onto stack.
       Returning KOS_OP_RES_CONTROL_RETURNED triggers post_opcode to run
       kos_dynnode_on_control_returned (e.g. wind handler for thunk completion). */
    if (unlikely(KosDynFrame_NATIVE_HANDLER(CURRENT_FRAME(ctx)) != NULL)) {
      typedef int (*NativeHandlerFn)(void *, void *, int, PyObject *);
      NativeHandlerFn fn =
          (NativeHandlerFn) KosDynFrame_NATIVE_HANDLER(CURRENT_FRAME(ctx));
      int res = fn(ctx, out_ev, 1, val);
      Py_DECREF(val);
      return kos_op_outcome_from_int_step(res);
    }
    if (KosStackBlock_PushRef(&CURRENT_STACK(ctx), val) < 0) {
      Py_DECREF(val);
      return KOS_OP_POST_ERR();
    }
    Py_DECREF(val);
    return KOS_OP_POST_CONTROL();
  }
  }

  /* === Phase 2c: Top-level return (no parent or parent is GEN) ===
     We are the root frame; returning means the evaluator is done. */
  {
    KosTransitionReq treq = {
      .kind = KOS_TRANS_WALK_RETURN_TO_FRAME,
      .target_frame = CURRENT_FRAME(ctx),
      .payload = val,
    };
    KosTransitionResult tres = {0};
    if (kos_dyn_transition(ctx, &treq, &tres, out_ev) < 0) {
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
