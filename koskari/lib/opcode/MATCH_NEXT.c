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
  MATCH_NEXT: the next element of the cursor at field 1 pushed, the
  cursor moved in place or its copy stored back; a miss at the end.
  Row LABEL, depth.
*/
KOS_OP_HANDLER(MATCH_NEXT) {
  KOS_SYNC_PC(es, insn);
  const KosExtra *row = KosOp_ROW(insn);
  (void) kos_op_delivery_;

  uint32_t under = KosExtra_IMM(row, 1);
  PyObject *cursor = kos_match_cursor_at(es, under);
  if (unlikely(! cursor))
    RESULT_RAISE();

  PyObject *elem = NULL;
  int r = kos_match_pull(es, cursor, (size_t) under, &elem);
  if (unlikely(r < 0))
    RESULT_RAISE();
  if (! r)
    KOS_MATCH_MISS(row);
  RESULT_STEAL(es, elem);
}


// The end.
