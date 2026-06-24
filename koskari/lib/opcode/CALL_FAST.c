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
  KosCallable: steal args then KosEvalState_Call. Plain Python: TailPtr +
  PyObject_Vectorcall + DropNoErr.
*/

KOS_OP_HANDLER(CALL_FAST) {
  KosCode *code = KosEvalState_CurrentCode(es);
  KosExtra row = KosCode_GetExtra(code, KosOp_ARG(insn));
  uint32_t slot_idx = row.a0;
  unsigned n = row.a1;

  PyObject *ns = CURRENT_NS();
  if (! ns) {
    PyErr_SetString(PyExc_RuntimeError, "CALL_FAST on NULL namespace");
    return EVAL_RAISE();
  }
  PyObject *callable = kos_ns_fast_deref(ns, (size_t) slot_idx);
  if (! callable)
    return EVAL_RAISE();

  if (KosCallable_Check(callable)) {
    KosStackSteal steal = { 0 };
    if (KosEvalState_StackSteal_Tail(es, &steal, (size_t) n) < 0) {
      Py_DECREF(callable);
      return EVAL_RAISE();
    }
    KosOpResult r = KosEvalState_Call(es, callable,
                            (n > 0) ? steal.vals : NULL,
                            (Py_ssize_t) n, NULL, 0);
    Py_DECREF(callable);
    KosStackSteal_Free(&steal);
    return r;
  }

  PyObject *result = NULL;
  if (n == 0) {
    result = PyObject_CallNoArgs(callable);

  } else {
    PyObject *const *ptr = KosEvalState_StackTailPtr(es, n);
    if (! ptr) {
      Py_DECREF(callable);
      return EVAL_RAISE();
    }

    result = PyObject_Vectorcall(callable, ptr, (size_t) n, NULL);
    KosEvalState_StackDropNoErr(es, n);
  }

  Py_DECREF(callable);
  return kos_op_result_PUSH_OR_AWAIT(es, result);
}


// The end.
