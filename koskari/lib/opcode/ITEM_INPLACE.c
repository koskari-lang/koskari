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
   ITEM_INPLACE: arg = OPCOUNT (operator selector in the high byte,
   reduction count in the low byte). Stack [obj, key, v0 .. v_{count-1}];
   obj[key] OP= each value left to right through kos_item_inplace_reduce,
   then every entry is dropped; pushes nothing (the special appends CONST
   None). Cold: the operator switch lives in the shared reduce.
*/

KOS_OP_HANDLER(ITEM_INPLACE) {
  uint16_t arg = KosOp_ARG(insn);
  KosOperand op = (KosOperand) KosOpCount_TYPE(arg);
  int count = (int) KosOpCount_COUNT(arg);

  PyObject *const *tail = KosEvalState_StackTailPtr(es, (size_t) (count + 2));
  if (! tail)
    RESULT_RAISE();

  int res = kos_item_inplace_reduce(tail[0], tail[1], op, &tail[2], count);
  KosEvalState_StackDropNoErr(es, (size_t) (count + 2));
  if (unlikely(res < 0))
    RESULT_RAISE();
  RESULT_NEXT(es);
}


// The end.
