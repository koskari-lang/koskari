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
   CALL: arg = number of positional arguments N. Stack has [callable, arg_0,
   ..., arg_{N-1}] (callable oldest). Call callable with those args, drop
   them, then push result or await it.
*/

KOS_OP_HANDLER(CALL) {
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

  int r = kos_call_dispatch(ctx, out_ev, callable, args, 0);

  Py_DECREF(callable);
  Py_XDECREF(args);
  return r;
}
