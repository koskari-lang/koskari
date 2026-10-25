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
  CALL and TAIL_CALL: arg = number of positional arguments N. Stack has
  [callable, arg_0, ..., arg_{N-1}] (callable oldest). Prologue only: the whole
  window (callable included) is borrowed; kos_call_invoke owns the drop. A
  TAIL_CALL instruction reaches this same handler and adds the tail bit, which
  the core turns into a frame reset instead of a push; its return settles a
  pending wind thunk.
*/
KOS_OP_HANDLER(CALL) {
  KOS_SYNC_PC(ctx, insn);
  unsigned n = KosOp_ARG(insn);
  unsigned tail = KOS_OP_IS_TAIL_CALL(KosOp_OP(insn)) ? KOS_CALL_TAIL : 0;

  PyObject *const *ptr = KosContext_StackTailPtr(ctx, n + 1);
  if (unlikely(! ptr))
    RESULT_RAISE();

  SAYF("CALL: callable=%R", ptr[0]);

  RESULT_RELAY(ctx, kos_call_invoke_from_handler(
      ctx, tail, ptr[0], NULL, n ? ptr + 1 : NULL, (Py_ssize_t) n, NULL,
      KOS_CALL_ARGV_WINDOW | KOS_CALL_WINDOW_CALLABLE));
}


// The end.
