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
   CONTINUE: arg = result count (0 = None, 1 = single value, N = tuple of N).
   Jump to innermost loop/foreach continue target. Updates the region result
   and clears the loop stack block before jumping. Raises RuntimeError if
   no LOOP/FOREACH region on the chain.
*/

KOS_OP_HANDLER(CONTINUE) {
  unsigned opa = KosOp_ARG(insn);
  PyObject *result = NULL;

  if (opa == 0) {
    result = Py_None;
    Py_INCREF(result);

  } else if (opa == 1) {
    result = KosEvalState_StackPopRef(es);
    if (! result) {
      PyErr_SetString(PyExc_RuntimeError, "CONTINUE pop underrun");
      return EVAL_RAISE();
    }

  } else {
    result = KosEvalState_StackPopTail(es, (size_t) opa);
    if (! result) {
      PyErr_SetString(PyExc_RuntimeError, "CONTINUE PopTail underrun");
      return EVAL_RAISE();
    }
  }

  {
    KosTransitionReq treq = {
      .kind = KOS_TRANS_TRANSFER_TO_LOOP,
      .xfer_event = KOS_XFER_EVENT_CONTINUE,
      .payload = result,
    };
    KosOpResult kr = KosEvalState_TransitionOp(es, &treq);
    if (kr.kind == KOS_RES_RAISE)
      Py_DECREF(result);
    return kr;
  }
}


// The end.
