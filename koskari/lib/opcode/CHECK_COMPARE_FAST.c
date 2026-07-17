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
  KosCode *code = KosEvalState_CurrentCode(es);
  KosExtra row = KosCode_GetExtra(code, KosOp_ARG(insn));
  uint16_t src = row.a0;
  PyObject *ns = CURRENT_NS();
  KOS_OP_REQUIRE_NS(ns);

  PyObject *ops[2];

  if (KosCmpSrc_X_CONST(src)) {
    ops[0] = KosEvalState_CONST(es, row.a1);   /* borrowed */
  } else {
    ops[0] = kos_ns_fast_deref(ns, (size_t) row.a1);
    if (! ops[0])
      return EVAL_RAISE();
  }

  if (KosCmpSrc_Y_CONST(src)) {
    ops[1] = KosEvalState_CONST(es, row.a2);   /* borrowed */
  } else {
    ops[1] = kos_ns_fast_deref(ns, (size_t) row.a2);
    if (! ops[1]) {
      if (! KosCmpSrc_X_CONST(src))
        Py_DECREF(ops[0]);
      return EVAL_RAISE();
    }
  }

  int res = kos_compare_chain((KosCompare) KosCmpSrc_SEL(src), ops, 2);

  if (! KosCmpSrc_X_CONST(src))
    Py_DECREF(ops[0]);
  if (! KosCmpSrc_Y_CONST(src))
    Py_DECREF(ops[1]);

  if (unlikely(res < 0))
    return EVAL_RAISE();

  if (res == 0)
    KosEvalState_SET_PC(es, row.a3);

  return EVAL_NORMAL();
}


// The end.
