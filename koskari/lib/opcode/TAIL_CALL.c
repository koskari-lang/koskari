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
   TAIL_CALL: arg = N. Same stack as CALL [callable, arg_0, ..., arg_{N-1}].
   Tail call: do not push a new frame; remove or recycle the current one.

   Extracts args via KosStackSteal before call; KosEvalState_Call handles frame
   behavior and dispatch. Returns EVAL_CONTROL_RETURNED() when we push and
   continue (triggers kos_dynnode_on_control_returned for wind thunks).
*/

KOS_OP_HANDLER(TAIL_CALL) {
  unsigned n = KosOp_ARG(insn);

  PyObject *const *ptr = KosEvalState_StackTailPtr(es, n + 1);
  if (! ptr)
    RESULT_RAISE();

  /* Prologue only: the whole window (callable included) is borrowed;
     the core's tail subpath binds before the frame reset drops it. */
  return kos_call_invoke(es, ptr[0], NULL, n ? ptr + 1 : NULL,
                         (Py_ssize_t) n, NULL,
                         KOS_CALL_ARGV_WINDOW | KOS_CALL_WINDOW_CALLABLE
                         | KOS_CALL_TAIL);
}


// The end.
