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
  KosContext *ctx = KosEvalState_CONTEXT(es);
  // CONTEXT: dirty, touches context directly
  KosDynDelim *ret_delim = kos_find_nearest_delim(es, KOS_DYN_DELIM_MODE_POP_PROMPT);

  SAYF("[eval] POP_PROMPT delim=%p dyn_top=%p IS_PROMPT=%d frame=%p stack_len=%zu\n",
       (void *) ret_delim, (void *) KosEvalState_CurrentNode(es),
       (KosEvalState_CurrentNode(es) && KosDynNode_IS_PROMPT(KosEvalState_CurrentNode(es))) ? 1 : 0,
       (void *) KosEvalState_FRAME(es),
       KosEvalState_StackLen(es));

  if (ret_delim) {
    PyObject *val = KosEvalState_StackPopRef(es);
    if (! val) {
      SAY("[eval] POP_PROMPT (invoke) pop underrun\n");
      return KOS_OP_POST_ERR();
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
      KosTransitionResult tres = {0};
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
      if (kos_dyn_transition(es, &treq, &tres, out_ev) < 0) {
        Py_DECREF(val);
        return KOS_OP_POST_ERR();
      }
      int unw = tres.raw;
      if (unw == KOS_DYN_UNWIND_ASYNC_STARTED) {
        KosEvalState_SetPendingReturn(es, val, 0);
        return KOS_OP_RUN();
      }
      if (unw < 0) {
        Py_DECREF(val);
        return KOS_OP_POST_ERR();
      }
      SAYF("[eval] POP_PROMPT after unwind: dyn_top=%p kind=%s",
           (void *) KosEvalState_CurrentNode(es),
           KosEvalState_CurrentNode(es) ? kos_dynnode_kind_name(KosDynNode_KIND(KosEvalState_CurrentNode(es))) : "NULL");
      if (kos_dyn_finish_delimited_pop_return(es, ret_delim, val) < 0)
        return KOS_OP_POST_ERR();
      (void) insn;
      return KOS_OP_RUN();
    }

    KosEvent_KIND(out_ev) = KOS_EV_RETURN;
    KosEvent_PAYLOAD(out_ev) = val;
    if (KosEvalState_CurrentNode(es) == (KosDynNode *) ret_delim)
      kos_dyn_pop_delim(es, ret_delim);
    KosContext_FLAGS(ctx) &= ~KOS_CTX_RUNNING;
    (void) insn;
    return KOS_OP_POST_SUSPEND();
  }

  KosDynNode *top = KosEvalState_CurrentNode(es);
  if (! top || ! KosDynNode_IS_PROMPT(top)) {
    PyErr_SetString(PyExc_RuntimeError, "POP_PROMPT: top is not a prompt");
    return KOS_OP_POST_ERR();
  }
  SAYF("[eval] POP_PROMPT (normal): popping prompt, frame=%p stack_len=%zu",
       (void *) KosEvalState_FRAME(es),
       KosEvalState_StackLen(es));

  PyObject *result = KosEvalState_StackPopRef(es);
  if (! result) {
    SAY("[eval] POP_PROMPT (normal): stack underrun");
    return KOS_OP_POST_ERR();
  }

  if (kos_dyn_pop_prompt(es, (KosDynPrompt *) top) < 0) {
    Py_XDECREF(result);
    return KOS_OP_POST_ERR();
  }

  SAYF("[eval] POP_PROMPT (normal): after pop, frame=%p stack_len=%zu",
       (void *) KosEvalState_FRAME(es),
       KosEvalState_StackLen(es));

  if (result) {
    if (KosEvalState_StackPUSH(es, result) < 0) {
      return KOS_OP_POST_ERR();
    }
  }

  (void) insn;
  return KOS_OP_RUN();
}
