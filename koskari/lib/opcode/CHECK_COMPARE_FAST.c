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
  CHECK_COMPARE_FAST: arg = extra index. Schema CMPSRC, FASTVAR,
  FASTVAR, LABEL. Arity-2 fused comparison branch: load both operands
  per the CMPSRC source bits (fast slot, or constant pool when the bit
  is set), compare under the selector, jump to LABEL on failure.
*/
KOS_OP_HANDLER(CHECK_COMPARE_FAST) {
  const KosExtra *row = KosOp_ROW(insn);
  uint16_t src = KosExtra_IMM(row, 0);
  PyObject *ns = CURRENT_NS();
  KOS_OP_REQUIRE_NS(ns);

  /* Exact-int pair under a richcompare selector: settle on the
     extracted values with no refcount traffic, peeking the slots
     borrowed. Nothing runs between the peek and the compare that could
     rebind a slot, so the borrow is sound; any other shape takes the
     owned path below. */
  if (likely(KosCmpSrc_SEL(src) <= KOS_COMPARE_GE)) {
    PyObject *x = KosCmpSrc_X_CONST(src)
        ? KosExtra_OBJ(row, 1)
        : kos_ns_fast_peek(ns, (size_t) KosExtra_IMM(row, 1));
    PyObject *y = KosCmpSrc_Y_CONST(src)
        ? KosExtra_OBJ(row, 2)
        : kos_ns_fast_peek(ns, (size_t) KosExtra_IMM(row, 2));
    long long a, b;

    if (x && y && kos_as_ll2(x, y, &a, &b)) {
      if (kos_ll_compare((KosCompare) KosCmpSrc_SEL(src), a, b))
        RESULT_NEXT(es);
      RESULT_JUMP_AT(es, KosExtra_TARGET(row, 3));
    }
  }

  PyObject *ops[2];

  if (KosCmpSrc_X_CONST(src)) {
    ops[0] = KosExtra_OBJ(row, 1);   /* borrowed */

  } else {
    ops[0] = kos_ns_fast_deref(ns, (size_t) KosExtra_IMM(row, 1));
    if (unlikely(! ops[0]))
      RESULT_RAISE();
  }

  if (KosCmpSrc_Y_CONST(src)) {
    ops[1] = KosExtra_OBJ(row, 2);   /* borrowed */

  } else {
    ops[1] = kos_ns_fast_deref(ns, (size_t) KosExtra_IMM(row, 2));
    if (unlikely(! ops[1])) {
      if (! KosCmpSrc_X_CONST(src))
        Py_DECREF(ops[0]);
      RESULT_RAISE();
    }
  }

  int res = kos_compare_chain((KosCompare) KosCmpSrc_SEL(src), ops, 2);

  if (! KosCmpSrc_X_CONST(src))
    Py_DECREF(ops[0]);

  if (! KosCmpSrc_Y_CONST(src))
    Py_DECREF(ops[1]);

  if (unlikely(res < 0))
    RESULT_RAISE();

  if (res == 0)
    RESULT_JUMP_AT(es, KosExtra_TARGET(row, 3));

  RESULT_NEXT(es);
}


// The end.
