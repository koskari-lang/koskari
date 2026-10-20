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
   VAR_INPLACE: arg = EXTRA row index. Row a0 = OPERAND (the bare operator
   selector), a1 = constant-pool index of the variable name. Pop the one rhs
   from the stack and reduce it into the frame namespace binding with the
   operator. Cold: the op-type switch lives in the shared
   kos_var_inplace_operand, out of the inlined dispatch loop.
*/

KOS_OP_HANDLER(VAR_INPLACE) {
  KOS_SYNC_PC(ctx, insn);
  const KosExtra *row = KosOp_ROW(insn);

  KosOperand op = (KosOperand) KosExtra_IMM(row, 0);
  PyObject *name = KosExtra_OBJ(row, 1);

  if (unlikely(kos_var_inplace_operand(ctx, op, name) < 0))
    RESULT_RAISE();
  RESULT_NEXT(ctx);
}


// The end.
