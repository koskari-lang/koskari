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
  KosDynNode *top = KosEvalState_CurrentNode(es);
  if (! top || ! KosDynNode_IS_WITH(top)) {
    PyErr_SetString(PyExc_RuntimeError, "POP_DYN_WITH: top is not a with node");
    return EVAL_RAISE();
  }
  KosDynWith *with_node = (KosDynWith *) top;

  KosOpResult kr = kos_dyn_with_unwind(es, with_node);
  if (kr.kind == KOS_RES_AWAIT) {
    uint32_t same_pc = KosEvalState_ACTING_PC(es);
    KosEvalState_SET_PC(es, same_pc);
    return kr;
  }
  /* REPROLOGUE = exception swallowed; resume_pc already set, node stays on chain */
  if (kr.kind == KOS_RES_REPROLOGUE)
    return EVAL_NORMAL();
  if (kr.kind == KOS_RES_RAISE)
    return EVAL_RAISE();

  if (KosDynNode_STACK((KosDynNode *) with_node)
      && KosStackBlock_LEN(KosDynNode_STACK((KosDynNode *) with_node)) > 0) {
    PyObject *body_val = KosEvalState_StackPopRef(es);
    if (! body_val)
      return EVAL_RAISE();
    if (unlikely(kos_dyn_pop_with(es, with_node) < 0)) {
      Py_DECREF(body_val);
      return EVAL_RAISE();
    }
    if (KosEvalState_StackPushRef(es, body_val) < 0) {
      Py_DECREF(body_val);
      return EVAL_RAISE();
    }
    Py_DECREF(body_val);
  } else {
    if (unlikely(kos_dyn_pop_with(es, with_node) < 0))
      return EVAL_RAISE();
  }

  (void) insn;
  return EVAL_NORMAL();
}


// The end.
