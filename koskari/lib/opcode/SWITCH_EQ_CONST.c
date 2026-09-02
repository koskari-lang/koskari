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
  SWITCH_EQ_CONST: arg = extra index. Schema (LABEL, CONST, NOARG, NOARG):
  row.a0 = target PC, row.a1 = constant pool index for RHS. Stack
  [..., s] with scrutinee = TOS. Compare s to constant via Py_EQ. On
  match: pop scrutinee and jump to row.a0. On mismatch: fall through
  with stack unchanged.
*/

KOS_OP_HANDLER(SWITCH_EQ_CONST) {
  KosCode *code = KosEvalState_CurrentCode(es);
  KosExtra row = KosCode_GetExtra(code, KosOp_ARG(insn));

  PyObject *rhs = KosEvalState_CONST(es, row.a1);
  if (unlikely(! rhs))
    RESULT_RAISE();

  PyObject *value = KosEvalState_StackPeek(es);
  if (unlikely(! value))
    RESULT_RAISE();

  int res = PyObject_RichCompareBool(value, rhs, Py_EQ);
  if (unlikely(res < 0))
    RESULT_RAISE();

  if (res) {
    KosEvalState_StackDropNoErr(es, 1);
    KosEvalState_SET_PC(es, row.a0);
  }

  RESULT_NEXT(es);
}


// The end.
