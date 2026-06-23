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
  SWITCH_EQ_FAST: arg = extra index. Schema (LABEL, FASTVAR, NOARG, NOARG):
  row.a0 = target PC, row.a1 = fast slot index. Stack [..., s] with
  scrutinee = TOS. Resolve fast slot in current namespace and compare
  to s via Py_EQ. On match: pop scrutinee and jump to row.a0. On
  mismatch: fall through with stack unchanged.
*/

KOS_OP_HANDLER(SWITCH_EQ_FAST) {
  KosCode *code = KosEvalState_CurrentCode(es);
  KosExtra row = KosCode_GetExtra(code, KosOp_ARG(insn));

  PyObject *ns = CURRENT_NS();
  if (unlikely(! ns)) {
    PyErr_SetString(PyExc_RuntimeError,
                    "SWITCH_EQ_FAST invoked on NULL namespace");
    return EVAL_RAISE();
  }
  if (unlikely(! KosNamespace_SupportsFast(ns))) {
    PyErr_Format(PyExc_RuntimeError,
                 "SWITCH_EQ_FAST invoked on %R", ns);
    return EVAL_RAISE();
  }

  size_t idx = (size_t) row.a1;
  PyObject *rhs = KosNamespace_FastDeref(ns, idx);
  if (unlikely(! rhs))
    return EVAL_RAISE();

  PyObject *value = KosEvalState_StackPeek(es);
  if (unlikely(! value)) {
    Py_DECREF(rhs);
    return EVAL_RAISE();
  }

  int res = PyObject_RichCompareBool(value, rhs, Py_EQ);
  Py_DECREF(rhs);
  if (unlikely(res < 0))
    return EVAL_RAISE();

  if (res) {
    KosEvalState_StackDropNoErr(es, 1);
    KosEvalState_SET_PC(es, row.a0);
  }

  return EVAL_NORMAL();
}


// The end.
