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
   ITEM_INPLACE: arg = OPCOUNT (operator selector in the high byte,
   reduction count in the low byte). Stack [obj, key, v0 .. v_{count-1}];
   obj[key] OP= each value left to right through kos_item_inplace_reduce,
   then every entry is dropped; pushes nothing (the special appends CONST
   None). Prologue only: tail-call the body the site's slot elected
   (eval/eval_electors.c), which carries its own guard; an empty slot or
   an ungated site takes the electing function, which classifies the
   item and operand once and settles the slot. One cache slot:
   KOS_CACHE_ELECTOR.
*/

KOS_OP_HANDLER(ITEM_INPLACE) {
  KosCacheSlot *slot = KosOp_SLOT(insn);

  if (likely(slot && slot->kind == KOS_CACHE_ELECTOR))
    TAILCALL(slot->u.elector.fn, es, insn, NULL);
  TAILCALL(el_item_inplace_elect, es, insn, NULL);
}


// The end.
