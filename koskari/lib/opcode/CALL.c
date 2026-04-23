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
  ..., arg_{N-1}] (callable oldest). KosCallable: steal then KosEvalState_Call.
  Plain Python: TailPtr + PyObject_Vectorcall + DropNoErr (fast path).
*/

KOS_OP_HANDLER(CALL) {
  KosContext *ctx = KosEvalState_CONTEXT(es);
  unsigned n = KosOp_ARG(insn);

  PyObject *const *ptr = KosEvalState_StackTailPtr(es, n + 1);
  if (! ptr)
    return KOS_OP_POST_ERR();

  SAYF("CALL: callable=%R", ptr[0]);

  if (KosCallable_Check(ptr[0])) {
    KosStackSteal steal = { 0 };
    if (KosEvalState_StackSteal_Tail(es, &steal, (size_t)(n + 1)) < 0)
      return KOS_OP_POST_ERR();
    int r = KosEvalState_Call(es, out_ev, steal.vals[0],
                            (n > 0) ? &steal.vals[1] : NULL,
                            (Py_ssize_t) n, NULL, 0);
    KosStackSteal_Free(&steal);
    return kos_op_outcome_from_call_int(ctx, r);

  } else {
    PyObject *result;

    if (n == 0) {
      result = PyObject_CallNoArgs(ptr[0]);
    } else {
      result = PyObject_Vectorcall(ptr[0], &ptr[1], (size_t) n, NULL);
    }

    SAYF("CALL: result=%R", result);
    KosEvalState_StackDropNoErr(es, n + 1);
    return result
        ? kos_op_outcome_from_push_int(op_push_or_await(es, out_ev, result))
        : KOS_OP_POST_ERR();
  }
}
