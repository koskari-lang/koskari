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
  if (! KosContext_DYN_TOP(ctx) || ! KosDynNode_IS_WIND(KosContext_DYN_TOP(ctx))) {
    PyErr_SetString(PyExc_RuntimeError, "POP_WIND: top is not a wind node");
    return KOS_OP_POST_ERR();
  }

  KosDynWind *wind = (KosDynWind *) KosContext_DYN_TOP(ctx);
  PyObject *body_result = KosStackBlock_PopRef(&CURRENT_STACK(ctx));
  if (! body_result)
    return KOS_OP_POST_ERR();
  KosDynWind_BODY_RESULT(wind) = body_result;

  int r = kos_dyn_pop_top(ctx, KOS_XFER_EVENT_RETURN, out_ev);
  if (r == KOS_DYN_UNWIND_ASYNC_STARTED) {
    KosContext_UNWIND_TARGET_FRAME(ctx) =
        (KosDynFrame *) KosDynNode_PARENT((KosDynNode *) wind);
    return KOS_OP_RUN();  /* eval continues in thunk frame; RETURN will resume unwinding */
  }
  if (r < 0) {
    KosDynWind_BODY_RESULT(wind) = NULL;
    if (KosStackBlock_PushRef(&CURRENT_STACK(ctx), body_result) < 0)
      Py_DECREF(body_result);
    return KOS_OP_POST_ERR();
  }
  return kos_op_outcome_from_int_step(r);
}
