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
   SHIFT_FAST: arg = extra index. Schema COUNT (0 for the default
   prompt tag, or 1 for a tag popped from the stack of the frame below,
   where a popped None is the default tag again), FASTVAR (the slot
   the continuation is bound to).

   Runs in an aside, with no block of the aside's own open. Capture
   the continuation from the frame below this one up to the prompt,
   whole frames up to and including the reset's body, store it into
   the fast slot of this frame's namespace, and move this frame onto
   the prompt as ABORT does.
*/

KOS_OP_HANDLER(SHIFT_FAST) {
  KOS_SYNC_PC(ctx, insn);
  const KosExtra *row = KosOp_ROW(insn);
  KosDynPrompt *prompt;
  PyObject *k = shift_capture(ctx, (unsigned) KosExtra_IMM(row, 0), &prompt);

  if (! k)
    RESULT_RAISE();

  if (kos_ns_fast_store_steal(KosContext_NAMESPACE(ctx),
                              (size_t) KosExtra_IMM(row, 1), k) < 0)
    RESULT_RAISE();

  RESULT_RELAY(ctx, kos_abort_to_prompt(ctx, prompt));
}


// The end.
