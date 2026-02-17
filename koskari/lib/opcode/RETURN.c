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

  if (opa == 0) {
    val = Py_None;
    Py_INCREF(val);

  } else if (opa == 1) {
    val = KosStackBlock_PopRef(&CURRENT_STACK(ctx));
    if (! val) {
      SAY("[eval] RETURN pop underrun\n");
      return -1;
    }

  } else {
    val = KosStackBlock_PopTail(&CURRENT_STACK(ctx), (size_t) opa);
    if (! val) {
      SAY("[eval] RETURN PopTail underrun\n");
      return -1;
    }
  }

  /* Frame has parent: return into parent in same loop. */
  if (KosDynNode_PARENT(CURRENT_FRAME(ctx)) != NULL) {
    int was_pending_ctx = (KosContext_PENDING_AGEN(ctx) != NULL);
    KosDynFrame *detached_frame = NULL;
    if (kos_dyn_unwind_to_frame(ctx, CURRENT_FRAME(ctx)) < 0) {
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

  if (kos_dyn_unwind_to_frame(ctx, CURRENT_FRAME(ctx)) < 0) {
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
