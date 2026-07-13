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
   OBJECT_OPERAND: arg = OPCOUNT (operator selector in the high byte, operand
   count in the low byte). Pop the count operands and apply the operator,
   pushing the result. Cold: the op-type switch lives in the shared
   kos_object_operand, out of the inlined dispatch loop.
*/

KOS_OP_HANDLER_COLD(OBJECT_OPERAND) {
  uint16_t arg = KosOp_ARG(insn);
  KosOperand op = (KosOperand) KosOpCount_TYPE(arg);
  int count = (int) KosOpCount_COUNT(arg);

  return kos_op_result_from_push_int(
      kos_object_operand(es, op, count));
}


// The end.
