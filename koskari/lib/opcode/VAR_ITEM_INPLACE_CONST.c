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
   VAR_ITEM_INPLACE_CONST: arg = extra index. Schema OPERAND (selector),
   VAR (obj name), VAR (key name), CONST (rhs). obj[key] OP= rhs with
   both operands resolved by name in the frame namespace and the operand
   from the pool; no stack effect. The cold twin of
   FAST_ITEM_INPLACE_CONST.
*/

KOS_OP_HANDLER(VAR_ITEM_INPLACE_CONST) {
  const KosExtra *row = KosOp_ROW(insn);
  KosOperand op = (KosOperand) KosExtra_IMM(row, 0);
  PyObject *rhs = KosExtra_OBJ(row, 3);

  PyObject *obj = frame_ns_deref(es, KosExtra_OBJ(row, 1));
  if (! obj)
    RESULT_RAISE();

  PyObject *key = frame_ns_deref(es, KosExtra_OBJ(row, 2));
  if (! key) {
    Py_DECREF(obj);
    RESULT_RAISE();
  }

  int res = kos_item_inplace_reduce(obj, key, op, &rhs, 1);
  Py_DECREF(obj);
  Py_DECREF(key);
  if (unlikely(res < 0))
    RESULT_RAISE();
  RESULT_NEXT(es);
}


// The end.
