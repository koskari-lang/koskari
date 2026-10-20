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
  CALL_VAR and TAIL_CALL_VAR: arg = extra index. Schema VAR (name), COUNT (arg
  count). Call callable from namespace by name. Stack [arg_0, ..., arg_{N-1}].
  Prologue only: deref the callable (owned), take the borrowed argv window,
  and delegate to kos_call_invoke (which owns the window drop and the
  callable release). A TAIL_CALL instruction reaches this same
  handler and adds the tail bit, which the core turns into a frame reset
  instead of a push.
*/
KOS_OP_HANDLER(CALL_VAR) {
  KOS_SYNC_PC(ctx, insn);
  unsigned tail = KOS_OP_IS_TAIL_CALL(KosOp_OP(insn)) ? KOS_CALL_TAIL : 0;
  const KosExtra *row = KosOp_ROW(insn);
  unsigned n = KosExtra_IMM(row, 1);

  PyObject *name = KosExtra_OBJ(row, 0);

  PyObject *callable = frame_ns_deref(ctx, name);
  SAYF("CALL_VAR: callable=%R", callable);
  if (unlikely(! callable))
    RESULT_RAISE();

  PyObject *const *argv = NULL;
  if (n) {
    argv = KosEvalState_StackTailPtr(ctx, n);
    if (unlikely(! argv)) {
      Py_DECREF(callable);
      RESULT_RAISE();
    }
  }

  RESULT_RELAY(ctx, kos_call_invoke_from_handler(
      ctx, tail, callable, NULL, argv, (Py_ssize_t) n, NULL,
      KOS_CALL_ARGV_WINDOW | KOS_CALL_CALLABLE_OWNED));
}


// The end.
