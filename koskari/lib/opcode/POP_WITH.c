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
  (void) insn;

  KosDynNode *top = KosEvalState_CurrentNode(es);
  if (unlikely(! top)) {
    PyErr_SetString(PyExc_RuntimeError, "POP_WITH: dyn_top is NULL");
    RESULT_RAISE();
  }

  if (unlikely(! KosDynNode_IS_WITH(top))) {
    PyErr_Format(
      PyExc_RuntimeError,
      "POP_WITH: top is %s (expected WITH), node=%p",
      kos_dynnode_kind_name(KosDynNode_KIND(top)), (void *) top);
    RESULT_RAISE();
  }

  /* the unwind will pass the value to the upper stack */
  KosOpResult kr = kos_dyn_with_unwind(es, (KosDynWith *) top);

  /* Error path, bail out. */
  if (kr.kind == KOS_RES_RAISE)
    RESULT_RAISE();

  /* Async path, need to suspend, we'll resume at this opcode when we
     wake back up. */
  if (kr.kind == KOS_RES_AWAIT) {
    uint32_t same_pc = KosEvalState_ACTING_PC(es);
    KosEvalState_SET_PC(es, same_pc);
    return kr;
  }

  /* __exit__ is a KosCallable whose frame is installed and has yet to
     run. Leave the node on the chain and the pc alone (the current frame
     is the callee's): kos_dynnode_on_control_returned completes the exit,
     pops the node, and sets the pc past this opcode. */
  if (KosDynWith_EXIT_PENDING((KosDynWith *) top))
    return kr;

  /* all is well (NORMAL or REPROLOGUE/swallowed), pop the node and return */
  if (unlikely(kos_dyn_pop_with(es, (KosDynWith *) top) < 0))
    RESULT_RAISE();
  RESULT_NEXT(es);
}


// The end.
