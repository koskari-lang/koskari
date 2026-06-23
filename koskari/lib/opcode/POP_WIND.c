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
   POP_WIND: arg unused. Pop the top wind node from the dynamic chain.
   Stores TOS (body result) in the wind node before uninstall. Invokes the
   after thunk if entered and non-null; may return KOS_DYN_UNWIND_ASYNC_STARTED
   when the thunk pushes a frame.
*/

KOS_OP_HANDLER(POP_WIND) {
  KosDynNode *top = KosEvalState_CurrentNode(es);
  if (! top || ! KosDynNode_IS_WIND(top)) {
    PyErr_SetString(PyExc_RuntimeError, "POP_WIND: top is not a wind node");
    return EVAL_RAISE();
  }

  KosDynWind *wind = (KosDynWind *) top;
  PyObject *body_result = KosEvalState_StackPopRef(es);
  if (! body_result)
    return EVAL_RAISE();
  KosDynWind_BODY_RESULT(wind) = body_result;

  KosEvent _op_ev = {0};
  int r = kos_dyn_pop_top(es, KOS_XFER_EVENT_RETURN, &_op_ev);
  if (r == KOS_DYN_UNWIND_ASYNC_STARTED) {
    KosEvalState_SetUnwindTargetFrame(
        es, (KosDynFrame *) KosDynNode_PARENT((KosDynNode *) wind));
    return EVAL_NORMAL();  /* eval continues in thunk frame; RETURN will resume unwinding */
  }
  if (r < 0) {
    KosDynWind_BODY_RESULT(wind) = NULL;
    if (KosEvalState_StackPushRef(es, body_result) < 0)
      Py_DECREF(body_result);
    return EVAL_RAISE();
  }
  return kos_op_result_from_push_int(r);
}


// The end.
