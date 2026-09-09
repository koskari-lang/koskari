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
   POP_WITH: arg unused. Park TOS (the body value) on the with node and
   unlink it: __exit__ runs (auto-awaited when it returns an awaitable),
   the node leaves the chain, and the unlink delivers the body value to
   the revealed block. POP_DYN_WITH is this handler under another name.
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

  KosDynFrame *const caller = KosEvalState_FRAME(es);
  /* the body value, when the body left one, waits on the node for the
     unlink to deliver; a with in statement position leaves none */
  {
    KosSegment *seg = KosEvalState_BLOCK(es) ? *KosEvalState_BLOCK(es) : NULL;

    if (seg && KosSegment_LEN(seg)) {
      PyObject *body_result = KosEvalState_StackPopRef(es);

      if (! body_result || kos_dyn_ob_park_value(top, body_result) < 0)
        RESULT_RAISE();
    }
  }

  KosOpResult kr = kos_dyn_unlink_top(es, KOS_XFER_EVENT_NONE, 0);

  if (kr.kind == KOS_RES_RAISE)
    RESULT_RAISE();

  /* __exit__ suspended on an awaitable; the revive settles and unlinks
     the node and continues past this opcode. */
  if (kr.kind == KOS_RES_AWAIT) {
    uint32_t same_pc = KosEvalState_ACTING_PC(es);
    KosEvalState_SET_PC(es, same_pc);
    return kr;
  }

  /* __exit__ is a KosCallable whose frame is installed and has yet to
     run. The node stays on the chain and the pc alone (the current frame
     is the callee's, so threading on runs it): the callee's return
     settles and unlinks the node and sets the pc past this opcode. A
     plan armed by the install is the prologue's to drain. */
  if (kr.kind == KOS_RES_REPROLOGUE) {
    if (KosEvalState_FRAME(es) != caller && ! KosEvalState_GOAL(es))
      RESULT_CONTINUE(es);
    return kr;
  }

  RESULT_NEXT(es);
}


// The end.
