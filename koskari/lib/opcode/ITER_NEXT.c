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
  ITER_NEXT: the for-each step; arg = else PC. One request for the
  next element of the FOREACH region's iterator (KOS_ITER_STEP), the
  element pushed by the handler when it is in hand and by the step's
  resume when it arrives later. Branch to else_pc at the end of the
  iteration. Must be inside a PUSH_FOREACH block. Auto-await.
*/
KOS_OP_HANDLER(ITER_NEXT) {
  KOS_SYNC_PC(ctx, insn);
  KOS_ITER_STEP(ctx, insn, iter_next_push_deliver, &kos_iter_next_resume_kind,
                KosOp_TARGET((KosOp *) insn));
}


// The end.
