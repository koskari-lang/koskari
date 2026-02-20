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
  SAYF("[eval] POP_PROMPT inv_seg=%p dyn_top=%p IS_PROMPT=%d\n",
       (void *) KosContext_INVOCATION_SEGMENT_TOP(ctx), (void *) KosContext_DYN_TOP(ctx),
       (KosContext_DYN_TOP(ctx) && KosDynNode_IS_PROMPT(KosContext_DYN_TOP(ctx))) ? 1 : 0);

  if (KosContext_INVOCATION_SEGMENT_TOP(ctx) != NULL) {
    PyObject *val = KosStackBlock_PopRef(&CURRENT_STACK(ctx));
    if (! val) {
      SAY("[eval] POP_PROMPT (invoke) pop underrun\n");
      return -1;
    }
    if (PyLong_Check(val))
      SAYF("[eval] POP_PROMPT return to invoker value=%ld\n",
           PyLong_AsLong(val));

    if (KosContext_DELIMITED_RETURN_FRAME(ctx) != NULL
        && KosContext_DELIMITED_RETURN_PC(ctx) != (uint32_t) -1) {
      int unw;
      if (KosContext_DELIMITED_RETURN_NODE(ctx) != NULL) {
        unw = kos_dyn_unwind_to_node(ctx,
                                     KosContext_DELIMITED_RETURN_NODE(ctx),
                                     out_ev);
      } else {
        unw = kos_dyn_unwind_to_frame(ctx,
                                      KosContext_DELIMITED_RETURN_FRAME(ctx),
                                      out_ev);
      }
      if (unw == KOS_DYN_UNWIND_ASYNC_STARTED) {
        KosContext_PENDING_RETURN_VALUE(ctx) = val;
        KosContext_PENDING_RETURN_TO_PARENT(ctx) = 0;
        return 0;
      }
      if (unw < 0) {
        Py_DECREF(val);
        return -1;
      }
      if (KosStackBlock_PushRef(
              &KosDynFrame_STACK(KosContext_DELIMITED_RETURN_FRAME(ctx)),
              val) < 0) {
        Py_DECREF(val);
        return -1;
      }
      Py_DECREF(val);
      KosDynFrame_PC(KosContext_DELIMITED_RETURN_FRAME(ctx)) =
          KosContext_DELIMITED_RETURN_PC(ctx);
      KosContext_FRAME_TOP(ctx) = KosContext_DELIMITED_RETURN_FRAME(ctx);
      KosContext_INVOCATION_SEGMENT_TOP(ctx) = NULL;
      KosContext_DELIMITED_RETURN_FRAME(ctx) = NULL;
      KosContext_DELIMITED_RETURN_PC(ctx) = (uint32_t) -1;
      KosContext_DELIMITED_RETURN_NODE(ctx) = NULL;
      (void) insn;
      return 0;
    }

    KosEvent_KIND(out_ev) = KOS_EV_RETURN;
    KosEvent_PAYLOAD(out_ev) = val;
    KosContext_INVOCATION_SEGMENT_TOP(ctx) = NULL;
    KosContext_FLAGS(ctx) &= ~KOS_CTX_RUNNING;
    (void) insn;
    return 1;
  }

  if (! KosContext_DYN_TOP(ctx) || ! KosDynNode_IS_PROMPT(KosContext_DYN_TOP(ctx))) {
    PyErr_SetString(PyExc_RuntimeError, "POP_PROMPT: top is not a prompt");
    return -1;
  }
  kos_dyn_pop_prompt(ctx, (KosDynPrompt *) KosContext_DYN_TOP(ctx));
  (void) insn;
  return 0;
}
