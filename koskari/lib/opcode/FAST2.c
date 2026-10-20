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
  KOS_SYNC_PC(ctx, insn);
  const KosExtra *row = KosOp_ROW(insn);
  PyObject *ns = CURRENT_NS();
  KOS_OP_REQUIRE_NS(ns);

  PyObject *a = kos_ns_fast_deref(ns, (size_t) KosExtra_IMM(row, 0));
  if (unlikely(! a))
    RESULT_RAISE();

  PyObject *b = kos_ns_fast_deref(ns, (size_t) KosExtra_IMM(row, 1));
  if (unlikely(! b)) {
    Py_DECREF(a);
    RESULT_RAISE();
  }

  PyObject *vals[2] = { a, b };
  if (unlikely(kos_stack_push_steal_n_fast(
                   KosEvalState_BLOCK(ctx), vals, 2) < 0))
    RESULT_RAISE();
  RESULT_NEXT(ctx);
}


// The end.
