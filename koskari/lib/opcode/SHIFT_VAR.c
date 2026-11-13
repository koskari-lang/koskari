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
   prompt tag, or 1 for a tag popped from the stack of the frame below,
   where a popped None is the default tag again), VAR (the name the
   continuation is bound to).

   Runs in an aside, with no block of the aside's own open. Capture
   the continuation from the frame below this one up to the prompt,
   whole frames up to and including the reset's body, store it under
   the name in this frame's namespace, and move this frame onto the
   prompt as ABORT does. The frame below stands at the POP_ASIDE that
   closes this aside, so a call of the continuation goes on from there.
   The pre-lowering twin of SHIFT_FAST, which every site with a fast
   slot takes instead.
*/

KOS_OP_HANDLER(SHIFT_VAR) {
  KOS_SYNC_PC(ctx, insn);
  const KosExtra *row = KosOp_ROW(insn);
  KosDynPrompt *prompt;
  PyObject *k = shift_capture(ctx, (unsigned) KosExtra_IMM(row, 0), &prompt);
  int res;

  if (! k)
    RESULT_RAISE();

  res = frame_ns_store(ctx, KosExtra_OBJ(row, 1), k);
  Py_DECREF(k);
  if (res < 0)
    RESULT_RAISE();

  RESULT_RELAY(ctx, kos_abort_to_prompt(ctx, prompt));
}


// The end.
