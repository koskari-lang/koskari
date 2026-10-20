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
   VAR_OPERAND_VAR: arg = extra index. Schema OPERAND, VAR, VAR. The
   left named variable OP the right named variable, the result pushed.
   The fast pass lowers it to FAST_OPERAND_FAST.
*/

KOS_OP_HANDLER(VAR_OPERAND_VAR) {
  KOS_SYNC_PC(ctx, insn);
  const KosExtra *row = KosOp_ROW(insn);
  PyObject *pair[2];
  PyObject *result;

  pair[0] = frame_ns_deref(ctx, KosExtra_OBJ(row, 1));
  if (unlikely(! pair[0]))
    RESULT_RAISE();
  pair[1] = frame_ns_deref(ctx, KosExtra_OBJ(row, 2));
  if (unlikely(! pair[1])) {
    Py_DECREF(pair[0]);
    RESULT_RAISE();
  }
  result = kos_operand_reduce((KosOperand) KosExtra_IMM(row, 0), pair, 2);
  Py_DECREF(pair[0]);
  Py_DECREF(pair[1]);
  if (unlikely(! result))
    RESULT_RAISE();
  RESULT_STEAL(ctx, result);
}


// The end.
