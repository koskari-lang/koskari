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
   VAR_INPLACE_CONST: arg = EXTRA row index. Row a0 = bare
   KosOperand selector, a1 = constant-pool index of the variable name,
   a2 = constant-pool index of the operand. Augmented assignment with a
   constant rhs through the frame namespace, completely stack-free:
   obj = deref(name); obj OP= const; store back. The compressed form of
   CONST + VAR_INPLACE (count 1).
*/

KOS_OP_HANDLER(VAR_INPLACE_CONST) {
  KOS_SYNC_PC(ctx, insn);
  const KosExtra *row = KosOp_ROW(insn);

  PyObject *name = KosExtra_OBJ(row, 1);

  PyObject *operand = KosExtra_OBJ(row, 2);   /* borrowed */

  PyObject *obj = frame_ns_deref(ctx, name);
  if (unlikely(! obj))
    RESULT_RAISE();

  PyObject *pair[2] = { obj, operand };
  PyObject *result = kos_inplace_reduce((KosOperand) KosExtra_IMM(row, 0), pair, 2);
  Py_DECREF(obj);
  if (unlikely(! result))
    RESULT_RAISE();

  int res = frame_ns_store(ctx, name, result);
  Py_DECREF(result);
  if (unlikely(res < 0))
    RESULT_RAISE();
  RESULT_NEXT(ctx);
}


// The end.
