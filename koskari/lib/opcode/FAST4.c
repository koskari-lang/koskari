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
  FAST4: arg = extra index. Schema FASTVAR x4. Load the four slots from the
  current fast namespace and push the values in row order. Every slot
  resolves before any push, so an empty slot raises with nothing
  pushed; a push failure releases what the handler still holds, the
  failed value included by the push itself.
*/
KOS_OP_HANDLER(FAST4) {
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

  PyObject *c = kos_ns_fast_deref(ns, (size_t) KosExtra_IMM(row, 2));
  if (unlikely(! c)) {
    Py_DECREF(a);
    Py_DECREF(b);
    RESULT_RAISE();
  }

  PyObject *d = kos_ns_fast_deref(ns, (size_t) KosExtra_IMM(row, 3));
  if (unlikely(! d)) {
    Py_DECREF(a);
    Py_DECREF(b);
    Py_DECREF(c);
    RESULT_RAISE();
  }

  PyObject *vals[4] = { a, b, c, d };
  if (unlikely(kos_stack_push_steal_n_fast(
                   KosEvalState_BLOCK(ctx), vals, 4) < 0))
    RESULT_RAISE();
  RESULT_NEXT(ctx);
}


// The end.
