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
  const KosExtra *row = KosOp_ROW(insn);

  PyObject *ns = CURRENT_NS();
  KOS_OP_REQUIRE_NS(ns);
  size_t idx = (size_t) KosExtra_IMM(row, 1);
  PyObject *rhs = kos_ns_fast_deref(ns, idx);
  if (unlikely(! rhs))
    RESULT_RAISE();

  PyObject *value = KosEvalState_StackPeek(es);
  if (unlikely(! value)) {
    Py_DECREF(rhs);
    RESULT_RAISE();
  }

  int res = PyObject_RichCompareBool(value, rhs, Py_EQ);
  Py_DECREF(rhs);
  if (unlikely(res < 0))
    RESULT_RAISE();

  if (res) {
    KosEvalState_StackDropNoErr(es, 1);
    RESULT_JUMP_AT(es, KosExtra_TARGET(row, 0));
  }

  RESULT_NEXT(es);
}


// The end.
