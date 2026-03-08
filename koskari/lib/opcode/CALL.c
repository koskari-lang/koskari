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
  ..., arg_{N-1}] (callable oldest). TailPtr + vcall + DropNoErr (no tuple).
  KosCallable: kos_callable_vcall (install only pushes frames). Plain Python:
  PyObject_Vectorcall, DropNoErr, kos_push_or_await.
*/
KOS_OP_HANDLER(CALL) {
  unsigned n = KosOp_ARG(insn);

  KosDynFrame *frame_top = CURRENT_FRAME(ctx);
  KosStackBlock **stack = &KosDynFrame_STACK(frame_top);

  PyObject *const *ptr = KosStackBlock_TailPtr(*stack, n + 1);
  if (! ptr)
    return -1;

  if (KosCallable_Check(ptr[0])) {
    int r = kos_callable_vcall(ctx, out_ev, ptr[0],
                               (n > 0) ? &ptr[1] : NULL,
                               (Py_ssize_t) n, NULL);
    KosStackBlock_DropNoErr(stack, n + 1);
    return r;
  }

  PyObject *result = PyObject_Vectorcall(ptr[0],
                                         (n > 0) ? &ptr[1] : NULL,
                                         (size_t) n, NULL);
  if (! result)
    return -1;

  KosStackBlock_DropNoErr(stack, n + 1);
  return kos_push_or_await(ctx, out_ev, result);
}
