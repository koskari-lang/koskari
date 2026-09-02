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
   CHECK_COMPARE_VAR: arg = extra index. Schema CMPSRC, VAR, VAR,
   LABEL. Arity-2 fused comparison branch: load both operands per the
   CMPSRC source bits (name lookup in the frame namespace, or constant
   pool when the bit is set), compare under the selector, jump to
   LABEL on failure.
*/

KOS_OP_HANDLER_COLD(CHECK_COMPARE_VAR) {
  KosCode *code = KosEvalState_CurrentCode(es);
  KosExtra row = KosCode_GetExtra(code, KosOp_ARG(insn));
  uint16_t src = row.a0;

  PyObject *ops[2];

  if (KosCmpSrc_X_CONST(src)) {
    ops[0] = KosEvalState_CONST(es, row.a1);   /* borrowed */
  } else {
    PyObject *name = KosEvalState_CONST(es, row.a1);
    ops[0] = name ? frame_ns_deref(es, name) : NULL;
    if (! ops[0])
      RESULT_RAISE();
  }

  if (KosCmpSrc_Y_CONST(src)) {
    ops[1] = KosEvalState_CONST(es, row.a2);   /* borrowed */
  } else {
    PyObject *name = KosEvalState_CONST(es, row.a2);
    ops[1] = name ? frame_ns_deref(es, name) : NULL;
    if (! ops[1]) {
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
    KosEvalState_SET_PC(es, row.a3);

  RESULT_NEXT(es);
}


// The end.
