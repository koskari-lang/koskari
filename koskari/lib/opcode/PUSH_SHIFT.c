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
   PUSH_SHIFT: arg = 0 (the default prompt tag) or 1 (the tag is
   popped from the stack before the capture, so it is not among the
   captured operands; a popped None is the default tag again).
   Capture the continuation
   from POP_SHIFT+1 to POP_PROMPT (via extent) when the prompt lies in
   acceptable chain heritage (kos_shift_capture locates it and offers
   handle(SHIFT) en route in one walk), then abort to that prompt: a
   transfer leaves everything above it, attaches a SHIFT node to it, and
   delivers k there at the shift block's first instruction (push_pc +
   1). The handler runs with the region gone.
*/

KOS_OP_HANDLER(PUSH_SHIFT) {
  KOS_SYNC_PC(ctx, insn);
  PyObject *tag;
  KosContinuation *k = NULL;
  KosDynPrompt *prompt = NULL;
  KosOp *resume;
  int captured;
  int aborted;

  if (get_prompt_tag_from_insn(ctx, insn, &tag) < 0)
    RESULT_RAISE();

  resume = kos_shift_ic_for((PyObject *) KosContext_FrameCode(ctx),
                            KosOp_PC(insn));
  if (! resume) {
    Py_XDECREF(tag);
    PyErr_SetString(PyExc_RuntimeError, "PUSH_SHIFT: no SHIFT extent at pc");
    RESULT_RAISE();
  }

  /* NULL is the default tag */
  captured = kos_shift_capture(ctx, tag, resume, &k, &prompt);
  Py_XDECREF(tag);
  if (captured < 0)
    RESULT_RAISE();

  PyObject *wrapper = KosPyDelimitedContinuation_New(k);
  if (! wrapper) {
    kos_continuation_free(k);
    RESULT_RAISE();
  }

  /* the shift body starts at the next instruction; the goal holds its
     own reference to k */
  aborted = kos_shift_abort(ctx, prompt, wrapper, ((KosOp *) insn) + 1);
  Py_DECREF(wrapper);
  if (aborted < 0)
    RESULT_RAISE();

  RESULT_REPROLOGUE(ctx);
}


// The end.
