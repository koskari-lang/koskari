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
  KOS_SYNC_PC(ctx, insn);
  const KosExtra *row = KosOp_ROW(insn);

  PyObject *rhs = KosExtra_OBJ(row, 1);

  PyObject *value = KosContext_StackPeek(ctx);
  if (unlikely(! value))
    RESULT_RAISE();

  int res = PyObject_RichCompareBool(value, rhs, Py_EQ);
  if (unlikely(res < 0))
    RESULT_RAISE();

  if (res) {
    KosContext_StackDropNoErr(ctx, 1);
    RESULT_JUMP_AT(ctx, KosExtra_TARGET(row, 0));
  }

  RESULT_NEXT(ctx);
}


// The end.
