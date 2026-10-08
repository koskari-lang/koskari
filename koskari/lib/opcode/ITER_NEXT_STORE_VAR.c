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
  ITER_NEXT_STORE_VAR: arg = extra index. Schema VAR (name), LABEL (else
  pc). The for-each step with its element store: one iteration request
  against the FOREACH node's request on record, the element stored
  under the name in the frame namespace instead of pushed. Branch to
  the else pc on exhaustion. Must be inside a PUSH_FOREACH block.
  Auto-await: a step that defers sets the store resume on the frame,
  and the element is stored when it arrives. The pre-lowering twin of
  ITER_NEXT_STORE_FAST, which every site with a fast slot takes
  instead.
*/
KOS_OP_HANDLER(ITER_NEXT_STORE_VAR) {
  KOS_SYNC_PC(es, insn);
  KosDynNode *region = kos_dyn_find_loop(es);

  if (unlikely(! region || ! KosDynNode_IS_FOREACH(region))) {
    PyErr_SetString(PyExc_RuntimeError,
                    "ITER_NEXT_STORE_VAR must be used inside a PUSH_FOREACH"
                    " block");
    RESULT_RAISE();
  }

  kos_foreach_record_else(region, insn);

  {
    KosDynFrame *frame = KosEvalState_FRAME(es);
    PyObject *out = NULL;

    switch (kos_foreach_request_next(es, region, &out)) {
      case KOS_IREQ_DELIVERED:
        if (unlikely(kos_foreach_store_element(es, insn, out) < 0))
          RESULT_RAISE();
        RESULT_NEXT(es);

      case KOS_IREQ_EXHAUSTED:
        Py_XDECREF(out);
        KosDynForeach_EXHAUSTED((KosDynForeach *) region) = 1;
        RESULT_JUMP_AT(es, KosExtra_TARGET(KosOp_ROW(insn), 1));

      case KOS_IREQ_CONTINUES:
        kos_foreach_defer_step(frame, insn);
        RESULT_RELAY(es, kos_iter_installed(es));

      case KOS_IREQ_EXIT:
        kos_foreach_defer_step(frame, insn);
        RESULT_AWAIT(es, out);

      case KOS_IREQ_ERROR:
      default:
        RESULT_RAISE();
    }
  }
}


// The end.
