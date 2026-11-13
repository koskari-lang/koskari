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
   SHIFT_VAR: arg = extra index. Schema COUNT (0 for the default
   prompt tag, or 1 for a tag popped from the stack, where a popped
   None is the default tag again), LABEL (where the shift's own frame
   goes on when the continuation is called), VAR (the name the
   continuation is bound to).

   The handler is the instructions that follow, up to a RETURN. Capture
   the continuation from the label up to the prompt, whole frames up
   to and including the reset's body, store it under the name in the
   frame namespace, and abort to the prompt as ABORT does. The
   pre-lowering twin of SHIFT_FAST, which every site with a fast slot
   takes instead.
*/

KOS_OP_HANDLER(SHIFT_VAR) {
  KOS_SYNC_PC(ctx, insn);
  const KosExtra *row = KosOp_ROW(insn);
  KosOp *entry = KosContext_PC(ctx);
  KosDynPrompt *prompt;
  PyObject *k = shift_capture(ctx, row, &prompt);
  int res;

  if (! k)
    RESULT_RAISE();

  res = frame_ns_store(ctx, KosExtra_OBJ(row, 2), k);
  Py_DECREF(k);
  if (res < 0)
    RESULT_RAISE();

  if (kos_abort_to_prompt(ctx, prompt, entry) < 0)
    RESULT_RAISE();
  RESULT_REPROLOGUE(ctx);
}


// The end.
