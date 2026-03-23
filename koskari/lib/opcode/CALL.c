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
  ..., arg_{N-1}] (callable oldest). KosCallable: steal then KosContext_Call.
  Plain Python: TailPtr + PyObject_Vectorcall + DropNoErr (fast path).
*/

KOS_OP_HANDLER(CALL) {
  unsigned n = KosOp_ARG(insn);

  KosDynFrame *frame_top = CURRENT_FRAME(ctx);
  KosStackBlock **stack = &KosDynFrame_STACK(frame_top);

  PyObject *const *ptr = KosStackBlock_TailPtr(*stack, n + 1);
  if (! ptr)
    return -1;

  SAYF("CALL: callable=%R", ptr[0]);

  if (KosCallable_Check(ptr[0])) {
    KosStackSteal steal = { 0 };
    if (KosStackSteal_Tail(&steal, (size_t)(n + 1), stack) < 0)
      return -1;
    int r = KosContext_Call(ctx, out_ev, steal.vals[0],
                            (n > 0) ? &steal.vals[1] : NULL,
                            (Py_ssize_t) n, NULL, 0);
    KosStackSteal_Free(&steal);
    return r;

  } else {
    PyObject *result;

    if (n == 0) {
      result = PyObject_CallNoArgs(ptr[0]);
    } else {
      result = PyObject_Vectorcall(ptr[0], &ptr[1], (size_t) n, NULL);
    }

    SAYF("CALL: result=%R", result);
    KosStackBlock_DropNoErr(stack, n + 1);
    return result ? kos_push_or_await(ctx, out_ev, result) : -1;
  }
}
