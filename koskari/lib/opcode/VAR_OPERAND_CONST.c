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
   VAR_OPERAND_CONST: arg = extra index. Schema OPERAND, VAR, CONST.
   The named variable's value OP the pool constant, the result pushed.
   The VAR twin of FAST_OPERAND_CONST for a name without a fast slot;
   resolved by name on each execution, no cache.
*/

KOS_OP_HANDLER(VAR_OPERAND_CONST) {
  KOS_SYNC_PC(ctx, insn);
  const KosExtra *row = KosOp_ROW(insn);
  PyObject *name = KosExtra_OBJ(row, 1);
  PyObject *operand = KosExtra_OBJ(row, 2);   /* borrowed */
  PyObject *obj = frame_ns_deref(ctx, name);
  if (unlikely(! obj))
    RESULT_RAISE();
  PyObject *pair[2] = { obj, operand };
  PyObject *result = kos_operand_reduce(
      (KosOperand) KosExtra_IMM(row, 0), pair, 2);
  Py_DECREF(obj);
  if (unlikely(! result))
    RESULT_RAISE();
  RESULT_STEAL(ctx, result);
}


// The end.
