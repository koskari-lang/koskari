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
   POP_DYN_WITH: arg unused. Unlink the multi-shot dyn-with node: __exit__
   runs (auto-awaited when it returns an awaitable), the node leaves the
   chain, and the body value lands on the revealed block.
*/

KOS_OP_HANDLER(POP_DYN_WITH) {
  KosDynNode *top = KosEvalState_CurrentNode(es);
  if (! top || ! KosDynNode_IS_WITH(top)) {
    PyErr_SetString(PyExc_RuntimeError, "POP_DYN_WITH: top is not a with node");
    RESULT_RAISE();
  }
  KosDynFrame *const caller = KosEvalState_FRAME(es);
  KosOpResult kr = kos_dyn_unlink_top(es, KOS_XFER_EVENT_NONE, 0);

  if (kr.kind == KOS_RES_RAISE)
    RESULT_RAISE();
  if (kr.kind == KOS_RES_AWAIT) {
    uint32_t same_pc = KosEvalState_ACTING_PC(es);
    KosEvalState_SET_PC(es, same_pc);
    return kr;
  }
  /* __exit__'s frame is current: run it; its return settles and
     unlinks the node. A plan armed by the install drains first. */
  if (kr.kind == KOS_RES_REPROLOGUE) {
    if (KosEvalState_FRAME(es) != caller && ! KosEvalState_GOAL(es))
      RESULT_CONTINUE(es);
    return kr;
  }

  RESULT_NEXT(es);
}


// The end.
