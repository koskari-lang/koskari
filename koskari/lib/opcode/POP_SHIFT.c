/*
  This library is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 3 of the License, or
  (at your option) any later version.

  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this library; if not, see <http://www.gnu.org/licenses/>.
*/

/**
   POP_SHIFT: Take TOS as result, unwind the SHIFT node (teardown drops its
   stack block) and the PROMPT, jump past POP_PROMPT, push the result.

   Expects DYN_TOP to be a KOS_DYN_SHIFT node.  Uses a transfer plan to
   unwind (EXTENT_ACTION for the shift, then TRUNCATE_AND_INJECT) and
   inject the result past the prompt.  Heritage bytecode extents between
   SHIFT and PROMPT (e.g. WIND) are skipped when resolving the enclosing
   PROMPT extent for resume PC.
*/

KOS_OP_HANDLER(POP_SHIFT) {
  PyObject *result;
  uint32_t eid;
  uint32_t parent_id;
  KosExtentDesc *shift_extent;
  KosExtentDesc *prompt_extent;
  uint32_t prompt_pop_pc;
  KosCode *code = KosEvalState_CurrentCode(es);
  KosDynPrompt *prompt = NULL;
  KosTransferPlan *plan = NULL;

  KosDynNode *top = KosEvalState_CurrentNode(es);
  if (! top || ! KosDynNode_IS_SHIFT(top)) {
    PyErr_SetString(PyExc_RuntimeError, "POP_SHIFT: top is not a shift node");
    return KOS_OP_POST_ERR();
  }

  /* PC already advanced: we are post-INSN, so resume will run instruction at
   * prompt_pop_pc+1 (first instr after POP_PROMPT). */
  SAYF("[POP_SHIFT] pre-pop: frame=%p stack=%p len=%zu (pc already advanced to %u)",
       (void *) KosEvalState_FRAME(es),
       (void *) KosEvalState_StackHead(es),
       KosEvalState_StackLen(es),
       (unsigned) KosEvalState_PC(es));
  result = KosEvalState_StackPopRef(es);
  if (! result) {
    SAY("[eval] POP_SHIFT pop result failed (stack underrun)");
    return KOS_OP_POST_ERR();
  }

  /* acting_pc is POP_SHIFT; find SHIFT extent by its pop_pc. */
  {
    uint32_t pop_pc = KosEvalState_ACTING_PC(es);
    eid = kos_extent_find_by_pop_pc((PyObject *) code, pop_pc, KOS_EXT_SHIFT);
  }
  if (eid == KOS_EXTENT_NONE) {
    Py_DECREF(result);
    PyErr_SetString(PyExc_RuntimeError, "POP_SHIFT: no SHIFT extent at pc");
    return KOS_OP_POST_ERR();
  }

  shift_extent = kos_extent_desc((PyObject *) code, eid);
  if (! shift_extent) {
    Py_DECREF(result);
    return KOS_OP_POST_ERR();
  }

  parent_id = shift_extent->parent_extent_id;
  if (parent_id == KOS_EXTENT_NONE) {
    Py_DECREF(result);
    PyErr_SetString(PyExc_RuntimeError, "POP_SHIFT: SHIFT extent has no parent");
    return KOS_OP_POST_ERR();
  }

  /* SHIFT may sit under heritage extents (e.g. WIND) between it and PROMPT. */
  {
    uint32_t walk_id = parent_id;
    prompt_extent = NULL;
    while (walk_id != KOS_EXTENT_NONE) {
      KosExtentDesc *candidate = kos_extent_desc((PyObject *) code, walk_id);
      if (! candidate) {
        Py_DECREF(result);
        return KOS_OP_POST_ERR();
      }
      if (candidate->kind == KOS_EXT_PROMPT) {
        prompt_extent = candidate;
        break;
      }
      walk_id = candidate->parent_extent_id;
    }
  }
  if (! prompt_extent) {
    Py_DECREF(result);
    PyErr_SetString(PyExc_RuntimeError,
                    "POP_SHIFT: no enclosing PROMPT extent for SHIFT");
    return KOS_OP_POST_ERR();
  }

  prompt_pop_pc = prompt_extent->pop_pc;

  prompt = kos_find_nearest_prompt(es);
  if (! prompt) {
    Py_DECREF(result);
    PyErr_SetString(PyExc_RuntimeError, "POP_SHIFT: no prompt on chain");
    return KOS_OP_POST_ERR();
  }

  if (kos_dyn_build_pop_shift_plan(es, prompt, result, &plan) < 0) {
    Py_DECREF(result);
    return KOS_OP_POST_ERR();
  }
  Py_DECREF(result);

  KosTransferPlan_RESUME_PC(plan) = prompt_pop_pc + 1;
  KosTransferPlan_RESUME_FRAME(plan) = NULL;
  KosTransferPlan_RESUME_STACK(plan) = NULL;
  SAYF("[POP_SHIFT] plan ready: RESUME_PC=%u (first instr after POP_PROMPT)",
       (unsigned) (prompt_pop_pc + 1));

  KosEvalState_ArmTransferPlan(es, plan);
  KosTransferPlan_CONTINUATION_REF(plan) = NULL;

  return KOS_OP_PROLOGUE();
}
