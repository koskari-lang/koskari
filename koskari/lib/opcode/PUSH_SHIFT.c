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
   PUSH_SHIFT: arg = constant index of the tag selector: 0 for the
   default prompt tag, None for a tag popped from the stack before the
   capture, so it is not among the captured operands. Capture the continuation
   from POP_SHIFT+1 to POP_PROMPT (via extent) when the prompt lies in
   acceptable chain heritage (kos_shift_capture locates it and offers
   handle(SHIFT) en route in one walk), push a SHIFT node on the current
   dyn_top, push k on stack, and enter the shift block (first instruction
   = push_pc + 1).  Does not peel nodes above the prompt via a transfer
   plan.
*/

KOS_OP_HANDLER(PUSH_SHIFT) {
  KOS_SYNC_PC(ctx, insn);
  PyObject *tag;
  KosContinuation *k = NULL;
  KosOp *resume;
  int captured;

  if (get_prompt_tag_from_insn(ctx, insn, &tag) < 0)
    RESULT_RAISE();

  resume = kos_shift_ic_for((PyObject *) KosContext_FrameCode(ctx),
                            KosOp_PC(insn));
  if (! resume) {
    Py_DECREF(tag);
    PyErr_SetString(PyExc_RuntimeError, "PUSH_SHIFT: no SHIFT extent at pc");
    RESULT_RAISE();
  }

  captured = kos_shift_capture(ctx, tag, resume, &k);
  Py_DECREF(tag);
  if (captured < 0)
    RESULT_RAISE();

  PyObject *wrapper = KosPyDelimitedContinuation_New(k);
  if (! wrapper) {
    kos_continuation_free(k);
    RESULT_RAISE();
  }

  if (kos_dyn_push_shift(ctx, KosContinuation_LIVE_PROMPT_NODE_ID(k)) < 0) {
    Py_DECREF(wrapper);
    RESULT_RAISE();
  }

  /* Record the live SHIFT node identity for in-place (Path A) invoke
   * detection; capture ran before this node existed. */
  {
    KosDynNode *shift_node = KosContext_DYN_TOP(ctx);
    KosContinuation_LIVE_SHIFT_NODE(k) = shift_node;
    KosContinuation_LIVE_SHIFT_NODE_ID(k) = kos_dynnode_id(shift_node);
  }

  if (unlikely(KosContext_StackPUSH(ctx, wrapper) < 0))
    RESULT_RAISE();
  /* the shift body starts at the next instruction */
  KosContext_SET_PC(ctx, ((KosOp *) insn) + 1);
  RESULT_CONTINUE(ctx);
}


// The end.
