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
   BREAK: arg = result count (0 = None, 1 = single value, N = tuple of N).
   Exit innermost loop/foreach; jump to its break target. Pops the loop
   region and pushes the result value to the parent stack as the loop
   expression value. Raises RuntimeError if no LOOP/FOREACH region on chain.
*/

KOS_OP_HANDLER(BREAK) {
  KosContext *ctx = KosEvalState_CONTEXT(es);
  unsigned opa = KosOp_ARG(insn);
  PyObject *result = NULL;

  if (opa == 0) {
    result = Py_None;
    Py_INCREF(result);
  } else if (opa == 1) {
    result = KosEvalState_StackPopRef(es);
    if (! result) {
      PyErr_SetString(PyExc_RuntimeError, "BREAK pop underrun");
      return KOS_OP_POST_ERR();
    }
  } else {
    result = KosEvalState_StackPopTail(es, (size_t) opa);
    if (! result) {
      PyErr_SetString(PyExc_RuntimeError, "BREAK PopTail underrun");
      return KOS_OP_POST_ERR();
    }
  }

  {
    KosTransitionReq treq = {
      .kind = KOS_TRANS_TRANSFER_TO_LOOP,
      .xfer_event = KOS_XFER_EVENT_BREAK,
      .payload = result,
    };
    KosTransitionResult tres = {0};
    if (kos_dyn_transition(ctx, &treq, &tres, out_ev) < 0) {
      Py_DECREF(result);
      return KOS_OP_POST_ERR();
    }
    return kos_op_outcome_from_int_step(tres.raw);
  }
}
