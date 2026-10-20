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
  MATCH_TAIL: the remaining elements of the cursor at field 1 less
  field 2 pushed as a span, the cursor advanced past them; a miss
  when fewer than field 2 remain or the span is shorter than field 3.
  Row LABEL, depth, width, min.
*/
KOS_OP_HANDLER(MATCH_TAIL) {
  KOS_SYNC_PC(ctx, insn);
  const KosExtra *row = KosOp_ROW(insn);
  (void) kos_op_delivery_;

  PyObject *cursor = kos_match_cursor_at(ctx, KosExtra_IMM(row, 1));
  if (unlikely(! cursor))
    RESULT_RAISE();

  PyObject *span = NULL;
  PyObject *next = NULL;
  int r = KosMatchCursor_Tail(
      cursor, (Py_ssize_t) KosExtra_IMM(row, 2),
      (Py_ssize_t) KosExtra_IMM(row, 3), 1, &span, &next);
  if (unlikely(r < 0))
    RESULT_RAISE();
  if (! r)
    KOS_MATCH_MISS(row);

  if (unlikely(next != cursor)
      && unlikely(KosEvalState_StackSetUnder(ctx, KosExtra_IMM(row, 1), next) < 0)) {
    Py_DECREF(span);
    RESULT_RAISE();
  }
  RESULT_STEAL(ctx, span);
}


// The end.
