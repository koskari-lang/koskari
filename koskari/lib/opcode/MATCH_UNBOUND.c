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
  MATCH_UNBOUND: a cursor at the position of the cursor at field 0
  with no reserve, ending at its subject's end, pushed. Row depth.
*/
KOS_OP_HANDLER(MATCH_UNBOUND) {
  KOS_SYNC_PC(ctx, insn);
  const KosExtra *row = KosOp_ROW(insn);
  (void) kos_op_delivery_;

  PyObject *cursor = kos_match_cursor_at(ctx, KosExtra_IMM(row, 0));
  if (unlikely(! cursor))
    RESULT_RAISE();
  RESULT_STEAL(ctx, KosMatchCursor_Unbound(cursor));
}


// The end.
