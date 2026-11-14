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
   ABORT: arg = extra index. Schema COUNT (0 for the default prompt
   tag, or 1 for a tag popped from the stack, where a popped None is
   the default tag again), ASIDE (the handler).

   Find the prompt, leave everything between this frame and it, this
   frame included, and run the aside in a frame above the prompt.
   Nothing is captured. The handler's return ends the prompt and is
   the reset's return.
*/

KOS_OP_HANDLER(ABORT) {
  KOS_SYNC_PC(ctx, insn);
  const KosExtra *row = KosOp_ROW(insn);
  PyObject *tag;
  KosDynPrompt *prompt;

  if (pop_prompt_tag(KosContext_BLOCK(ctx), (unsigned) KosExtra_IMM(row, 0),
                     &tag) < 0)
    RESULT_RAISE();

  prompt = kos_abort_prompt(ctx, tag);
  Py_XDECREF(tag);
  if (! prompt)
    RESULT_RAISE();

  RESULT_RELAY(ctx, kos_abort_to_prompt(ctx, prompt, KosExtra_ASIDE(row, 1),
                                        NULL));
}


// The end.
