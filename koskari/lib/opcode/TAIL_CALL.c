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
   For KosExpression/KosFunction, replace current frame and continue in-place;
   otherwise same semantics as CALL. Uses vectorcall convention via
   kos_callable_tail_vcall, with args popped into a tuple for ref safety.
*/

KOS_OP_HANDLER(TAIL_CALL) {
  unsigned n = KosOp_ARG(insn);

  KosDynFrame *frame_top = CURRENT_FRAME(ctx);
  KosStackBlock **stack = &KosDynFrame_STACK(frame_top);

  PyObject *args = NULL;
  if (n > 0) {
    args = KosStackBlock_PopTail(stack, n);
    if (! args)
      return -1;
  }

  PyObject *callable = KosStackBlock_PopRef(stack);
  if (! callable) {
    Py_XDECREF(args);
    return -1;
  }

  PyObject *const *argv = args ? &PyTuple_GET_ITEM(args, 0) : NULL;
  int r = kos_callable_tail_vcall(ctx, out_ev, callable, argv, (Py_ssize_t) n, NULL);

  Py_DECREF(callable);
  Py_XDECREF(args);
  return r;
}
