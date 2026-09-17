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
   CONST_ITEM_INPLACE: arg = extra index. Schema OPCOUNT (operator
   selector and count), CONST (key). Stack [obj, v0 .. v_{count-1}];
   obj[key] OP= each value left to right through kos_item_inplace_reduce,
   then every entry is dropped; pushes nothing.
*/

KOS_OP_HANDLER_COLD(CONST_ITEM_INPLACE) {
  const KosExtra *row = KosOp_ROW(insn);
  KosOperand op = (KosOperand) KosOpCount_TYPE(KosExtra_IMM(row, 0));
  int count = (int) KosOpCount_COUNT(KosExtra_IMM(row, 0));
  PyObject *key = KosExtra_OBJ(row, 1);

  PyObject *const *tail = KosEvalState_StackTailPtr(es, (size_t) (count + 1));
  if (! tail)
    RESULT_RAISE();

  int res = kos_item_inplace_reduce(tail[0], key, op, &tail[1], count);
  KosEvalState_StackDropNoErr(es, (size_t) (count + 1));
  if (unlikely(res < 0))
    RESULT_RAISE();
  RESULT_NEXT(es);
}


// The end.
