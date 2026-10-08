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
   POP_PROMPT: arg unused. If returning to an invoker (delimited push model),
   unwind to call site, push value, resume; else pop the top prompt from the
   dynamic chain.
*/

KOS_OP_HANDLER(POP_PROMPT) {
  KOS_SYNC_PC(es, insn);
  (void) insn;

  /* Return bookmark: PUSH-graft (POP_PROMPT) or in-place (INPLACE)
   * delimited invoke. Invocation prompts (created by a k-invocation)
   * have no POP_PROMPT instruction of their own -- their completion
   * routes through the paired delim below, so the walk passes them. A
   * non-invocation PROMPT ends the walk: that is a live reset whose own
   * POP_PROMPT this is, and any delim beyond it belongs to an outer
   * invocation, not to this region. */
  KosDynDelim *ret_delim = NULL;
  {
    KosDynNode *node;
    for (node = KosEvalState_CurrentNode(es); node;
         node = KosDynNode_PARENT(node)) {
      if (KosDynNode_IS_PROMPT(node)
          && ! (KosDynNode_FLAGS(node) & KOS_DYNF_INVOCATION))
        break;
      if (! KosDynNode_IS_DELIM(node))
        continue;
      uint8_t mode = KosDynDelim_MODE((KosDynDelim *) node);
      if (mode == KOS_DYN_DELIM_MODE_POP_PROMPT
          || mode == KOS_DYN_DELIM_MODE_INPLACE) {
        ret_delim = (KosDynDelim *) node;
        break;
      }
    }
  }

  SAYF("[eval] POP_PROMPT delim=%p dyn_top=%p IS_PROMPT=%d frame=%p stack_len=%zu\n",
       (void *) ret_delim, (void *) KosEvalState_CurrentNode(es),
       (KosEvalState_CurrentNode(es) && KosDynNode_IS_PROMPT(KosEvalState_CurrentNode(es))) ? 1 : 0,
       (void *) KosEvalState_FRAME(es),
       KosEvalState_StackLen(es));

  if (ret_delim) {
    PyObject *val = KosEvalState_StackPopRef(es);
    if (! val) {
      SAY("[eval] POP_PROMPT (invoke) pop underrun\n");
      RESULT_RAISE();
    }
    if (PyLong_Check(val))
      SAYF("[eval] POP_PROMPT return to invoker value=%ld\n",
           PyLong_AsLong(val));

    if (KosDynDelim_RETURN_FRAME(ret_delim) != NULL
        && KosDynDelim_RETURN_PC(ret_delim) != (uint32_t) -1) {
      KosOpResult routed = kos_eval_route_delim_return(es, ret_delim, val);
#if KOS_EVAL_SAFETYNET
      /* NORMAL threads the next instruction of the landed frame; a
         code-less invoke site must be delivered through the prologue
         (REPROLOGUE) instead. */
      if (unlikely(routed.kind == KOS_RES_NORMAL
                   && ! KosEvalState_CODE(es))) {
        PyErr_SetString(PyExc_RuntimeError,
                        "POP_PROMPT: delimited return resumed dispatch "
                        "in a code-less frame");
        RESULT_RAISE();
      }
#endif
      RESULT_RELAY(es, routed);
    }

    if (KosEvalState_CurrentNode(es) == (KosDynNode *) ret_delim) {
      if (unlikely(kos_dyn_pop_delim(es, ret_delim) < 0)) {
        Py_DECREF(val);
        RESULT_RAISE();
      }
    }
    KosEvalState_MarkSuspendedNotRunning(es);
    RESULT_RETURN(es, val);
  }

  KosDynNode *top = KosEvalState_CurrentNode(es);
  if (! top || ! KosDynNode_IS_PROMPT(top)) {
    PyErr_SetString(PyExc_RuntimeError, "POP_PROMPT: top is not a prompt");
    RESULT_RAISE();
  }
  SAYF("[eval] POP_PROMPT (normal): popping prompt, frame=%p stack_len=%zu",
       (void *) KosEvalState_FRAME(es),
       KosEvalState_StackLen(es));

  PyObject *result = KosEvalState_StackPopRef(es);
  if (! result) {
    SAY("[eval] POP_PROMPT (normal): stack underrun");
    RESULT_RAISE();
  }

  if (kos_dyn_pop_prompt(es, (KosDynPrompt *) top) < 0) {
    Py_XDECREF(result);
    RESULT_RAISE();
  }

  SAYF("[eval] POP_PROMPT (normal): after pop, frame=%p stack_len=%zu",
       (void *) KosEvalState_FRAME(es),
       KosEvalState_StackLen(es));

  RESULT_STEAL(es, result);
}


// The end.
