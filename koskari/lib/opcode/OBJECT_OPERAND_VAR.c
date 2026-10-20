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
   OBJECT_OPERAND_VAR: arg = extra index. Schema OPERAND, VAR. The top
   of the stack OP the named variable's value, the result replacing
   the top. The fast pass lowers it to OBJECT_OPERAND_FAST.
*/

KOS_OP_HANDLER(OBJECT_OPERAND_VAR) {
  KOS_SYNC_PC(ctx, insn);
  const KosExtra *row = KosOp_ROW(insn);
  KosSegment **bl = KosEvalState_BLOCK(ctx);
  PyObject *const *top = kos_stack_tail_ptr_fast(*bl, 1);
  PyObject *pair[2];
  PyObject *result;

  if (unlikely(! top))
    RESULT_RAISE();
  pair[1] = frame_ns_deref(ctx, KosExtra_OBJ(row, 1));
  if (unlikely(! pair[1]))
    RESULT_RAISE();
  pair[0] = top[0];
  result = kos_operand_reduce((KosOperand) KosExtra_IMM(row, 0), pair, 2);
  Py_DECREF(pair[1]);
  if (unlikely(! result))
    RESULT_RAISE();
  kos_stack_drop1_fast(bl);
  RESULT_STEAL(ctx, result);
}


// The end.
