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
   FAST_INPLACE_CONST: arg = EXTRA row index. Row a0 = bare
   KosOperand selector, a1 = fast slot, a2 = constant-pool index of the
   operand. Augmented assignment with a constant rhs, completely
   stack-free: obj = deref(slot); obj OP= const; store back. The
   compressed form of CONST + FAST_INPLACE (count 1) -- e.g.
   (incr! i).
*/

KOS_OP_HANDLER(FAST_INPLACE_CONST) {
  const KosExtra *row = KosOp_ROW(insn);
  PyObject *ns = CURRENT_NS();
  KOS_OP_REQUIRE_NS(ns);

  PyObject *operand = KosExtra_OBJ(row, 2);   /* borrowed */

  size_t idx = (size_t) KosExtra_IMM(row, 1);
  PyObject *obj = kos_ns_fast_deref(ns, idx);
  if (unlikely(! obj))
    RESULT_RAISE();

  PyObject *pair[2] = { obj, operand };
  PyObject *result = kos_inplace_reduce((KosOperand) KosExtra_IMM(row, 0), pair, 2);
  Py_DECREF(obj);
  if (unlikely(! result))
    RESULT_RAISE();

  if (unlikely(kos_ns_fast_store_steal(ns, idx, result) < 0))
    RESULT_RAISE();
  RESULT_NEXT(es);
}


// The end.
