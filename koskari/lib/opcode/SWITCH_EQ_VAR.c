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
  SWITCH_EQ_VAR: arg = extra index. Schema (LABEL, VAR, NOARG, NOARG):
  row.a0 = target PC, row.a1 = constant pool index for the variable
  name. Stack [..., s] with scrutinee = TOS. Resolve var from frame
  namespace and compare to s via Py_EQ. On match: pop scrutinee and
  jump to row.a0. On mismatch: fall through with stack unchanged.
*/

KOS_OP_HANDLER(SWITCH_EQ_VAR) {
  KOS_SYNC_PC(es, insn);
  const KosExtra *row = KosOp_ROW(insn);

  PyObject *var_name = KosExtra_OBJ(row, 1);

  PyObject *rhs = frame_ns_deref(es, var_name);
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
