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
   otherwise same semantics as CALL.
*/

KOS_OP_HANDLER(TAIL_CALL) {
  unsigned n = KosOp_ARG(insn);

  KosDynFrame *frame_top = CURRENT_FRAME(ctx);

  PyObject *args = NULL;
  if (n > 0) {
    args = KosStackBlock_PopTail(&KosDynFrame_STACK(frame_top), n);
    if (! args)
      return -1;
  }

  PyObject *callable = KosStackBlock_PopRef(&KosDynFrame_STACK(frame_top));
  if (! callable)
    return -1;

  int r;
  if (KosPyDelimitedContinuation_Check(callable)
      || KosPyContinuation_Check(callable)) {
    r = call_call_continuation(ctx, callable, args, out_ev, insn);

  } else if (Py_TYPE(callable) == &KosExpressionType) {
    if (n > 0) {
      PyErr_SetString(PyExc_TypeError, "Expression takes no arguments");
      r = -1;
    } else {
      r = call_tail_flattened_expr(ctx, callable, out_ev);
    }

  } else if (Py_TYPE(callable) == &KosFunctionType) {
    r = call_tail_flattened_function(ctx, callable, args, out_ev);

  } else {
    r = call_pyobject_call(ctx, callable, args, out_ev);
  }

  Py_DECREF(callable);
  Py_XDECREF(args);
  return r;
}
