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
  MATCH_SCOPE: a mark at the position of the cursor at field 0 with
  field 1 empty slots pushed. Row depth, count.
*/
KOS_OP_HANDLER(MATCH_SCOPE) {
  KOS_SYNC_PC(es, insn);
  const KosExtra *row = KosOp_ROW(insn);
  (void) kos_op_delivery_;

  PyObject *cursor = kos_match_cursor_at(es, KosExtra_IMM(row, 0));
  if (unlikely(! cursor))
    RESULT_RAISE();
  RESULT_STEAL(
      es, KosMatchMark_New(cursor, (Py_ssize_t) KosExtra_IMM(row, 1)));
}


// The end.
