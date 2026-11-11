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
   POP_PROMPT: arg unused. The region is done and its prompt is at
   dyn_top. The prompt of an applied continuation delivers the region's
   value to the invoke site through the DELIM the copy is attached to; a
   reset running its own region pops its prompt from the dynamic chain.
*/

KOS_OP_HANDLER(POP_PROMPT) {
  KOS_SYNC_PC(ctx, insn);
  (void) insn;

  /* The prompt of an applied continuation has no reset of its own to
     complete: the copy's value is the value of the application, and
     goes to the DELIM. */
  KosDynDelim *ret_delim = NULL;
  {
    KosDynNode *node = KosContext_DYN_TOP(ctx);

    if (node && KosDynNode_IS_PROMPT(node))
      ret_delim = kos_prompt_applied_delim(node);
  }

  SAYF("[eval] POP_PROMPT delim=%p dyn_top=%p IS_PROMPT=%d frame=%p stack_len=%zu\n",
       (void *) ret_delim, (void *) KosContext_DYN_TOP(ctx),
       (KosContext_DYN_TOP(ctx) && KosDynNode_IS_PROMPT(KosContext_DYN_TOP(ctx))) ? 1 : 0,
       (void *) KosContext_FRAME(ctx),
       KosContext_StackLen(ctx));

  if (ret_delim) {
    PyObject *val = KosContext_StackPopRef(ctx);
    if (! val) {
      SAY("[eval] POP_PROMPT (invoke) pop underrun\n");
      RESULT_RAISE();
    }
    if (PyLong_Check(val))
      SAYF("[eval] POP_PROMPT return to invoker value=%ld\n",
           PyLong_AsLong(val));

    if (kos_dyndelim_return_frame(ret_delim) != NULL) {
      KosOpResult routed = kos_eval_route_delim_return(ctx, ret_delim, val);
#if KOS_EVAL_SAFETYNET
      /* NORMAL threads the next instruction of the return frame; a
         code-less invoke site must be delivered through the prologue
         (REPROLOGUE) instead. */
      if (unlikely(routed.kind == KOS_RES_NORMAL
                   && ! KosContext_FrameCode(ctx))) {
        PyErr_SetString(PyExc_RuntimeError,
                        "POP_PROMPT: delimited return resumed dispatch "
                        "in a code-less frame");
        RESULT_RAISE();
      }
#endif
      RESULT_RELAY(ctx, routed);
    }

    if (KosContext_DYN_TOP(ctx) == (KosDynNode *) ret_delim) {
      if (unlikely(kos_dyn_pop_delim(ctx, ret_delim) < 0)) {
        Py_DECREF(val);
        RESULT_RAISE();
      }
    }
    KosContext_MarkSuspendedNotRunning(ctx);
    RESULT_RETURN(ctx, val);
  }

  KosDynNode *top = KosContext_DYN_TOP(ctx);
  if (! top || ! KosDynNode_IS_PROMPT(top)) {
    PyErr_SetString(PyExc_RuntimeError, "POP_PROMPT: top is not a prompt");
    RESULT_RAISE();
  }
  SAYF("[eval] POP_PROMPT (normal): popping prompt, frame=%p stack_len=%zu",
       (void *) KosContext_FRAME(ctx),
       KosContext_StackLen(ctx));

  PyObject *result = KosContext_StackPopRef(ctx);
  if (! result) {
    SAY("[eval] POP_PROMPT (normal): stack underrun");
    RESULT_RAISE();
  }

  if (kos_dyn_pop_prompt(ctx, (KosDynPrompt *) top) < 0) {
    Py_XDECREF(result);
    RESULT_RAISE();
  }

  SAYF("[eval] POP_PROMPT (normal): after pop, frame=%p stack_len=%zu",
       (void *) KosContext_FRAME(ctx),
       KosContext_StackLen(ctx));

  RESULT_STEAL(ctx, result);
}


// The end.
