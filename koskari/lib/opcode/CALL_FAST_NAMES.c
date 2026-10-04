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
  CALL_FAST_NAMES and TAIL_CALL_FAST_NAMES: arg = extra index. Schema FASTVAR
  (slot), CONST (kw names), COUNT (npos). Call callable from fast local slot.
  Stack [pos_0, ..., pos_{N-1}, kwd_val_0, ..., kwd_val_{M-1}]. Prologue
  only: deref the callable (owned), take the borrowed argv window, and
  delegate to kos_call_invoke (which owns the window drop and the callable
  release). A TAIL_CALL instruction reaches this same
  handler and adds the tail bit, which the core turns into a frame reset
  instead of a push.
*/

KOS_OP_HANDLER(CALL_FAST_NAMES) {
  unsigned tail = KOS_OP_IS_TAIL_CALL(KosOp_OP(insn)) ? KOS_CALL_TAIL : 0;
  const KosExtra *row = KosOp_ROW(insn);
  uint32_t slot_idx = KosExtra_IMM(row, 0);
  Py_ssize_t npos = (Py_ssize_t) KosExtra_IMM(row, 2);

  PyObject *ns = CURRENT_NS();
  KOS_OP_REQUIRE_NS(ns);
  PyObject *callable = kos_ns_fast_deref(ns, (size_t) slot_idx);
  if (! callable)
    RESULT_RAISE();

  PyObject *names = KosExtra_OBJ(row, 1);
  if (! PyTuple_CheckExact(names)) {
    PyErr_Format(PyExc_RuntimeError, "%s names must be tuple, not %R",
                 kos_opcode_name(KosOp_OP(insn)), names);
    Py_DECREF(callable);
    RESULT_RAISE();
  }

  Py_ssize_t nkwds = PyTuple_GET_SIZE(names);
  size_t ntotal = (size_t) (npos + nkwds);

  PyObject *const *argv = NULL;
  if (ntotal) {
    argv = KosEvalState_StackTailPtr(es, ntotal);
    if (! argv) {
      Py_DECREF(callable);
      RESULT_RAISE();
    }
  }

  RESULT_RELAY(es, kos_call_invoke(
      es, callable, NULL, argv, npos, names,
      KOS_CALL_ARGV_WINDOW | tail | KOS_CALL_CALLABLE_OWNED));
}


// The end.
