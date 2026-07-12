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
   FAST_STORE_CONST: arg = extra index. Schema FASTVAR, CONST for slot and value.
   Store the value into the name in the current frame namespace.
*/

KOS_OP_HANDLER(FAST_STORE_CONST) {

    PyObject *ns = CURRENT_NS();
    if (! ns) {
      PyErr_SetString(PyExc_RuntimeError, "FAST_STORE_CONST invoked on NULL namespace");
      return EVAL_RAISE();
    }
    KosCode *code = KosEvalState_CurrentCode(es);
    KosExtra row = KosCode_GetExtra(code, KosOp_ARG(insn));

    size_t idx = (size_t) row.a0;
    PyObject *val = KosEvalState_CONST(es, row.a1);

    if (unlikely(! val))
      return EVAL_RAISE();

    if (unlikely(kos_ns_fast_store(ns, idx, val) < 0))
      return EVAL_RAISE();

    return EVAL_NORMAL();
  }


// The end.
