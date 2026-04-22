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
  with names at TOS. KosCallable: steal then KosContext_Call. Plain Python:
  TailPtr + PyObject_Vectorcall + DropNoErr.
*/
KOS_OP_HANDLER(CALL_NAMES) {
  KosContext *ctx = KosEvalState_Context(es);
  unsigned opa = KosOp_ARG(insn);

  PyObject *names = KosEvalState_StackPeek(es);
  if (! names)
    return KOS_OP_POST_ERR();
  if (! PyTuple_CheckExact(names)) {
    PyErr_Format(PyExc_RuntimeError, "CALL_NAMES names must be tuple"
                 ", not %R", names);
    return KOS_OP_POST_ERR();
  }

  Py_ssize_t npos = (Py_ssize_t) opa;
  Py_ssize_t nkwds = PyTuple_GET_SIZE(names);
  Py_ssize_t total = npos + nkwds;
  size_t ntotal = (size_t)(total + 2);

  PyObject *const *ptr = KosEvalState_StackTailPtr(es, ntotal);
  if (! ptr)
    return KOS_OP_POST_ERR();

  PyObject *kwnames = nkwds ? ptr[total + 1] : NULL;

  if (KosCallable_Check(ptr[0])) {
    KosStackSteal steal = { 0 };
    if (KosEvalState_StackSteal_Tail(es, &steal, ntotal) < 0)
      return KOS_OP_POST_ERR();
    PyObject *kwnames_arg = nkwds ? steal.vals[total + 1] : NULL;
    int r = KosContext_Call(ctx, out_ev, steal.vals[0],
                            &steal.vals[1], npos, kwnames_arg, 0);
    KosStackSteal_Free(&steal);
    return kos_op_outcome_from_call_int(ctx, r);
  }

  PyObject *result = PyObject_Vectorcall(ptr[0], &ptr[1], (size_t) npos,
                                         kwnames);
  if (! result)
    return KOS_OP_POST_ERR();

  KosEvalState_StackDropNoErr(es, ntotal);
  return kos_op_outcome_from_push_int(op_push_or_await(es, out_ev, result));
}
