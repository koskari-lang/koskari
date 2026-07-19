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
   count operands from the stack into the slot with the operator. Cold: the
   op-type switch lives in the shared kos_fast_inplace_operand, out of the
   inlined dispatch loop.
*/

KOS_OP_HANDLER_COLD(FAST_INPLACE) {
  KosCode *code = KosEvalState_CurrentCode(es);
  KosExtra row = KosCode_GetExtra(code, KosOp_ARG(insn));

  KosOperand op = (KosOperand) KosOpCount_TYPE(row.a0);
  size_t idx = (size_t) row.a1;

  return kos_op_result_from_push_int(
      kos_fast_inplace_operand(es, op, idx));
}


// The end.
