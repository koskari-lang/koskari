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
   FAST_CONST_ITEM: arg = extra index. Schema FASTVAR (obj slot),
   CONST (key). Push obj[key] with the object read from a fast slot;
   nothing is popped.
   Prologue only: tail-call the body the site's slot elected
   (eval/eval_electors.c), which carries its own guard; an empty slot
   or an ungated site takes the electing function, which classifies
   the container once and settles the slot. One cache slot:
   KOS_CACHE_ELECTOR.
*/

KOS_OP_HANDLER(FAST_CONST_ITEM) {
  KosCacheSlot *slot = KosOp_SLOT(insn);

  if (likely(slot && slot->kind == KOS_CACHE_ELECTOR))
    TAILCALL(slot->u.elector.fn, es, insn, NULL);
  TAILCALL(el_fast_const_item_elect, es, insn, NULL);
}


// The end.
