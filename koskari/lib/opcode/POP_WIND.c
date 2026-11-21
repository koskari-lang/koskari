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
   POP_WIND: arg unused. Pop the top wind node from the dynamic chain.
   Stores TOS (body result) in the wind node, then unlinks it: the after
   thunk runs if entered and non-null, and the body result is pushed on the
   revealed block once the node is gone.
*/

KOS_OP_HANDLER(POP_WIND) {
  KOS_SYNC_PC(ctx, insn);
  KosDynNode *top = KosContext_DYN_TOP(ctx);
  if (! top || ! KosDynNode_IS_WIND(top)) {
    PyErr_SetString(PyExc_RuntimeError, "POP_WIND: top is not a wind node");
    RESULT_RAISE();
  }

  PyObject *body_result = KosContext_StackPopRef(ctx);
  if (! body_result)
    RESULT_RAISE();
  /* the body value waits on the node for the unlink to deliver */
  if (kos_dyn_ob_store_value(top, body_result) < 0)
    RESULT_RAISE();

  KosDynFrame *const caller = KosContext_FRAME(ctx);
  KosOpResult kr = kos_dyn_unlink_top(ctx, KOS_XFER_EVENT_NONE, 0);
  if (kr.kind == KOS_RES_REPROLOGUE) {
    /* the after-thunk's frame is current: its return settles and
       unlinks the wind. A transfer goal pushed by the call with the frame
       unchanged is the trampoline's to drain before anything past this
       opcode runs. */
    if (KosContext_FRAME(ctx) != caller && ! KosContext_GOAL(ctx))
      RESULT_CONTINUE(ctx);
    return kr;
  }
  if (kr.kind == KOS_RES_AWAIT)
    return kr;  /* the revive settles and unlinks the wind */
  if (kr.kind == KOS_RES_RAISE)
    RESULT_RAISE();
  RESULT_NEXT(ctx);
}


// The end.
