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
   SET_PROMPT_HANDLER: arg = PC (label) of handler. If dyn_top is a prompt,
   set its handler code and PC so SHIFT will jump there with the continuation.
*/

KOS_OP_HANDLER(SET_PROMPT_HANDLER) {
  if (KosContext_DYN_TOP(ctx) && KosDynNode_IS_PROMPT(KosContext_DYN_TOP(ctx))) {
    KosDynPrompt *p = (KosDynPrompt *) KosContext_DYN_TOP(ctx);
    KosDynPrompt_HANDLER_CODE(p) = CURRENT_CODE(ctx);
    KosDynPrompt_HANDLER_PC(p) = KosOp_ARG(insn);
  }
  (void) out_ev;
  return 0;
}
