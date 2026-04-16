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
   YIELD: arg = 0 (None), 1 (single value), or N (tuple of N values).
   Pop the value(s) from the stack and suspend with KOS_EV_YIELD.
   Resumption (send value) is handled by py_resume_inject at re-entry.
*/

KOS_OP_HANDLER(YIELD) {
  unsigned opa = KosOp_ARG(insn);
  PyObject *val = NULL;

  if (opa == 0) {
    val = Py_None;
    Py_INCREF(val);

  } else if (opa == 1) {
    val = KosStackBlock_PopRef(&CURRENT_STACK(ctx));
    if (! val) {
      SAY("[eval] YIELD pop underrun\n");
      return KOS_OP_POST_ERR();
    }

  } else {
    val = KosStackBlock_PopTail(&CURRENT_STACK(ctx), (size_t) opa);
    if (! val) {
      SAY("[eval] YIELD PopTail underrun\n");
      return KOS_OP_POST_ERR();
    }
  }

  /* Frame has parent (not GEN): return into parent in same loop
     (CALL return or async gen step).  Parent is GEN => top-level yield. */
  // {
  //   KosDynNode *parent = KosDynNode_PARENT(CURRENT_FRAME(ctx));
  //   if (parent != NULL && ! KosDynNode_IS_GEN(parent)) {
  //     int was_pending_ctx = (KosContext_PENDING_AGEN(ctx) != NULL);
  //     KosPyAsyncGen *agen = was_pending_ctx ? (KosPyAsyncGen *) KosContext_PENDING_AGEN(ctx) : NULL;
  //     if (was_pending_ctx && agen)
  //       agen->in_progress = 0;

  //     KosDynFrame *detached = NULL;
  //     if (kos_dyn_detach_top_frame(ctx, &detached) < 0) {
  //       Py_DECREF(val);
  //       return KOS_OP_POST_ERR();
  //     }

  //     if (was_pending_ctx && agen) {
  //       KosContext_PENDING_AGEN(ctx) = NULL;
  //       Py_XDECREF(KosContext_PENDING_AGEN_DEFAULT(ctx));
  //       KosContext_PENDING_AGEN_DEFAULT(ctx) = NULL;
  //     }

  //     if (unlikely(KosStackBlock_PushRef(&CURRENT_STACK(ctx), val) < 0)) {
  //       Py_DECREF(val);
  //       return KOS_OP_POST_ERR();
  //     }

  //     Py_DECREF(val);
  //     return KOS_OP_RUN();
  //   }
  // }

  KosEvent_KIND(out_ev) = KOS_EV_YIELD;
  KosEvent_PAYLOAD(out_ev) = val;
  KosContext_FLAGS(ctx) &= ~KOS_CTX_RUNNING;
  KosContext_FLAGS(ctx) |= KOS_CTX_DEAD;
  return KOS_OP_POST_SUSPEND();
}
