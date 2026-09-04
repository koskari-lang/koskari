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
   FAST_INPLACE: arg = EXTRA row index. Row a0 = OPCOUNT
   (op-type high byte, reduce count low byte), a1 = fast slot. Reduce the
   count operands from the stack into the slot with the operator. Hot: the
   row decode is inlined; the op-type switch stays out of line in the shared
   kos_fast_inplace_operand.
*/

KOS_OP_HANDLER(FAST_INPLACE) {
  const KosExtra *row = KosOp_ROW(insn);

  KosOperand op = (KosOperand) KosOpCount_TYPE(KosExtra_IMM(row, 0));
  size_t idx = (size_t) KosExtra_IMM(row, 1);

  if (unlikely(kos_fast_inplace_operand(es, op, idx) < 0))
    RESULT_RAISE();
  RESULT_NEXT(es);
}


// The end.
