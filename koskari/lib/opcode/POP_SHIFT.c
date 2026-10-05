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
   POP_SHIFT: Take TOS as result, unwind the SHIFT node (a transparent
   bookmark with no segment of its own) and the PROMPT, then deliver
   the result as the region's value.  The delimiter is resolved by the instance the shift captured
   against; an invocation prompt delivers through its paired DELIM at the
   invoke site, a live reset resumes past its POP_PROMPT (the pop_pc the
   prompt recorded at push).
*/

KOS_OP_HANDLER(POP_SHIFT) {
  PyObject *result;
  uint32_t prompt_pop_pc;
  KosDynPrompt *prompt = NULL;
  KosXferGoal *goal;

  KosDynNode *top = KosEvalState_CurrentNode(es);
  if (! top || ! KosDynNode_IS_SHIFT(top)) {
    PyErr_SetString(PyExc_RuntimeError, "POP_SHIFT: top is not a shift node");
    RESULT_RAISE();
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
    RESULT_RAISE();
  }

  /* Resolve the delimiter by the instance the shift captured against
     (recorded on the SHIFT node at PUSH_SHIFT). Two prompts with the
     same tag can coexist on the chain -- a grafted or in-place
     invocation instance alongside the live original -- so position
     (nearest) is never the right answer; identity is. */
  {
    uint32_t want = KosDynShift_PROMPT_NODE_ID(top);
    KosDynNode *node = KosDynNode_PARENT(top);

    while (node) {
      if (KosDynNode_IS_PROMPT(node)
          && kos_dynnode_id(node) == want) {
        prompt = (KosDynPrompt *) node;
        break;
      }
      node = KosDynNode_PARENT(node);
    }
  }
  if (! prompt) {
    Py_DECREF(result);
    PyErr_SetString(PyExc_RuntimeError,
                    "POP_SHIFT: captured prompt instance not on chain");
    RESULT_RAISE();
  }

  /* Invocation prompt: created by a k-invocation, so no POP_PROMPT
     instruction anywhere pairs with it. The handler value IS the value
     of that k-invocation; deliver it at the invoke site through the
     paired DELIM bookmark below the prompt (unwinds this SHIFT, the
     region remnants, and the invocation prompt on the way). */
  if (KosDynNode_FLAGS((KosDynNode *) prompt) & KOS_DYNF_INVOCATION) {
    KosDynDelim *pair = NULL;
    KosDynNode *node = KosDynNode_PARENT((KosDynNode *) prompt);

    while (node && ! KosDynNode_IS_PROMPT(node)) {
      if (KosDynNode_IS_DELIM(node)) {
        uint8_t mode = KosDynDelim_MODE((KosDynDelim *) node);
        if (mode == KOS_DYN_DELIM_MODE_POP_PROMPT
            || mode == KOS_DYN_DELIM_MODE_INPLACE) {
          pair = (KosDynDelim *) node;
          break;
        }
      }
      node = KosDynNode_PARENT(node);
    }
    if (! pair) {
      Py_DECREF(result);
      PyErr_SetString(PyExc_RuntimeError,
                      "POP_SHIFT: invocation prompt has no paired delim");
      RESULT_RAISE();
    }
    SAYF("[POP_SHIFT] invocation prompt %u: delivering to paired delim %p",
         (unsigned) kos_dynnode_id((KosDynNode *) prompt),
         (void *) pair);
    RESULT_RELAY(es, kos_eval_route_delim_return(es, pair, result));
  }

  /* Live reset: the prompt recorded its region's closing pc when
     PUSH_PROMPT resolved the PROMPT extent; resume past POP_PROMPT. */
  prompt_pop_pc = KosDynPrompt_POP_PC(prompt);
  if (prompt_pop_pc == (uint32_t) -1) {
    Py_DECREF(result);
    PyErr_SetString(PyExc_RuntimeError,
                    "POP_SHIFT: prompt has no recorded pop pc");
    RESULT_RAISE();
  }

  /* Leave everything down to the prompt's parent -- the shift, the
     region remnants, the prompt itself -- and deliver the handler's
     value there, at the first instruction after POP_PROMPT. */
  goal = kos_xfer_goal_new(NULL, KosDynNode_PARENT((KosDynNode *) prompt),
                           result, prompt_pop_pc + 1, NULL, 0);
  Py_DECREF(result);
  if (! goal)
    RESULT_RAISE();
  SAYF("[POP_SHIFT] goal armed: resume_pc=%u (first instr after POP_PROMPT)",
       (unsigned) (prompt_pop_pc + 1));
  kos_xfer_goal_arm(es, goal);

  RESULT_REPROLOGUE(es);
}


// The end.
