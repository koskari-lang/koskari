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
  FAST_NEXT: fused FAST + OBJECT_NEXT. Arg = extra index. Schema
  FASTVAR (iterator slot), COUNT (n = 1 or 2). Stack: [] or [default]
  per n. Load the iterator from its fast slot and run the shared step
  core (object_next_step): value or default on exhaustion, 1-arg
  raises StopIteration, auto-await.
*/
KOS_OP_HANDLER(FAST_NEXT) {
  const KosExtra *row = KosOp_ROW(insn);
  uint16_t n = (uint16_t) KosExtra_IMM(row, 1);
  if (unlikely(n != 1 && n != 2)) {
    PyErr_Format(PyExc_ValueError, "FAST_NEXT arg must be 1 or 2, got %u", n);
    RESULT_RAISE();
  }

  PyObject *ns = CURRENT_NS();
  KOS_OP_REQUIRE_NS(ns);

  PyObject *default_val = NULL;
  if (n == 2) {
    default_val = KosEvalState_StackPopRef(es);
    if (unlikely(! default_val))
      RESULT_RAISE();
  }

  PyObject *iter = kos_ns_fast_deref(ns, (size_t) KosExtra_IMM(row, 0));
  if (unlikely(! iter)) {
    Py_XDECREF(default_val);
    RESULT_RAISE();
  }

  RESULT_RELAY(es, object_next_step(es, iter, default_val, n));
}


// The end.
