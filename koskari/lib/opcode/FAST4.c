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
  FAST4: arg = extra index. Schema FASTVAR x4. Push four values from
  fast local slots in row order. All slots resolve before any push, so
  an unbound name raises with nothing pushed.
*/
KOS_OP_HANDLER(FAST4) {
  KosCode *code = KosEvalState_CurrentCode(es);
  KosExtra row = KosCode_GetExtra(code, KosOp_ARG(insn));
  PyObject *ns = CURRENT_NS();
  KOS_OP_REQUIRE_NS(ns);

  uint16_t idx[4] = { row.a0, row.a1, row.a2, row.a3 };
  PyObject *vals[4];

  for (size_t i = 0; i < 4; i++) {
    vals[i] = kos_ns_fast_deref(ns, (size_t) idx[i]);
    if (unlikely(! vals[i])) {
      while (i)
        Py_DECREF(vals[--i]);
      return EVAL_RAISE();
    }
  }

  for (size_t i = 0; i < 4; i++) {
    if (unlikely(KosEvalState_StackPUSH(es, vals[i]) < 0)) {
      /* PUSH consumes vals[i] even on failure; free the rest */
      while (++i < 4)
        Py_DECREF(vals[i]);
      return EVAL_RAISE();
    }
  }

  return EVAL_NORMAL();
}


// The end.
