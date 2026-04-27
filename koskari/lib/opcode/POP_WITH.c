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
   POP_WITH: arg unused. Pop single-shot with context. Call exit(typ, val, tb);
   auto-await if exit returns awaitable. If exception and exit returns true,
   swallow; else re-raise. Pop body value from block, drop block, pop
   KOS_DYN_WITH node (single-shot), push body value.
*/

KOS_OP_HANDLER(POP_WITH) {

  SAY("[eval] POP_WITH entry");

  KosDynNode *top = KosEvalState_CurrentNode(es);
  if (! top || ! KosDynNode_IS_WITH(top)) {
    SAY("[eval] POP_WITH top is not a with node");
    PyErr_SetString(PyExc_RuntimeError, "POP_WITH: top is not a with node");
    return KOS_OP_POST_ERR();
  }
  KosDynWith *with_node = (KosDynWith *) top;

  int unw = kos_dyn_with_unwind(es, with_node, out_ev);

  /* Async path, need to suspend, we'll resume at this opcode when we
     wake back up. */
  if (unw == KOS_DYN_UNWIND_ASYNC_STARTED) {
    uint32_t same_pc = KosEvalState_ACTING_PC(es);
    KosEvalState_SET_PC(es, same_pc);
    return KOS_OP_POST_SUSPEND();
  }

  /* Error path, bail out. */
  if (unw != KOS_DYN_UNWIND_PROTOCOL_SKIPPED && unw < 0)
    return KOS_OP_POST_ERR();

  /* all is well, we've completed the unwind, pop the node and return */
  kos_dyn_pop_with(es, with_node);

  SAY("[eval] POP_WITH sync path done -> return 0");
  (void) out_ev;
  (void) insn;
  return KOS_OP_RUN();
}


// The end.
