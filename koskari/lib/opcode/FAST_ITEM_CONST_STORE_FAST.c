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
  FAST_ITEM_CONST_STORE_FAST: arg = extra index. Schema FASTVAR, CONST,
  FASTVAR. No stack operands. The fast slot's object[key] = another fast
  slot's value. Dispatches to the site's elected body, a list or dict form
  with the constant's index held in the slot, else elects
  (eval/eval_electors.c, the constant-key item families).
*/
KOS_OP_HANDLER(FAST_ITEM_CONST_STORE_FAST) {
  KosCacheSlot *slot = KosOp_SLOT(insn);
  if (likely(slot && slot->kind == KOS_CACHE_ELECTOR))
    TAILCALL(slot->u.elector.fn, es, insn, NULL);
  TAILCALL(el_fast_item_const_store_fast_elect, es, insn, NULL);
}


// The end.
