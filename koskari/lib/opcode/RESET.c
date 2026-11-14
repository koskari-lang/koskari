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
   RESET: arg = extra index. Schema COUNT (0 for the default prompt
   tag, or 1 for a tag popped from the stack, where a popped None is
   the default tag again), ASIDE (the body).

   Push a prompt carrying the tag, and above it a frame that runs the
   aside. This frame stands at the next instruction: the body's
   return, or the return of a handler that aborted to the prompt,
   takes the prompt with it and pushes its value here as any callee's
   return does.
*/

KOS_OP_HANDLER(RESET) {
  KOS_SYNC_PC(ctx, insn);
  const KosExtra *row = KosOp_ROW(insn);
  PyObject *tag;
  KosDynPrompt *prompt;

  if (pop_prompt_tag(KosContext_BLOCK(ctx), (unsigned) KosExtra_IMM(row, 0),
                     &tag) < 0)
    RESULT_RAISE();

  /* the prompt takes its own reference; NULL is the default tag */
  prompt = kos_dyn_push_prompt(ctx, tag);
  Py_XDECREF(tag);
  if (! prompt)
    RESULT_RAISE();

  if (! kos_dyn_push_aside(ctx, KosExtra_ASIDE(row, 1)))
    RESULT_RAISE();

  RESULT_CONTINUE(ctx);
}


// The end.
