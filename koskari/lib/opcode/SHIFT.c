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
   SHIFT: arg = extra index. Schema COUNT (0 for the default prompt
   tag, or 1 for a tag popped from the stack, where a popped None is
   the default tag again), CONST (the handler's FunctionSpec, a
   function of one parameter, the continuation).

   Build the handler over the live namespace, capture the continuation
   from the next instruction up to the prompt, whole frames up to and
   including the reset's body, and abort to the prompt: the transfer
   leaves everything above it, and calls the handler with the
   continuation above the prompt. The handler's return ends the prompt
   and is the reset's return.
*/

KOS_OP_HANDLER(SHIFT) {
  KOS_SYNC_PC(ctx, insn);
  const KosExtra *row = KosOp_ROW(insn);
  PyObject *tag;
  PyObject *handler;
  PyObject *wrapper;
  KosContinuation *k = NULL;
  KosDynPrompt *prompt = NULL;
  int failed;

  if (pop_prompt_tag(ctx, (unsigned) KosExtra_IMM(row, 0), &tag) < 0)
    RESULT_RAISE();

  handler = kos_eval_build_function(KosExtra_OBJ(row, 1), NULL, NULL,
                                    CURRENT_NS());
  if (! handler) {
    Py_XDECREF(tag);
    RESULT_RAISE();
  }

  failed = kos_shift_capture(ctx, tag, KosContext_PC(ctx), &k, &prompt);
  Py_XDECREF(tag);
  if (failed < 0) {
    Py_DECREF(handler);
    RESULT_RAISE();
  }

  wrapper = KosPyDelimitedContinuation_New(k);
  if (! wrapper) {
    kos_continuation_free(k);
    Py_DECREF(handler);
    RESULT_RAISE();
  }

  failed = kos_shift_abort(ctx, prompt, handler, wrapper);
  Py_DECREF(handler);
  Py_DECREF(wrapper);
  if (failed < 0)
    RESULT_RAISE();
  RESULT_REPROLOGUE(ctx);
}


// The end.
