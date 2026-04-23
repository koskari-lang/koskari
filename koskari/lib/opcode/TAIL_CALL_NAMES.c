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
   TAIL_CALL_NAMES: arg = N (number of positional arguments). Same stack
   layout as CALL_NAMES: [callable, pos_0, ..., pos_{N-1}, kwd_val_0, ...,
   kwd_val_{M-1}, names]. Tail call: do not push a new frame; remove or
   recycle the current one.

   Extracts args via KosStackSteal before call; KosContext_Call handles frame
   behavior and dispatch.
*/

KOS_OP_HANDLER(TAIL_CALL_NAMES) {
  KosContext *ctx = KosEvalState_CONTEXT(es);
  unsigned opa = KosOp_ARG(insn);
  PyObject *names = KosEvalState_StackPeek(es);
  if (! names)
    return KOS_OP_POST_ERR();
  if (! PyTuple_CheckExact(names)) {
    PyErr_Format(PyExc_RuntimeError, "TAIL_CALL_NAMES names must be tuple"
                 ", not %R", names);
    return KOS_OP_POST_ERR();
  }

  Py_ssize_t npos = (Py_ssize_t) opa;
  Py_ssize_t nkwds = PyTuple_GET_SIZE(names);
  Py_ssize_t total = npos + nkwds;
  size_t ntotal = (size_t)(total + 2);

  KosStackSteal steal = { 0 };
  if (KosEvalState_StackSteal_Tail(es, &steal, ntotal) < 0)
    return KOS_OP_POST_ERR();

  PyObject *callable = steal.vals[0];
  PyObject *const *args =
    (total > 0) ? (PyObject *const *)&steal.vals[1] : NULL;
  PyObject *kwnames_arg = nkwds ? steal.vals[total + 1] : NULL;

  int res = KosContext_Call(ctx, out_ev, callable, args,
                            npos, kwnames_arg, 1);

  KosStackSteal_Free(&steal);
  return kos_op_outcome_from_call_int(ctx, res);
}
