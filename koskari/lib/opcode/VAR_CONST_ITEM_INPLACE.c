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
   VAR_CONST_ITEM_INPLACE: arg = extra index. Schema OPCOUNT (operator
   selector and count), VAR (obj name), CONST (key). Stack [v0 ..
   v_{count-1}]; obj[key] OP= each value left to right with the object
   resolved by name after the values were pushed; the values are
   dropped, nothing is pushed. The cold twin of FAST_CONST_ITEM_INPLACE.
*/

KOS_OP_HANDLER_COLD(VAR_CONST_ITEM_INPLACE) {
  const KosExtra *row = KosOp_ROW(insn);
  KosOperand op = (KosOperand) KosOpCount_TYPE(KosExtra_IMM(row, 0));
  int count = (int) KosOpCount_COUNT(KosExtra_IMM(row, 0));
  PyObject *key = KosExtra_OBJ(row, 2);

  PyObject *const *args = KosEvalState_StackTailPtr(es, (size_t) count);
  if (count && ! args)
    RESULT_RAISE();

  PyObject *obj = frame_ns_deref(es, KosExtra_OBJ(row, 1));
  if (! obj)
    RESULT_RAISE();

  int res = kos_item_inplace_reduce(obj, key, op, args, count);
  KosEvalState_StackDropNoErr(es, (size_t) count);
  Py_DECREF(obj);
  if (unlikely(res < 0))
    RESULT_RAISE();
  RESULT_NEXT(es);
}


// The end.
