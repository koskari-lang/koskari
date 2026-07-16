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
  CALL_FAST: arg = extra index. Schema FASTVAR (slot), COUNT (arg count).
  Call callable from fast local slot. Stack [arg_0, ..., arg_{N-1}].
  Prologue only: deref the callable, take the borrowed argv window, and
  delegate to kos_call_invoke (which owns the window drop).
*/

KOS_OP_HANDLER(CALL_FAST) {
  KosCode *code = KosEvalState_CurrentCode(es);
  KosExtra row = KosCode_GetExtra(code, KosOp_ARG(insn));
  unsigned n = row.a1;

  PyObject *ns = CURRENT_NS();
  KOS_OP_REQUIRE_NS(ns);
  PyObject *callable = kos_ns_fast_deref(ns, (size_t) row.a0);
  if (! callable)
    return EVAL_RAISE();

  PyObject *const *argv = NULL;
  if (n) {
    argv = KosEvalState_StackTailPtr(es, n);
    if (unlikely(! argv)) {
      Py_DECREF(callable);
      return EVAL_RAISE();
    }
  }

  KosOpResult r = kos_call_invoke(es, callable, NULL, argv, (Py_ssize_t) n,
                                  NULL, KOS_CALL_ARGV_WINDOW);
  Py_DECREF(callable);
  return r;
}


// The end.
