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
   the default tag again), ASIDE_NS (the handler).

   Capture the continuation from this frame down to the prompt, whole
   frames up to and including the reset's body, and go to the prompt
   as ABORT does: the nodes the abort leaves are the continuation's
   template, kept as they come off the chain. The aside runs at the
   prompt, over a namespace built from its spec above the one live
   here, with the continuation pushed on its stack, where the
   handler's first instruction stores it. This frame stands after
   this instruction, so a call of the continuation goes on from there
   with the value it is called with.
*/

KOS_OP_HANDLER(SHIFT) {
  KOS_SYNC_PC(ctx, insn);
  const KosExtra *row = KosOp_ROW(insn);
  KosCode *aside = KosExtra_ASIDE(row, 1);
  KosDynPrompt *prompt;
  PyObject *k;
  PyObject *ns;

  k = shift_capture(ctx, (unsigned) KosExtra_IMM(row, 0), &prompt);
  if (! k)
    RESULT_RAISE();

  /* the abort takes the references to ns and k, a failed ns included */
  ns = KosNamespace_FromSpec(KosCode_NSPEC(aside), KosContext_NAMESPACE(ctx));
  RESULT_RELAY(ctx, kos_abort_to_prompt(
      ctx, prompt, aside, ns, 1, k,
      KosPyDelimitedContinuation_CAPTURED(k)));
}


// The end.
