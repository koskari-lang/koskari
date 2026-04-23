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
   POP_DYN_WITH: arg unused. Pop multi-shot dyn-with context. Call exit(typ, val, tb);
   auto-await if exit returns awaitable. If exception and exit returns true,
   swallow; else re-raise. Pop body value from block, drop block, pop
   KOS_DYN_WITH node (multi-shot), push body value.
*/

KOS_OP_HANDLER(POP_DYN_WITH) {
  KosContext *ctx = KosEvalState_CONTEXT(es);

  KosDynNode *top = KosContext_DYN_TOP(ctx);
  if (! top || ! KosDynNode_IS_WITH(top)) {
    PyErr_SetString(PyExc_RuntimeError, "POP_DYN_WITH: top is not a with node");
    return KOS_OP_POST_ERR();
  }
  KosDynWith *with_node = (KosDynWith *) top;

  int unw = kos_dyn_with_unwind(ctx, with_node, out_ev);
  if (unw == KOS_DYN_UNWIND_ASYNC_STARTED) {
    uint32_t same_pc = CURRENT_ACTING_PC(ctx);
    CURRENT_PC(ctx) = same_pc;
    KosDynFrame_ACTING_PC(CURRENT_FRAME(ctx)) = same_pc;
    return KOS_OP_POST_SUSPEND();
  }
  if (unw == KOS_DYN_UNWIND_SWALLOWED)
    return KOS_OP_RUN();
  if (unw != KOS_DYN_UNWIND_PROTOCOL_SKIPPED && unw < 0)
    return KOS_OP_POST_ERR();

  PyObject *body_val = KosEvalState_StackPopRef(es);
  if (! body_val)
    return KOS_OP_POST_ERR();
  kos_dyn_pop_with(ctx, with_node);
  if (KosEvalState_StackPushRef(es, body_val) < 0) {
    Py_DECREF(body_val);
    return KOS_OP_POST_ERR();
  }
  Py_DECREF(body_val);

  (void) out_ev;
  (void) insn;
  return KOS_OP_RUN();
}


// The end.
