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
   PUSH_SHIFT: arg = constant index of prompt_id. Capture the continuation
   from POP_SHIFT+1 to POP_PROMPT (via extent) when the prompt lies in
   acceptable chain heritage (kos_shift_capture locates it and offers
   handle(SHIFT) en route in one walk), push a SHIFT node on the current
   dyn_top, push k on stack, and enter the shift block (first instruction
   = push_pc + 1).  Does not peel nodes above the prompt via a transfer
   plan.
*/

KOS_OP_HANDLER(PUSH_SHIFT) {
  KOS_SYNC_PC(ctx, insn);
  uint32_t prompt_id;
  KosContinuation *k = NULL;
  uint32_t resume_pc;
  uint32_t shift_body_pc;

  if (get_prompt_id_from_insn(ctx, insn, &prompt_id) < 0)
    RESULT_RAISE();

  resume_pc = kos_shift_ic_for((PyObject *) KosEvalState_CurrentCode(ctx),
                               KosEvalState_ACTING_PC(ctx));
  if (resume_pc == KOS_EXTENT_NONE) {
    PyErr_SetString(PyExc_RuntimeError, "PUSH_SHIFT: no SHIFT extent at pc");
    RESULT_RAISE();
  }

  if (kos_shift_capture(ctx, prompt_id, resume_pc, &k) < 0)
    RESULT_RAISE();

  PyObject *wrapper = KosPyDelimitedContinuation_New(k);
  if (! wrapper) {
    kos_continuation_free(k);
    RESULT_RAISE();
  }

  shift_body_pc = KosEvalState_ACTING_PC(ctx) + 1;
  if (kos_dyn_push_shift(ctx, KosContinuation_LIVE_PROMPT_NODE_ID(k)) < 0) {
    Py_DECREF(wrapper);
    RESULT_RAISE();
  }

  /* Record the live SHIFT node identity for in-place (Path A) invoke
   * detection; capture ran before this node existed. */
  {
    KosDynNode *shift_node = KosEvalState_DYN_TOP(ctx);
    KosContinuation_LIVE_SHIFT_NODE(k) = shift_node;
    KosContinuation_LIVE_SHIFT_NODE_ID(k) = kos_dynnode_id(shift_node);
  }

  if (unlikely(KosEvalState_StackPUSH(ctx, wrapper) < 0))
    RESULT_RAISE();
  KosEvalState_SET_PC(ctx, shift_body_pc);
  RESULT_CONTINUE(ctx);
}


// The end.
