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
   FAST2: arg = extra index. Schema FASTVAR, FASTVAR. Push two values
   from fast local slots in row order. Both slots resolve before either
   push, so an unbound name raises with nothing pushed.
*/

KOS_OP_HANDLER(FAST2) {
  KosCode *code = KosEvalState_CurrentCode(es);
  KosExtra row = KosCode_GetExtra(code, KosOp_ARG(insn));
  PyObject *ns = CURRENT_NS();
  KOS_OP_REQUIRE_NS(ns);

  PyObject *a = kos_ns_fast_deref(ns, (size_t) row.a0);
  if (! a)
    return EVAL_RAISE();

  PyObject *b = kos_ns_fast_deref(ns, (size_t) row.a1);
  if (! b) {
    Py_DECREF(a);
    return EVAL_RAISE();
  }

  if (KosEvalState_StackPUSH(es, a) < 0) {
    Py_DECREF(b);
    return EVAL_RAISE();
  }
  return kos_op_result_PUSH(es, b);
}


// The end.
