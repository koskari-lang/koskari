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
   OBJECT_OPERAND: arg = OPCOUNT (operator selector in the high byte, operand
   count in the low byte). Pop the count operands and apply the operator,
   pushing the result. Hot: the hottest opcode by both dispatch count and
   total time. Prologue only: tail-call the body the site's slot elected
   (eval/eval_electors.c), which carries its own guard; an empty slot or
   an ungated site takes the electing function, which runs the ladder
   once and settles the slot. One cache slot: KOS_CACHE_ELECTOR.
*/

KOS_OP_HANDLER(OBJECT_OPERAND) {
  KosCacheSlot *slot = KosOp_SLOT(insn);

  if (likely(slot && slot->kind == KOS_CACHE_ELECTOR))
    TAILCALL(slot->u.elector.fn, ctx, insn, NULL);
  TAILCALL(el_operand_elect, ctx, insn, NULL);
}


// The end.
