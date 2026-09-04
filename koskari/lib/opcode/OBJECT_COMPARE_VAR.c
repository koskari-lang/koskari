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
   OBJECT_COMPARE_VAR: arg = extra index. Schema CMP, CONST, VAR,
   CONST. The ternary bounds idiom (const < x < const) with the middle
   operand resolved by name in the frame namespace: chain-compare the
   three operands under the selector and push the chain result.
*/

KOS_OP_HANDLER_COLD(OBJECT_COMPARE_VAR) {
  const KosExtra *row = KosOp_ROW(insn);

  PyObject *ops[3];
  ops[0] = KosExtra_OBJ(row, 1);   /* borrowed */
  ops[2] = KosExtra_OBJ(row, 3);   /* borrowed */

  PyObject *name = KosExtra_OBJ(row, 2);
  ops[1] = name ? frame_ns_deref(es, name) : NULL;
  if (! ops[1])
    RESULT_RAISE();

  PyObject *result = kos_compare_reduce((KosCompare) KosExtra_IMM(row, 0), ops, 3);
  Py_DECREF(ops[1]);

  if (unlikely(! result))
    RESULT_RAISE();

  RESULT_STEAL(es, result);
}


// The end.
