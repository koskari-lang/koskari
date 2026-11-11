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
   POP_SHIFT: Take TOS as result, unwind the SHIFT node and the PROMPT,
   then deliver the result as the region's value.  The handler runs on
   the SHIFT node, or in the frame copy on it when the shift ran in a
   called function, and the SHIFT's parent is the prompt it aborted to.
   The prompt of an applied continuation delivers through its DELIM at
   the invoke site, a live reset resumes past its POP_PROMPT (the
   pop_pc the prompt recorded at push).
*/

KOS_OP_HANDLER(POP_SHIFT) {
  KOS_SYNC_PC(ctx, insn);
  PyObject *result;
  KosOp *prompt_pop;
  KosDynPrompt *prompt = NULL;
  KosDynDelim *applied;
  KosXferGoal *goal;

  KosDynNode *top = KosContext_DYN_TOP(ctx);
  if (top && KosDynNode_IS_FRAME(top))
    top = KosDynNode_PARENT(top);
  if (! top || ! KosDynNode_IS_SHIFT(top)) {
    PyErr_SetString(PyExc_RuntimeError, "POP_SHIFT: top is not a shift node");
    RESULT_RAISE();
  }

  /* PC already advanced: we are post-INSN, so resume will run instruction at
   * prompt_pop_pc+1 (first instr after POP_PROMPT). */
  SAYF("[POP_SHIFT] pre-pop: frame=%p stack=%p len=%zu (pc already advanced to %u)",
       (void *) KosContext_FRAME(ctx),
       (void *) KosContext_StackHead(ctx),
       KosContext_StackLen(ctx),
       (unsigned) kos_insn_pc(KosContext_PC(ctx)));
  result = KosContext_StackPopRef(ctx);
  if (! result) {
    SAY("[eval] POP_SHIFT pop result failed (stack underrun)");
    RESULT_RAISE();
  }

  /* The shift aborted to its prompt and its SHIFT node was attached to it,
     so the delimiter is the SHIFT's parent. */
  if (KosDynNode_PARENT(top) && KosDynNode_IS_PROMPT(KosDynNode_PARENT(top)))
    prompt = (KosDynPrompt *) KosDynNode_PARENT(top);
  if (! prompt) {
    Py_DECREF(result);
    PyErr_SetString(PyExc_RuntimeError,
                    "POP_SHIFT: the shift node is not on its prompt");
    RESULT_RAISE();
  }

  /* The prompt of an applied continuation: no POP_PROMPT instruction
     pairs with it as its own reset. The handler value IS the value of
     that k-invocation; deliver it at the invoke site through the DELIM
     the copy is attached to (unwinds this SHIFT, the prompt and the copy's
     frame on the way). */
  applied = kos_prompt_applied_delim((KosDynNode *) prompt);
  if (applied) {
    SAYF("[POP_SHIFT] applied continuation prompt %u: delivering to delim %p",
         (unsigned) kos_dynnode_id((KosDynNode *) prompt),
         (void *) applied);
    RESULT_RELAY(ctx, kos_eval_route_delim_return(ctx, applied, result));
  }

  /* Live reset: the prompt recorded its region's closing pc when
     PUSH_PROMPT resolved the PROMPT extent; resume past POP_PROMPT. */
  prompt_pop = KosDynPrompt_POP_PC(prompt);
  if (! prompt_pop) {
    Py_DECREF(result);
    PyErr_SetString(PyExc_RuntimeError,
                    "POP_SHIFT: prompt has no recorded pop pc");
    RESULT_RAISE();
  }

  /* Leave everything down to the prompt's parent -- the shift, the
     region remnants, the prompt itself -- and deliver the handler's
     value there, at the first instruction after POP_PROMPT. */
  goal = kos_xfer_goal_new(NULL, KosDynNode_PARENT((KosDynNode *) prompt),
                           result, prompt_pop + 1, NULL, 0);
  Py_DECREF(result);
  if (! goal)
    RESULT_RAISE();
  SAYF("[POP_SHIFT] goal active: resume_pc=%u (first instr after POP_PROMPT)",
       (unsigned) kos_insn_pc(prompt_pop + 1));
  kos_xfer_goal_push(ctx, goal);

  RESULT_REPROLOGUE(ctx);
}


// The end.
