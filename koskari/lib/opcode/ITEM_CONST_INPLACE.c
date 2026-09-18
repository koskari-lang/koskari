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
  ITEM_CONST_INPLACE: arg = extra index. Schema OPCOUNT, CONST,
  NOARG, NOARG. Stack [obj, v0 .. v(count-1)]. Object from the stack,
  key from the pool, operand from the stack. obj[key] = obj[key] OP
  operands, reduced left to right; push nothing. The access runs
  through kos_item_inplace (eval/eval_item.c) on the site's one
  KOS_CACHE_ITEM slot.
*/
KOS_OP_HANDLER(ITEM_CONST_INPLACE) {
  const KosExtra *row = KosOp_ROW(insn);
  KosSegment **bl = KosEvalState_BLOCK(es);
  PyObject *const *tail;
  uint32_t opc = KosExtra_IMM(row, 0);
  KosOperand op = (KosOperand) KosOpCount_TYPE(opc);
  int count = (int) KosOpCount_COUNT(opc);
  PyObject *obj;
  PyObject *key = KosExtra_OBJ(row, 1);
  int rc;

  tail = kos_stack_tail_ptr_fast(*bl, (size_t) count + 1);
  if (unlikely(! tail))
    RESULT_RAISE();

  obj = tail[0];
  rc = kos_item_inplace(KosOp_SLOT(insn), obj, key, op, tail + 1, count);

  if (unlikely(rc < 0))
    RESULT_RAISE();

  kos_stack_drop_noerr_fast(bl, (size_t) count + 1);
  RESULT_NEXT(es);
}


// The end.
