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
  CALL_FAST: arg = extra index. Schema FASTVAR (slot), COUNT (arg count).
  Call callable from fast local slot. Stack [arg_0, ..., arg_{N-1}].
  KosCallable: steal args then KosContext_Call. Plain Python: TailPtr +
  PyObject_Vectorcall + DropNoErr.
*/

KOS_OP_HANDLER(CALL_FAST) {
  KosContext *ctx = KosEvalState_Context(es);
  KosCode *code = CURRENT_CODE(ctx);
  KosExtra row = KosCode_GetExtra(code, KosOp_ARG(insn));
  uint32_t slot_idx = row.a0;
  unsigned n = row.a1;

  PyObject *ns = CURRENT_NS(ctx);
  if (! ns) {
    PyErr_SetString(PyExc_RuntimeError, "CALL_FAST on NULL namespace");
    return KOS_OP_POST_ERR();
  }
  if (! KosNamespace_SupportsFast(ns)) {
    PyErr_Format(PyExc_RuntimeError, "CALL_FAST on %R", ns);
    return KOS_OP_POST_ERR();
  }

  PyObject *callable = KosNamespace_FastDeref(ns, (size_t) slot_idx);
  if (! callable)
    return KOS_OP_POST_ERR();

  if (KosCallable_Check(callable)) {
    KosStackSteal steal = { 0 };
    if (KosStackSteal_Tail(&steal, (size_t) n, KosEvalState_StackOperandSlot(es)) < 0) {
      Py_DECREF(callable);
      return KOS_OP_POST_ERR();
    }
    int r = KosContext_Call(ctx, out_ev, callable,
                            (n > 0) ? steal.vals : NULL,
                            (Py_ssize_t) n, NULL, 0);
    Py_DECREF(callable);
    KosStackSteal_Free(&steal);
    return kos_op_outcome_from_call_int(ctx, r);
  }

  PyObject *result = NULL;
  if (n == 0) {
    result = PyObject_CallNoArgs(callable);

  } else {
    PyObject *const *ptr = KosEvalState_StackTailPtr(es, n);
    if (! ptr) {
      Py_DECREF(callable);
      return KOS_OP_POST_ERR();
    }

    result = PyObject_Vectorcall(callable, ptr, (size_t) n, NULL);
    KosEvalState_StackDropNoErr(es, n);
  }

  Py_DECREF(callable);
  return result ? kos_op_outcome_from_push_int(op_push_or_await(es, out_ev, result)) : KOS_OP_POST_ERR();
}


// The end.
