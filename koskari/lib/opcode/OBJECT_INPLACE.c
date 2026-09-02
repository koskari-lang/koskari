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
   OBJECT_INPLACE: arg = OPCOUNT (operator selector in the high byte, operand
   count in the low byte). Reduce the count operands from the stack
   left-to-right through the operator's PyNumber_InPlace* protocol and push the
   result. The stack in-place op used by the inplace-item / inplace-attr
   special forms. Cold: the op-type switch lives in the shared
   kos_object_inplace, out of the inlined dispatch loop.
*/

KOS_OP_HANDLER_COLD(OBJECT_INPLACE) {
  uint16_t arg = KosOp_ARG(insn);
  KosOperand op = (KosOperand) KosOpCount_TYPE(arg);
  int count = (int) KosOpCount_COUNT(arg);

  if (unlikely(kos_object_inplace(es, op, count) < 0))
    RESULT_RAISE();
  RESULT_NEXT(es);
}


// The end.
