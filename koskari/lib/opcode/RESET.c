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
   the default tag again), LABEL (where this frame goes on once the
   body has returned).

   The body is the instructions from the next one up to its RETURN,
   compiled into this code against this namespace. Push a prompt
   carrying the tag, and above it a frame over this frame's code and
   the live namespace that starts at the body. The body's return, or
   the return of a shift's handler called above the same prompt, takes
   the prompt with it and is this instruction's value, as any callee's
   return is; this frame goes on at the label with that value pushed.
*/

KOS_OP_HANDLER(RESET) {
  KOS_SYNC_PC(ctx, insn);
  const KosExtra *row = KosOp_ROW(insn);
  KosOp *body = KosContext_PC(ctx);
  PyObject *tag;
  KosDynPrompt *prompt;

  if (pop_prompt_tag(ctx, (unsigned) KosExtra_IMM(row, 0), &tag) < 0)
    RESULT_RAISE();

  /* the prompt takes its own reference; NULL is the default tag */
  prompt = kos_dyn_push_prompt(ctx, tag);
  Py_XDECREF(tag);
  if (! prompt)
    RESULT_RAISE();

  /* the position this frame keeps while the body runs: the push below
     stores it, and the body's return continues from it */
  KosContext_SET_PC(ctx, KosExtra_TARGET(row, 1));

  if (! kos_dyn_push_aside(ctx, body))
    RESULT_RAISE();

  RESULT_CONTINUE(ctx);
}


// The end.
