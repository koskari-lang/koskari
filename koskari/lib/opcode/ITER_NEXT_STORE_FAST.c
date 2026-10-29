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
  ITER_NEXT_STORE_FAST: arg = extra index. Schema FASTVAR (slot), LABEL
  (else pc). The for-each step with its element store: one request for
  the next element of the FOREACH region's iterator (KOS_ITER_STEP),
  the element stored into the fast slot by the handler when it is in
  hand and by the step's resume when it arrives later. Branch to the
  else pc at the end of the iteration. Must be inside a PUSH_FOREACH
  block. Auto-await.
*/
KOS_OP_HANDLER(ITER_NEXT_STORE_FAST) {
  KOS_SYNC_PC(ctx, insn);
  KOS_ITER_STEP(ctx, insn, iter_next_store_fast_deliver,
                &kos_iter_next_store_fast_resume_kind,
                KosExtra_TARGET(KosOp_ROW((KosOp *) insn), 1));
}


// The end.
