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
   the default tag again), ASIDE (the handler).

   Capture the continuation from this frame down to the prompt, whole
   frames up to and including the reset's body, then go to the prompt
   as ABORT does and run the aside there with the continuation pushed
   on its stack, where the handler's first instruction stores it. This
   frame's copy stands at the next instruction, so a call of the
   continuation goes on from there with the value it is called with.
*/

KOS_OP_HANDLER(SHIFT) {
  KOS_SYNC_PC(ctx, insn);
  const KosExtra *row = KosOp_ROW(insn);
  KosDynPrompt *prompt;
  PyObject *k = shift_capture(ctx, (unsigned) KosExtra_IMM(row, 0), &prompt);
  KosOpResult res;

  if (! k)
    RESULT_RAISE();

  res = kos_abort_to_prompt(ctx, prompt, KosExtra_ASIDE(row, 1), k);
  Py_DECREF(k);
  RESULT_RELAY(ctx, res);
}


// The end.
