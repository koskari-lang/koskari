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
  MATCH_FITS: the cursor at field 1 must have at least field 2 and
  at most field 3 elements left, KOS_MATCH_UNBOUNDED for no maximum.
  Row LABEL, depth, min, max.
*/
KOS_OP_HANDLER(MATCH_FITS) {
  const KosExtra *row = KosOp_ROW(insn);
  (void) kos_op_delivery_;

  PyObject *cursor = kos_match_cursor_at(ctx, KosExtra_IMM(row, 1));
  if (unlikely(! cursor))
    RESULT_RAISE_AT(ctx, insn);

  Py_ssize_t remaining = KosMatchCursor_Remaining(cursor);
  uint32_t max = KosExtra_IMM(row, 3);
  if (remaining < (Py_ssize_t) KosExtra_IMM(row, 2)
      || (max != KOS_MATCH_UNBOUNDED && remaining > (Py_ssize_t) max))
    KOS_MATCH_MISS(row);
  RESULT_NEXT(ctx);
}


// The end.
