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
   CAPTURE_CC: arg = 0 or 1. Capture the undelimited continuation up to
   the current CC block and push it (KosPyContinuation) on the stack.
   With arg 1 the callee on TOS is taken off the stack for the capture
   and put back under the continuation, for the CALL 1 that applies it:
   it is the call/cc form's own operand, not part of the continuation,
   and a resume that found it still on the captured stack would land
   its value on top of it. let/cc, which binds the continuation to a
   name, emits arg 0.
*/

KOS_OP_HANDLER(CAPTURE_CC) {
  KOS_SYNC_PC(ctx, insn);
  KosContinuation *k = NULL;
  PyObject *fn = NULL;

  if (KosOp_ARG(insn)) {
    fn = KosContext_StackPopRef(ctx);
    if (unlikely(! fn)) {
      PyErr_SetString(PyExc_RuntimeError, "CAPTURE_CC pop underrun");
      RESULT_RAISE();
    }
  }
  if (kos_capture_cc(ctx, &k) < 0) {
    Py_XDECREF(fn);
    RESULT_RAISE();
  }
  PyObject *wrapper = KosPyContinuation_New(k);
  if (! wrapper) {
    Py_XDECREF(fn);
    kos_continuation_free(k);
    RESULT_RAISE();
  }
  if (fn && unlikely(KosContext_StackPUSH(ctx, fn) < 0)) {
    Py_DECREF(wrapper);
    RESULT_RAISE();
  }
  RESULT_STEAL(ctx, wrapper);
}


// The end.
