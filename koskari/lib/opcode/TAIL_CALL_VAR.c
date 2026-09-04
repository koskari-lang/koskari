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
  TAIL_CALL_VAR: arg = extra index. Schema VAR (name), COUNT (arg count).
  Call callable from namespace by name. Stack [arg_0, ..., arg_{N-1}].
  Tail call: do not push a new frame; remove or recycle the current one.
  Prologue only: kos_call_invoke owns the window drop and the frame
  handoff under KOS_CALL_TAIL.
*/
KOS_OP_HANDLER_COLD(TAIL_CALL_VAR) {
  const KosExtra *row = KosOp_ROW(insn);
  PyObject *name_obj = KosExtra_OBJ(row, 0);
  unsigned n = KosExtra_IMM(row, 1);

  PyObject *name = name_obj;
  if (unlikely(! name))
    RESULT_RAISE();

  PyObject *callable = frame_ns_deref(es, name);
  if (unlikely(! callable))
    RESULT_RAISE();

  PyObject *const *argv = NULL;
  if (n) {
    argv = KosEvalState_StackTailPtr(es, n);
    if (unlikely(! argv)) {
      Py_DECREF(callable);
      RESULT_RAISE();
    }
  }

  RESULT_RELAY(es, kos_call_invoke(
      es, callable, NULL, argv, (Py_ssize_t) n, NULL,
      KOS_CALL_ARGV_WINDOW | KOS_CALL_TAIL | KOS_CALL_CALLABLE_OWNED));
}


// The end.
