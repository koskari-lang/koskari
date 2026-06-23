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
  KosEvent _op_ev = {0};
  (void) insn;

  KosDynNode *top = KosEvalState_CurrentNode(es);
  if (unlikely(! top)) {
    PyErr_SetString(PyExc_RuntimeError, "POP_WITH: dyn_top is NULL");
    return EVAL_RAISE();
  }

  if (unlikely(! KosDynNode_IS_WITH(top))) {
    PyErr_Format(
      PyExc_RuntimeError,
      "POP_WITH: top is %s (expected WITH), node=%p",
      kos_dynnode_kind_name(KosDynNode_KIND(top)), (void *) top);
    return EVAL_RAISE();
  }

  /* the unwind will pass the value to the upper stack */
  int unw = kos_dyn_with_unwind(es, (KosDynWith *) top, &_op_ev);

  /* Async path, need to suspend, we'll resume at this opcode when we
     wake back up. */
  if (unw == KOS_DYN_UNWIND_ASYNC_STARTED) {
    uint32_t same_pc = KosEvalState_ACTING_PC(es);
    KosEvalState_SET_PC(es, same_pc);
    PyObject *aw = KosEvent_PAYLOAD(&_op_ev);
    KosEvent_PAYLOAD(&_op_ev) = NULL;
    return EVAL_AWAIT(aw);
  }

  /* Error path, bail out. */
  if (unw != KOS_DYN_UNWIND_PROTOCOL_SKIPPED && unw < 0)
    return EVAL_RAISE();

  /* all is well, we've completed the unwind, pop the node and return */
  if (unlikely(kos_dyn_pop_with(es, (KosDynWith *) top) < 0))
    return EVAL_RAISE();
  return EVAL_NORMAL();
}


// The end.
