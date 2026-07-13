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

KOS_OP_HANDLER_COLD(POP_PROMPT) {
  (void) insn;

  /* Nearest return bookmark: PUSH-graft (POP_PROMPT) or in-place
   * (INPLACE) delimited invoke. */
  KosDynDelim *ret_delim = NULL;
  {
    KosDynNode *node;
    for (node = KosEvalState_CurrentNode(es); node;
         node = KosDynNode_PARENT(node)) {
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
      return EVAL_RAISE();
    }
    if (PyLong_Check(val))
      SAYF("[eval] POP_PROMPT return to invoker value=%ld\n",
           PyLong_AsLong(val));

    if (KosDynDelim_RETURN_FRAME(ret_delim) != NULL
        && KosDynDelim_RETURN_PC(ret_delim) != (uint32_t) -1) {
      SAYF("[eval] POP_PROMPT unwinding to return frame=%p node=%p",
           (void *) KosDynDelim_RETURN_FRAME(ret_delim),
           (void *) KosDynDelim_RETURN_NODE(ret_delim));
      KosTransitionReq treq;
      if (KosDynDelim_RETURN_NODE(ret_delim) != NULL) {
        treq = (KosTransitionReq) {
          .kind = KOS_TRANS_UNWIND_TO_NODE,
          .xfer_event = KOS_XFER_EVENT_SHIFT,
          .target_node = KosDynDelim_RETURN_NODE(ret_delim),
        };
      } else {
        treq = (KosTransitionReq) {
          .kind = KOS_TRANS_UNWIND_TO_FRAME,
          .xfer_event = KOS_XFER_EVENT_SHIFT,
          .target_frame = KosDynDelim_RETURN_FRAME(ret_delim),
        };
      }
      {
        KosOpResult _kr = KosEvalState_TransitionOp(es, &treq);
        if (_kr.kind == KOS_RES_RAISE) {
          Py_DECREF(val);
          return EVAL_RAISE();
        }
        if (_kr.kind != KOS_RES_NORMAL) {
          /* REPROLOGUE or AWAIT: async unwind started; stage pending return */
          Py_XDECREF(_kr.val);
          KosEvalState_SetPendingReturn(es, val, 0);
          return EVAL_NORMAL();
        }
      }
      SAYF("[eval] POP_PROMPT after unwind: dyn_top=%p kind=%s",
           (void *) KosEvalState_CurrentNode(es),
           KosEvalState_CurrentNode(es) ? kos_dynnode_kind_name(KosDynNode_KIND(KosEvalState_CurrentNode(es))) : "NULL");
      if (KosDynDelim_MODE(ret_delim) == KOS_DYN_DELIM_MODE_INPLACE) {
        if (kos_dyn_finish_inplace_pop_return(es, ret_delim, val) < 0)
          return EVAL_RAISE();
      } else if (kos_dyn_finish_delimited_pop_return(es, ret_delim, val) < 0) {
        return EVAL_RAISE();
      }
      return EVAL_NORMAL();
    }

    if (KosEvalState_CurrentNode(es) == (KosDynNode *) ret_delim) {
      if (unlikely(kos_dyn_pop_delim(es, ret_delim) < 0)) {
        Py_DECREF(val);
        return EVAL_RAISE();
      }
    }
    KosEvalState_MarkSuspendedNotRunning(es);
    return EVAL_RETURN(val);
  }

  KosDynNode *top = KosEvalState_CurrentNode(es);
  if (! top || ! KosDynNode_IS_PROMPT(top)) {
    PyErr_SetString(PyExc_RuntimeError, "POP_PROMPT: top is not a prompt");
    return EVAL_RAISE();
  }
  SAYF("[eval] POP_PROMPT (normal): popping prompt, frame=%p stack_len=%zu",
       (void *) KosEvalState_FRAME(es),
       KosEvalState_StackLen(es));

  PyObject *result = KosEvalState_StackPopRef(es);
  if (! result) {
    SAY("[eval] POP_PROMPT (normal): stack underrun");
    return EVAL_RAISE();
  }

  if (kos_dyn_pop_prompt(es, (KosDynPrompt *) top) < 0) {
    Py_XDECREF(result);
    return EVAL_RAISE();
  }

  SAYF("[eval] POP_PROMPT (normal): after pop, frame=%p stack_len=%zu",
       (void *) KosEvalState_FRAME(es),
       KosEvalState_StackLen(es));

  if (KosEvalState_StackPUSH(es, result) < 0) {
    return EVAL_RAISE();
  }
  return EVAL_NORMAL();
}


// The end.
