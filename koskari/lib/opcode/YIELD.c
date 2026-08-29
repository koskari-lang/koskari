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
  YIELD: arg = 0 (None), 1 (single value), or N (tuple of N values).
  Pop the value(s) from the stack and return EVAL_YIELD(val); apply_kos_op_result
  routes to eval_event_yield for walk/freeze and node delivery.
  Resumption (send value) is handled by py_resume_inject at re-entry.
*/
KOS_OP_HANDLER(YIELD) {
  unsigned opa = KosOp_ARG(insn);
  PyObject *val = NULL;

  if (opa == 0) {
    val = Py_None;
    Py_INCREF(val);

  } else if (opa == 1) {
    val = KosEvalState_StackPopRef(es);
    if (unlikely(! val)) {
      SAY("[eval] YIELD pop underrun\n");
      return EVAL_RAISE();
    }

  } else {
    val = KosEvalState_StackPopTail(es, (size_t) opa);
    if (unlikely(! val)) {
      SAY("[eval] YIELD PopTail underrun\n");
      return EVAL_RAISE();
    }
  }

  /* Frame has parent (not GEN): return into parent in same loop
     (CALL return or async gen step).  Parent is GEN => top-level yield. */

  return EVAL_YIELD(val);
}


// The end.
