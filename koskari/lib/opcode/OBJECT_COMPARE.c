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
  OBJECT_COMPARE: arg = CMPCOUNT (KosCompare selector high byte, reduce
  count low byte). Chain-compare the top COUNT operands under the
  selector and push the result. Prologue only: tail-call the body the
  site's slot elected (eval/eval_electors.c); an empty slot or an
  ungated site takes the electing function. One cache slot:
  KOS_CACHE_ELECTOR.
*/
KOS_OP_HANDLER(OBJECT_COMPARE) {
  KosCacheSlot *slot = KosOp_SLOT(insn);

  if (likely(slot && slot->kind == KOS_CACHE_ELECTOR))
    TAILCALL(slot->u.elector.fn, ctx, insn, NULL);
  TAILCALL(el_object_compare_elect, ctx, insn, NULL);
}


// The end.
