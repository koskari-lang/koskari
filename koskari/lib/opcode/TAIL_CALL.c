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
   TAIL_CALL: arg = N. Same stack as CALL [callable, arg_0, ..., arg_{N-1}].
   Tail call: do not push a new frame; remove or recycle the current one.

   Extracts args via KosStackSteal before call; KosContext_Call handles frame
   behavior and dispatch. Returns KOS_OP_RES_CONTROL_RETURNED when we push and
   continue (triggers kos_dynnode_on_control_returned for wind thunks).
*/

KOS_OP_HANDLER(TAIL_CALL) {
  unsigned n = KosOp_ARG(insn);
  size_t count = (size_t)(n + 1);

  KosDynFrame *frame_top = CURRENT_FRAME(ctx);
  KosStackBlock **stack = &KosDynFrame_STACK(frame_top);

  KosStackSteal steal = { 0 };
  if (KosStackSteal_Tail(&steal, count, stack) < 0)
    return KOS_OP_POST_ERR();

  PyObject *callable = steal.vals[0];
  PyObject *const *args =
    (n > 0) ? (PyObject *const *)&steal.vals[1] : NULL;

  int res = KosContext_Call(ctx, out_ev, callable, args,
                            (Py_ssize_t) n, NULL, 1);

  KosStackSteal_Free(&steal);
  return kos_op_outcome_from_call_int(ctx, res);
}
