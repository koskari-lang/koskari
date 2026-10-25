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
  MATCH_CLEAR: the slots the index tuple in field 1 names emptied in
  the mark at field 0. Row depth, const.
*/
KOS_OP_HANDLER(MATCH_CLEAR) {
  KOS_SYNC_PC(ctx, insn);
  const KosExtra *row = KosOp_ROW(insn);
  (void) kos_op_delivery_;

  PyObject *mark = kos_match_writable_at(ctx, row, 0);
  if (unlikely(! mark))
    RESULT_RAISE();
  if (unlikely(KosMatchMark_Clear(mark, KosExtra_OBJ(row, 1)) < 0))
    RESULT_RAISE();
  RESULT_NEXT(ctx);
}


// The end.
