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
  CALL_NAMES and TAIL_CALL_NAMES: arg = N (number of positional arguments).
  Stack: [callable, pos_0, ..., pos_{N-1}, kwd_val_0, ..., kwd_val_{M-1},
  names] with names at TOS. Prologue only: take the borrowed argv window and delegate to
  kos_call_invoke (which owns the window drop). A TAIL_CALL instruction
  reaches this same handler and adds the tail bit, which the core turns into a
  frame reset instead of a push.
*/
KOS_OP_HANDLER(CALL_NAMES) {
  KOS_SYNC_PC(es, insn);
  unsigned tail = KOS_OP_IS_TAIL_CALL(KosOp_OP(insn)) ? KOS_CALL_TAIL : 0;
  unsigned opa = KosOp_ARG(insn);

  PyObject *names = KosEvalState_StackPeek(es);
  if (! names)
    RESULT_RAISE();
  if (! PyTuple_CheckExact(names)) {
    PyErr_Format(PyExc_RuntimeError, "%s names must be tuple, not %R",
                 kos_opcode_name(KosOp_OP(insn)), names);
    RESULT_RAISE();
  }

  Py_ssize_t npos = (Py_ssize_t) opa;
  Py_ssize_t nkwds = PyTuple_GET_SIZE(names);
  Py_ssize_t total = npos + nkwds;

  /* Pop the names tuple off the top; the remaining window is the
     standard [callable, pos..., kw_vals...] shape for the core. */
  names = KosEvalState_StackPopRef(es);
  if (! names)
    RESULT_RAISE();

  PyObject *const *ptr = KosEvalState_StackTailPtr(es, (size_t) total + 1);
  if (! ptr) {
    Py_DECREF(names);
    RESULT_RAISE();
  }

  KosOpResult r = kos_call_invoke_from_handler(
      es, tail, ptr[0], NULL, total ? ptr + 1 : NULL, npos,
      nkwds ? names : NULL,
      KOS_CALL_ARGV_WINDOW | KOS_CALL_WINDOW_CALLABLE);
  Py_DECREF(names);
  RESULT_RELAY(es, r);
}


// The end.
