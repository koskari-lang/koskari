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

KOS_OP_HANDLER_COLD(POP_SHIFT) {
  PyObject *result;
  uint32_t eid;
  uint32_t parent_id;
  uint32_t prompt_pop_pc;
  KosCode *code = KosEvalState_CurrentCode(es);
  KosDynPrompt *prompt = NULL;
  KosExtentDesc *lexical_prompt = NULL;
  KosTransferPlan *plan = NULL;

  KosDynNode *top = KosEvalState_CurrentNode(es);
  if (! top || ! KosDynNode_IS_SHIFT(top)) {
    PyErr_SetString(PyExc_RuntimeError, "POP_SHIFT: top is not a shift node");
    return EVAL_RAISE();
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
    return EVAL_RAISE();
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
          && KosDynNode_NODE_ID(node) == want) {
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
    return EVAL_RAISE();
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
      return EVAL_RAISE();
    }
    SAYF("[POP_SHIFT] invocation prompt %u: delivering to paired delim %p",
         (unsigned) KosDynNode_NODE_ID((KosDynNode *) prompt),
         (void *) pair);
    return kos_eval_route_delim_return(es, pair, result);
  }

  /* acting_pc is POP_SHIFT; find SHIFT extent by its pop_pc. */
  {
    uint32_t pop_pc = KosEvalState_ACTING_PC(es);
    eid = kos_extent_find_by_pop_pc((PyObject *) code, pop_pc, KOS_EXT_SHIFT);
  }
  if (eid == KOS_EXTENT_NONE) {
    Py_DECREF(result);
    PyErr_SetString(PyExc_RuntimeError, "POP_SHIFT: no SHIFT extent at pc");
    return EVAL_RAISE();
  }

  KosExtentDesc *const shift_extent = kos_extent_desc((PyObject *) code, eid);
  if (! shift_extent) {
    Py_DECREF(result);
    return EVAL_RAISE();
  }

  /* Preferred: the PROMPT extent lexically enclosing this SHIFT in our own
     code. Reliable whenever the shift sits inside its reset, and does not
     depend on any frame's acting pc, which slice execution overwrites. */
  parent_id = shift_extent->parent_extent_id;
  {
    uint32_t walk_id = parent_id;
    while (walk_id != KOS_EXTENT_NONE) {
      KosExtentDesc *candidate = kos_extent_desc((PyObject *) code, walk_id);
      if (! candidate) {
        Py_DECREF(result);
        return EVAL_RAISE();
      }
      if (candidate->kind == KOS_EXT_PROMPT) {
        lexical_prompt = candidate;
        break;
      }
      walk_id = candidate->parent_extent_id;
    }
  }

  if (lexical_prompt) {
    prompt_pop_pc = lexical_prompt->pop_pc;
  } else
  /* Resolve POP_PROMPT against the prompt's OWN frame, not ours. The
     shift/reset relationship is dynamic: a shift in a callee has its
     prompt in a different code object, where this code object's extent
     tree necessarily shows the SHIFT extent with no PROMPT parent. Taking
     pop_pc from the current code would then be a PC into the wrong
     bytecode. When the shift is lexically inside its reset the prompt's
     frame is this frame and the two resolutions agree. */
  {
    KosDynNode *node = (KosDynNode *) prompt;
    KosDynFrame *prompt_frame = NULL;
    PyObject *prompt_code;
    uint32_t prompt_eid;
    KosExtentDesc *prompt_extent;

    while (node) {
      if (KosDynNode_IS_FRAME(node)) {
        prompt_frame = (KosDynFrame *) node;
        break;
      }
      node = KosDynNode_PARENT(node);
    }
    if (! prompt_frame) {
      Py_DECREF(result);
      PyErr_SetString(PyExc_RuntimeError,
                      "POP_SHIFT: no frame below prompt");
      return EVAL_RAISE();
    }

    prompt_code = (PyObject *) KosDynFrame_CODE(prompt_frame);
    if (! prompt_code) {
      Py_DECREF(result);
      PyErr_SetString(PyExc_RuntimeError,
                      "POP_SHIFT: prompt frame has no code");
      return EVAL_RAISE();
    }

    prompt_eid = kos_extent_find(prompt_code,
                                 KosDynPrompt_PUSH_PC(prompt),
                                 KOS_EXT_PROMPT);
    if (prompt_eid == KOS_EXTENT_NONE) {
      Py_DECREF(result);
      PyErr_SetString(PyExc_RuntimeError,
                      "POP_SHIFT: no enclosing PROMPT extent for SHIFT");
      return EVAL_RAISE();
    }

    prompt_extent = kos_extent_desc(prompt_code, prompt_eid);
    if (! prompt_extent) {
      Py_DECREF(result);
      return EVAL_RAISE();
    }

    prompt_pop_pc = prompt_extent->pop_pc;
  }

  if (kos_dyn_build_pop_shift_plan(es, prompt, result, &plan) < 0) {
    Py_DECREF(result);
    return EVAL_RAISE();
  }
  Py_DECREF(result);

  KosTransferPlan_RESUME_PC(plan) = prompt_pop_pc + 1;
  KosTransferPlan_RESUME_FRAME(plan) = NULL;
  KosTransferPlan_RESUME_STACK(plan) = NULL;
  SAYF("[POP_SHIFT] plan ready: RESUME_PC=%u (first instr after POP_PROMPT)",
       (unsigned) (prompt_pop_pc + 1));

  KosEvalState_ArmTransferPlan(es, plan);
  KosTransferPlan_CONTINUATION_REF(plan) = NULL;

  return EVAL_REPROLOGUE();
}


// The end.
