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
  MATCH_STORE: TOS popped into slot field 1 of the mark at field 0;
  when the cursor at field 2 walks a pair spine, a Datum value takes
  the slot's name from the names tuple in field 3 as its binding.
  Field 2 is 0 for a value no cursor produced, depth 0 being the value
  itself. Row depth, index, depth, const.
*/
KOS_OP_HANDLER(MATCH_STORE) {
  KOS_SYNC_PC(ctx, insn);
  const KosExtra *row = KosOp_ROW(insn);
  Py_ssize_t i = (Py_ssize_t) KosExtra_IMM(row, 1);
  uint32_t under = KosExtra_IMM(row, 2);
  (void) kos_op_delivery_;

  /* the code's validation holds the index within the names */
  PyObject *name = NULL;
  if (under) {
    PyObject *cursor = kos_match_cursor_at(ctx, under);
    if (unlikely(! cursor))
      RESULT_RAISE();
    if (KosMatchCursor_KIND(cursor) == KOS_MATCH_SPINE)
      name = PyTuple_GET_ITEM(KosExtra_OBJ(row, 3), i);
  }

  PyObject *mark = kos_match_writable_at(ctx, row, 0);
  if (unlikely(! mark))
    RESULT_RAISE();

  PyObject *value = KosContext_StackPopRef(ctx);
  if (unlikely(! value))
    RESULT_RAISE();
  if (unlikely(KosMatchMark_Store(mark, i, value, name) < 0))
    RESULT_RAISE();
  RESULT_NEXT(ctx);
}


// The end.
