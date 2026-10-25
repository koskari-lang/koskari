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
   OBJECT_OPERAND_FAST: arg = extra index. Schema OPERAND, FASTVAR.
   The top of the stack OP the fast slot's value, the result replacing
   the top. Dispatches to the site's elected body, else elects.
*/

KOS_OP_HANDLER(OBJECT_OPERAND_FAST) {
  KosCacheSlot *slot = KosOp_SLOT(insn);
  if (likely(slot && slot->kind == KOS_CACHE_ELECTOR))
    TAILCALL(slot->u.elector.fn, ctx, insn, NULL);
  TAILCALL(el_operand_fast_elect, ctx, insn, NULL);
}


// The end.
