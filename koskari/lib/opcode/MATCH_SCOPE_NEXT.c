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
  MATCH_SCOPE_NEXT: MATCH_SCOPE on the cursor at field 1 with field 2
  empty slots, then MATCH_NEXT on that cursor, one entry deeper under
  the mark; a miss at the end leaves the mark for the fail entry the
  NEXT's miss label names. Row LABEL, depth, count.
*/
KOS_OP_HANDLER(MATCH_SCOPE_NEXT) {
  KOS_SYNC_PC(es, insn);
  const KosExtra *row = KosOp_ROW(insn);
  (void) kos_op_delivery_;

  uint32_t under = KosExtra_IMM(row, 1);
  PyObject *cursor = kos_match_cursor_at(es, under);
  if (unlikely(! cursor))
    RESULT_RAISE();

  PyObject *mark = KosMatchMark_New(
      cursor, (Py_ssize_t) KosExtra_IMM(row, 2));
  if (unlikely(! mark))
    RESULT_RAISE();
  if (unlikely(KosEvalState_StackPUSH(es, mark) < 0))
    RESULT_RAISE();

  PyObject *elem = NULL;
  int r = kos_match_pull(es, cursor, (size_t) under + 1, &elem);
  if (unlikely(r < 0))
    RESULT_RAISE();
  if (! r)
    KOS_MATCH_MISS(row);
  RESULT_STEAL(es, elem);
}


// The end.
