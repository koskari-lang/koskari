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
   VAR_INPLACE: arg = EXTRA row index. Row a0 = OPCOUNT
   (op-type high byte, reduce count low byte), a1 = constant-pool index of the
   variable name. Reduce the count operands from the stack into the frame
   namespace binding with the operator. Cold: the op-type switch lives in the
   shared kos_var_inplace_operand, out of the inlined dispatch loop.
*/

KOS_OP_HANDLER_COLD(VAR_INPLACE) {
  KosCode *code = KosEvalState_CurrentCode(es);
  KosExtra row = KosCode_GetExtra(code, KosOp_ARG(insn));

  KosOperand op = (KosOperand) KosOpCount_TYPE(row.a0);
  PyObject *name = KosEvalState_CONST(es, row.a1);
  if (unlikely(! name))
    return EVAL_RAISE();

  return kos_op_result_from_push_int(
      kos_var_inplace_operand(es, op, name));
}


// The end.
