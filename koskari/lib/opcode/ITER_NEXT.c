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
  next element of the FOREACH region's iterator (kos_iter_step), the
  element pushed by the step's resume, which runs at once when the
  element is in hand and when it arrives otherwise. Branch to else_pc
  at the end of the iteration. Must be inside a PUSH_FOREACH block.
  Auto-await.
*/
KOS_OP_HANDLER(ITER_NEXT) {
  KOS_SYNC_PC(ctx, insn);
  RESULT_ENTRY(ctx, kos_iter_step(ctx, (KosOp *) insn,
                                  &kos_iter_next_resume_kind),
               &kos_iter_next_resume_kind, insn);
}


// The end.
