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
  MATCH_SPAN: the span from the position of the mark at field 1 to
  the cursor at field 2 pushed; an empty span jumps to field 0,
  pushing nothing. Row LABEL, depth, depth.
*/
KOS_OP_HANDLER(MATCH_SPAN) {
  KOS_SYNC_PC(es, insn);
  const KosExtra *row = KosOp_ROW(insn);
  (void) kos_op_delivery_;

  PyObject *mark = kos_match_mark_at(es, KosExtra_IMM(row, 1));
  if (unlikely(! mark))
    RESULT_RAISE();
  PyObject *cursor = kos_match_cursor_at(es, KosExtra_IMM(row, 2));
  if (unlikely(! cursor))
    RESULT_RAISE();

  if (KosMatchCursor_INDEX(cursor) == KosMatchMark_INDEX(mark))
    KOS_MATCH_MISS(row);
  RESULT_STEAL(es, KosMatchMark_Span(mark, cursor));
}


// The end.
