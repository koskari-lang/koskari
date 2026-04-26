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
   from POP_SHIFT+1 to POP_PROMPT (via extent), unwind to the matching
   prompt, push a SHIFT node (stack block, no frame/namespace), push k
   on stack, and enter the shift block (first instruction = push_pc + 1).
*/

KOS_OP_HANDLER(PUSH_SHIFT) {
  KosContext *ctx = KosEvalState_CONTEXT(es);
  uint32_t prompt_id;
  KosContinuation *k = NULL;
  uint32_t resume_pc;
  uint32_t shift_body_pc;
  KosTransferPlan *plan = NULL;

  if (get_prompt_id_from_insn(KosEvalState_CurrentCode(es), insn, &prompt_id) < 0)
    return KOS_OP_POST_ERR();

  KosDynPrompt *prompt = kos_find_prompt(es, prompt_id);
  if (! prompt) {
    PyErr_SetString(PyExc_RuntimeError, "PUSH_SHIFT: prompt not found");
    return KOS_OP_POST_ERR();
  }

  /* Re-entry after plan completed: push shift node and continuation */
  if (KosDynPrompt_PENDING_SHIFT_WRAPPER(prompt)
      && KosContext_DYN_TOP(ctx) == (KosDynNode *) prompt) {
    PyObject *wrapper = KosDynPrompt_PENDING_SHIFT_WRAPPER(prompt);
    KosDynPrompt_PENDING_SHIFT_WRAPPER(prompt) = NULL;

    shift_body_pc = KosEvalState_GetActingPc(es) + 1;
    if (kos_dyn_push_shift(es) < 0) {
      KosDynPrompt_PENDING_SHIFT_WRAPPER(prompt) = wrapper;
      return KOS_OP_POST_ERR();
    }

    if (KosEvalState_SetFramePc(es, shift_body_pc) < 0)
      return KOS_OP_POST_ERR();
    return kos_op_outcome_from_int_step(push_result(es, wrapper));
  }

  /* Already at prompt: capture, push shift node, done */
  if (KosContext_DYN_TOP(ctx) == (KosDynNode *) prompt) {
    resume_pc = kos_shift_ic_for((PyObject *) KosEvalState_CurrentCode(es),
                                 KosEvalState_GetActingPc(es));
    if (resume_pc == KOS_EXTENT_NONE) {
      PyErr_SetString(PyExc_RuntimeError, "PUSH_SHIFT: no SHIFT extent at pc");
      return KOS_OP_POST_ERR();
    }
    if (kos_shift_capture(es, prompt_id, resume_pc, &k) < 0)
      return KOS_OP_POST_ERR();

    PyObject *wrapper = KosPyDelimitedContinuation_New(k);
    if (! wrapper) {
      kos_continuation_free(k);
      return KOS_OP_POST_ERR();
    }

    shift_body_pc = KosEvalState_GetActingPc(es) + 1;
    if (kos_dyn_push_shift(es) < 0) {
      Py_DECREF(wrapper);
      return KOS_OP_POST_ERR();
    }

    if (KosEvalState_SetFramePc(es, shift_body_pc) < 0)
      return KOS_OP_POST_ERR();
    return kos_op_outcome_from_int_step(push_result(es, wrapper));
  }

  /* Need to unwind: build plan, capture, set continuation on plan, roll back PC */
  resume_pc = kos_shift_ic_for((PyObject *) KosEvalState_CurrentCode(es),
                               KosEvalState_GetActingPc(es));
  if (resume_pc == KOS_EXTENT_NONE) {
    PyErr_SetString(PyExc_RuntimeError, "PUSH_SHIFT: no SHIFT extent at pc");
    return KOS_OP_POST_ERR();
  }

  SAYF("[eval] PUSH_SHIFT prompt_id=%u resume_pc=%u\n",
       (unsigned) prompt_id, (unsigned) resume_pc);
  if (kos_shift_capture(es, prompt_id, resume_pc, &k) < 0) {
    SAY("[eval] PUSH_SHIFT capture failed\n");
    return KOS_OP_POST_ERR();
  }

  PyObject *wrapper = KosPyDelimitedContinuation_New(k);
  if (! wrapper) {
    kos_continuation_free(k);
    return KOS_OP_POST_ERR();
  }

  if (kos_dyn_build_push_shift_unwind_plan(es, prompt, &plan) < 0) {
    Py_DECREF(wrapper);
    return KOS_OP_POST_ERR();
  }

  /* Plan consumes the wrapper's refcount from New; kos_free_transfer_plan
   * Py_XDECREFs continuation_ref. (Unlike kos_invoke_continuation, there is
   * no separate caller-owned ref, so no extra INCREF here.) */
  KosTransferPlan_CONTINUATION_REF(plan) = wrapper;

  if (KosContext_PLAN(ctx))
    kos_free_transfer_plan(KosContext_PLAN(ctx));
  KosContext_PLAN(ctx) = plan;
  KosContext_FLAGS(ctx) |= KOS_CTX_IN_TRANSFER;

  /* Roll back PC so we re-enter PUSH_SHIFT after plan completes */
  if (KosEvalState_SetFramePc(es, KosEvalState_GetActingPc(es)) < 0)
    return KOS_OP_POST_ERR();
  return KOS_OP_PROLOGUE();
}
