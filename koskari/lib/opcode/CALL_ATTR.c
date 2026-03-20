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
   CALL_ATTR: arg = N (positional args). Stack [name, obj, arg_0, ..., arg_{N-1}]
   (name underneath obj). Resolve obj.name, then call. Unbound fn: fn(obj, *args).
   Bound/other: callable(*args).
*/
KOS_OP_HANDLER(CALL_ATTR) {
  unsigned n = KosOp_ARG(insn);
  unsigned count = n + 2;

  KosDynFrame *frame_top = CURRENT_FRAME(ctx);
  KosStackBlock **stack = &KosDynFrame_STACK(frame_top);

  PyObject *const *ptr = KosStackBlock_TailPtr(*stack, count);
  if (! ptr)
    return -1;

  PyObject *name = ptr[0];
  PyObject *obj = ptr[1];

  PyObject *resolved;
  int send_self = kos_resolve_named_attr(obj, name, NULL, &resolved);
  if (send_self < 0)
    return -1;

  PyObject *result;
  if (KosCallable_Check(resolved)) {
    KosStackSteal steal = { 0 };
    if (KosStackSteal_Tail(&steal, (size_t)count, stack) < 0) {
      Py_DECREF(resolved);
      return -1;
    }
    PyObject *const *args;
    Py_ssize_t nargs;
    if (send_self == 0) {
      args = &steal.vals[1];
      nargs = (Py_ssize_t) n + 1;
    } else {
      args = (n > 0) ? &steal.vals[2] : NULL;
      nargs = (Py_ssize_t) n;
    }
    int r = KosContext_Call(ctx, out_ev, resolved, args, nargs, NULL, 0);
    KosStackSteal_Free(&steal);
    Py_DECREF(resolved);
    return r;
  }

  if (send_self == 0) {
    result = PyObject_Vectorcall(resolved, &ptr[1], (size_t) n + 1, NULL);
  } else {
    result = PyObject_Vectorcall(resolved,
        (n > 0) ? &ptr[2] : NULL, (size_t) n, NULL);
  }
  Py_DECREF(resolved);
  if (! result)
    return -1;

  KosStackBlock_DropNoErr(stack, count);
  return kos_push_or_await(ctx, out_ev, result);
}
