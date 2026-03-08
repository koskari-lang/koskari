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
   TAIL_CALL_NAMES: arg = N (number of positional arguments). Same
   stack layout as CALL_NAMES: [callable, pos_0, ..., pos_{N-1},
   kwd_val_0, ..., kwd_val_{M-1}, names]. TailPtr + vcall (no tuple).
   KosCallable: kos_callable_tail_vcall; tail install takes stack ownership,
   no DropNoErr. Plain Python: PyObject_Vectorcall, DropNoErr, kos_push_or_await.
*/

KOS_OP_HANDLER(TAIL_CALL_NAMES) {
  unsigned opa = KosOp_ARG(insn);

  KosStackBlock **stack = &CURRENT_STACK(ctx);

  PyObject *names = KosStackBlock_Peek(*stack);
  if (! names)
    return -1;
  if (! PyTuple_CheckExact(names)) {
    PyErr_Format(PyExc_RuntimeError, "TAIL_CALL_NAMES names must be tuple"
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
    int r = kos_callable_tail_vcall(ctx, out_ev, ptr[0], &ptr[1], npos,
                                    kwnames);
    /* No DropNoErr: tail install takes ownership of the stack. It either
       frees the frame (kos_tail_koscall) or resets it (kos_dynframe_reset_for_tail).
       Calling DropNoErr would be use-after-free or operate on NULL. */
    return r;
  }

  PyObject *result = PyObject_Vectorcall(ptr[0], &ptr[1], (size_t) npos,
                                         kwnames);
  if (! result)
    return -1;

  KosStackBlock_DropNoErr(stack, ntotal);
  return kos_push_or_await(ctx, out_ev, result);
}
