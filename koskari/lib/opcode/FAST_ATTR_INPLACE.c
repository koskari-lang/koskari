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
   FAST_ATTR_INPLACE: arg = EXTRA row index. Schema OPCOUNT (op-type + count),
   FASTVAR (slot), CONST (attr name). obj = fast-slot deref; the stack holds the
   count reduction values [v0, ..., v_{count-1}]. Reduce obj.attr in place with
   the operator and store it back; pushes nothing (the special appends CONST
   None). The lowered form of VAR_ATTR_INPLACE. Hot: the row decode and cache
   lookup are inlined; the op-type switch stays out of line in the shared
   kos_inplace_reduce.

   Carries two per-site cache slots. The first is the field cache: a
   single resolved storage location serves both the read of obj.attr and
   the write-back, so a hit skips the getattr/setattr protocol on both
   ends; kos_attr_inplace_cached does the read-modify-write on the
   generic path. The second is the elector: prologue only, the handler
   tail-calls the body it holds (eval/eval_electors.c), which guards
   through the field slot and reads, computes, and stores through its
   location; an empty elector slot or an ungated site takes the electing
   function.
*/

KOS_OP_HANDLER(FAST_ATTR_INPLACE) {
  KosCacheSlot *slot = KosOp_SLOT(insn);

  if (likely(slot && slot[1].kind == KOS_CACHE_ELECTOR))
    TAILCALL(slot[1].u.elector.fn, ctx, insn, NULL);
  TAILCALL(el_fast_attr_inplace_elect, ctx, insn, NULL);
}


// The end.
