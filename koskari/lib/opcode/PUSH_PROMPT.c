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
   PUSH_PROMPT: arg = constant index of the tag selector: 0 for the
   default prompt tag, None for a tag popped from the stack. Push a
   delimited prompt onto the dynamic chain for shift/reset. The tag is
   popped before the prompt is pushed, so it is not an operand of the
   region or of the block around it. The region's closing pc is
   resolved from the PROMPT extent here, once, and stored on the node;
   POP_SHIFT reads the answer instead of re-deriving it from a pc that
   slice execution has long since moved.
*/

KOS_OP_HANDLER(PUSH_PROMPT) {
  KOS_SYNC_PC(ctx, insn);
  PyObject *tag;
  KosDynPrompt *prompt;
  KosOp *pop;

  if (get_prompt_tag_from_insn(ctx, insn, &tag) < 0)
    RESULT_RAISE();

  /* the POP_PROMPT closing the extent, for POP_SHIFT's live reset */
  if (kos_site_extent_close(ctx, insn, KOS_EXT_PROMPT, "PUSH_PROMPT",
                            &pop) < 0) {
    Py_DECREF(tag);
    RESULT_RAISE();
  }

  /* the prompt takes its own reference */
  prompt = kos_dyn_push_prompt(ctx, tag, pop);
  Py_DECREF(tag);
  if (! prompt)
    RESULT_RAISE();
  RESULT_NEXT(ctx);
}


// The end.
