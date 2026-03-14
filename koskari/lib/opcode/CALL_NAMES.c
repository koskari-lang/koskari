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
  CALL_NAMES: arg = N (number of positional arguments). Stack:
  [callable, pos_0, ..., pos_{N-1}, kwd_val_0, ..., kwd_val_{M-1}, names]
  with names at TOS. KosCallable: steal then kos_callable_vcall (stack clear
  before install). Plain Python: TailPtr + PyObject_Vectorcall + DropNoErr.
*/
KOS_OP_HANDLER(CALL_NAMES) {
  unsigned opa = KosOp_ARG(insn);

  KosStackBlock **stack = &CURRENT_STACK(ctx);

  PyObject *names = KosStackBlock_Peek(*stack);
  if (! names)
    return -1;
  if (! PyTuple_CheckExact(names)) {
    PyErr_Format(PyExc_RuntimeError, "CALL_NAMES names must be tuple"
                 ", not %R", names);
    return -1;
  }

  Py_ssize_t npos = (Py_ssize_t) opa;
  Py_ssize_t nkwds = PyTuple_GET_SIZE(names);
  Py_ssize_t total = npos + nkwds;
  size_t ntotal = (size_t)(total + 2);

  PyObject *const *ptr = KosStackBlock_TailPtr(*stack, ntotal);
  if (! ptr)
    return -1;

  PyObject *kwnames = nkwds ? ptr[total + 1] : NULL;

  if (KosCallable_Check(ptr[0])) {
    KosStackSteal steal;
    if (KosStackSteal_Tail(&steal, ntotal, stack) < 0)
      return -1;
    PyObject *kwnames_arg = nkwds ? steal.vals[total + 1] : NULL;
    int r = kos_callable_vcall(ctx, out_ev, steal.vals[0],
                               &steal.vals[1], npos, kwnames_arg);
    KosStackSteal_Free(&steal);
    return r;
  }

  PyObject *result = PyObject_Vectorcall(ptr[0], &ptr[1], (size_t) npos,
                                         kwnames);
  if (! result)
    return -1;

  KosStackBlock_DropNoErr(stack, ntotal);
  return kos_push_or_await(ctx, out_ev, result);
}
