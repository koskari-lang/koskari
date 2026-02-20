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

  /* PC already advanced: we are post-RETURN insn dispatch */
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
      return -1;
    }

  } else {
    val = KosStackBlock_PopTail(&CURRENT_STACK(ctx), (size_t) opa);
    if (! val) {
      SAY("[eval] RETURN PopTail underrun\n");
      return -1;
    }
  }

  /* Delimited k(value) return path: transfer back to invocation site. */
  if (KosContext_RETURN_TARGET_CHAIN(ctx) != NULL
      && KosContext_RETURN_TARGET_PC(ctx) != (uint32_t) -1) {
    SAYF("[RETURN] taking return-target path: return_pc=%u", (unsigned) KosContext_RETURN_TARGET_PC(ctx));
    KosTransferPlan *plan = NULL;
    if (kos_build_return_plan(ctx, KosContext_RETURN_TARGET_CHAIN(ctx),
                              val, 0, &plan) < 0) {
      Py_DECREF(val);
      return -1;
    }
    Py_DECREF(val);
    KosTransferPlan_RESUME_PC(plan) = KosContext_RETURN_TARGET_PC(ctx);
    KosTransferPlan_RESUME_FRAME(plan) = NULL;
    KosTransferPlan_RESUME_STACK(plan) = NULL;
    if (KosContext_PLAN(ctx))
      kos_free_transfer_plan(KosContext_PLAN(ctx));
    KosContext_PLAN(ctx) = plan;
    KosContext_FLAGS(ctx) |= KOS_CTX_IN_TRANSFER;
    KosTransferPlan_CONTINUATION_REF(plan) = NULL;
    kos_dynchain_free(KosContext_RETURN_TARGET_CHAIN(ctx), NULL);
    KosContext_RETURN_TARGET_CHAIN(ctx) = NULL;
    KosContext_RETURN_TARGET_PC(ctx) = (uint32_t) -1;
    if (run_transfer_plan_to_completion(ctx, out_ev) < 0)
      return -1;
    return 0;
  }

  /* Frame has parent: return into parent in same loop. */
  if (KosDynNode_PARENT(CURRENT_FRAME(ctx)) != NULL) {
    int was_pending_ctx = (KosContext_PENDING_AGEN(ctx) != NULL);
    KosDynFrame *detached_frame = NULL;
    int unw = kos_dyn_unwind_to_frame(ctx, CURRENT_FRAME(ctx), out_ev);
    if (unw == KOS_DYN_UNWIND_ASYNC_STARTED) {
      KosContext_PENDING_RETURN_VALUE(ctx) = val;
      KosContext_PENDING_RETURN_TO_PARENT(ctx) = 1;
      return 0;
    }
    if (unw < 0) {
      Py_DECREF(val);
      return -1;
    }
    if (kos_dyn_detach_top_frame(ctx, &detached_frame) < 0) {
      Py_DECREF(val);
      return -1;
    }
    KosPyAsyncGen *agen = was_pending_ctx ? (KosPyAsyncGen *) KosContext_PENDING_AGEN(ctx) : NULL;

    if (was_pending_ctx && agen) {
      agen->closed = 1;
      agen->in_progress = 0;
      if (KosContext_PENDING_AGEN_N(ctx) == 0) {
        /* ITER_NEXT case: exhausted; jump to else (region and else_pc on region) */
        KosDynRegion *r = kos_find_region_for_loop(ctx);
        if (r) {
          KosDynRegion_EXHAUSTED(r) = 1;
          CURRENT_PC(ctx) = KosDynRegion_ITER_NEXT_ELSE_PC(r);
        }
        KosContext_PENDING_AGEN(ctx) = NULL;
        KosContext_PENDING_AGEN_DEFAULT(ctx) = NULL;
        Py_DECREF(val);
        return 0;
      }
      if (KosContext_PENDING_AGEN_N(ctx) == 2 && KosContext_PENDING_AGEN_DEFAULT(ctx)) {
        if (KosStackBlock_PushRef(&CURRENT_STACK(ctx),
                                  KosContext_PENDING_AGEN_DEFAULT(ctx)) < 0) {
          Py_XDECREF(KosContext_PENDING_AGEN_DEFAULT(ctx));
          KosContext_PENDING_AGEN_DEFAULT(ctx) = NULL;
          KosContext_PENDING_AGEN(ctx) = NULL;
          Py_DECREF(val);
          return -1;
        }
        Py_XDECREF(KosContext_PENDING_AGEN_DEFAULT(ctx));
        Py_DECREF(val);
      } else if (KosContext_PENDING_AGEN_N(ctx) == 1) {
        PyErr_SetNone(PyExc_StopIteration);
        Py_DECREF(val);
        KosContext_PENDING_AGEN(ctx) = NULL;
        KosContext_PENDING_AGEN_DEFAULT(ctx) = NULL;
        return -1;
      } else {
        Py_DECREF(val);
      }
      KosContext_PENDING_AGEN(ctx) = NULL;
      KosContext_PENDING_AGEN_DEFAULT(ctx) = NULL;
      return 0;
    }
    /* Normal CALL return: push value onto parent. */
    if (KosStackBlock_PushRef(&CURRENT_STACK(ctx), val) < 0) {
      Py_DECREF(val);
      return -1;
    }
    Py_DECREF(val);
    return 0;
  }

  int unw = kos_dyn_unwind_to_frame(ctx, CURRENT_FRAME(ctx), out_ev);
  if (unw == KOS_DYN_UNWIND_ASYNC_STARTED) {
    KosContext_PENDING_RETURN_VALUE(ctx) = val;
    KosContext_PENDING_RETURN_TO_PARENT(ctx) = 0;
    return 0;
  }
  if (unw < 0) {
    Py_DECREF(val);
    return -1;
  }
  SAYF("[eval] RETURN count=%u -> KOS_EV_RETURN\n", opa);
  SAYF("[eval] RETURN payload: %R\n", val);
  KosEvent_KIND(out_ev) = KOS_EV_RETURN;
  KosEvent_PAYLOAD(out_ev) = val;
  KosContext_FLAGS(ctx) &= ~KOS_CTX_RUNNING;
  KosContext_FLAGS(ctx) |= KOS_CTX_DEAD;
  return 1;
}
