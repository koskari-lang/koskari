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
   PUSH_RESET: arg = 0 for the default prompt tag, or 1 for a tag
   popped from the stack, where a popped None is the default tag again.
   Cache: the extent's close.

   The body is the instructions from the next one up to its RETURN,
   compiled into this code against this namespace. Push a prompt
   carrying the tag, and above it an aside over this frame's code and
   the live namespace that starts at the body. This frame is left
   standing at the POP_RESET that closes the extent: the body's return,
   or the return of a handler that aborted to the prompt, takes the
   prompt with it and pushes its value here as any callee's return
   does, and this frame goes on from the POP_RESET.
*/

KOS_OP_HANDLER(PUSH_RESET) {
  KOS_SYNC_PC(ctx, insn);
  KosOp *body = KosContext_PC(ctx);
  KosOp *pop;
  PyObject *tag;
  KosDynPrompt *prompt;

  if (kos_site_extent_close(ctx, insn, KOS_EXT_RESET, "PUSH_RESET", &pop) < 0)
    RESULT_RAISE();

  if (pop_prompt_tag(KosContext_BLOCK(ctx), (unsigned) KosOp_ARG(insn),
                     &tag) < 0)
    RESULT_RAISE();

  /* the prompt takes its own reference; NULL is the default tag */
  prompt = kos_dyn_push_prompt(ctx, tag);
  Py_XDECREF(tag);
  if (! prompt)
    RESULT_RAISE();

  /* the position this frame keeps while the body runs: the push below
     stores it, and the body's return continues from it */
  KosContext_SET_PC(ctx, pop);

  if (! kos_dyn_push_aside(ctx, body, NULL, 0))
    RESULT_RAISE();

  RESULT_CONTINUE(ctx);
}


// The end.
