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
  MATCH_SYNC: the cursor at field 0 moved to the position of the
  cursor at field 1. Row depth, depth.
*/
KOS_OP_HANDLER(MATCH_SYNC) {
  KOS_SYNC_PC(ctx, insn);
  const KosExtra *row = KosOp_ROW(insn);
  (void) kos_op_delivery_;

  PyObject *cursor = kos_match_cursor_at(ctx, KosExtra_IMM(row, 0));
  if (unlikely(! cursor))
    RESULT_RAISE();
  PyObject *to = kos_match_cursor_at(ctx, KosExtra_IMM(row, 1));
  if (unlikely(! to))
    RESULT_RAISE();

  PyObject *moved = KosMatchCursor_Sync(cursor, to, 1);
  if (unlikely(! moved))
    RESULT_RAISE();
  if (unlikely(moved != cursor)
      && unlikely(KosContext_StackSetUnder(ctx, KosExtra_IMM(row, 0), moved) < 0))
    RESULT_RAISE();
  RESULT_NEXT(ctx);
}


// The end.
