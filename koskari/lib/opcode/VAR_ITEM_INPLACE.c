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
  VAR_ITEM_INPLACE: arg = extra index. Schema OPCOUNT, VAR, NOARG,
  NOARG. Stack [key, v0 .. v(count-1)]. Object from the frame
  namespace by name, key from the stack, operand from the stack.
  obj[key] = obj[key] OP operands, reduced left to right; push
  nothing. The access runs through kos_item_inplace
  (eval/eval_item.c) with no slot; the cold twin of the FAST row.
*/
KOS_OP_HANDLER(VAR_ITEM_INPLACE) {
  KOS_SYNC_PC(ctx, insn);
  const KosExtra *row = KosOp_ROW(insn);
  KosSegment **bl = KosEvalState_BLOCK(ctx);
  PyObject *const *tail;
  uint32_t opc = KosExtra_IMM(row, 0);
  KosOperand op = (KosOperand) KosOpCount_TYPE(opc);
  int count = (int) KosOpCount_COUNT(opc);
  PyObject *obj;
  PyObject *key;
  int rc;

  obj = frame_ns_deref(ctx, KosExtra_OBJ(row, 1));
  if (unlikely(! obj))
    RESULT_RAISE();

  tail = kos_stack_tail_ptr_fast(*bl, (size_t) count + 1);
  if (unlikely(! tail)) {
    Py_DECREF(obj);
    RESULT_RAISE();
  }

  key = tail[0];
  rc = kos_item_inplace(KosOp_SLOT(insn), obj, key, op, tail + 1, count);
  Py_DECREF(obj);

  if (unlikely(rc < 0))
    RESULT_RAISE();

  kos_stack_drop_noerr_fast(bl, (size_t) count + 1);
  RESULT_NEXT(ctx);
}


// The end.
