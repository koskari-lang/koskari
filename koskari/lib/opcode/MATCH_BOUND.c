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
  MATCH_BOUND: a cursor at the position of the cursor at field 1,
  ending field 2 elements before its end, pushed; a miss when fewer
  remain. Row LABEL, depth, width.
*/
KOS_OP_HANDLER(MATCH_BOUND) {
  KOS_SYNC_PC(es, insn);
  const KosExtra *row = KosOp_ROW(insn);
  (void) kos_op_delivery_;

  PyObject *cursor = kos_match_cursor_at(es, KosExtra_IMM(row, 1));
  if (unlikely(! cursor))
    RESULT_RAISE();

  PyObject *bound = NULL;
  int r = KosMatchCursor_Bound(
      cursor, (Py_ssize_t) KosExtra_IMM(row, 2), &bound);
  if (unlikely(r < 0))
    RESULT_RAISE();
  if (! r)
    KOS_MATCH_MISS(row);
  RESULT_STEAL(es, bound);
}


// The end.
