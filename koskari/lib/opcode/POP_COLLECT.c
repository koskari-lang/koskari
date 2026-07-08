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
   POP_COLLECT: arg unused. Verify dyn_top is KOS_DYN_COLLECT, pop the node,
   and push the collected namespace onto the value stack.
*/

KOS_OP_HANDLER(POP_COLLECT) {
  (void) insn;

  KosDynNode *top = KosEvalState_CurrentNode(es);
  /* the collected namespace lands on the value stack: reveal it */
  PyObject *collector = KosNamespace_Reveal(KosDynNode_NS(top));

  if (unlikely(kos_dyn_pop_collect(es, (KosDynCollect *) top) < 0)) {
    Py_DECREF(collector);
    return EVAL_RAISE();
  }

  int res = KosEvalState_StackPUSH(es, collector);
  return kos_op_result_from_push_int(res);
}


// The end.
