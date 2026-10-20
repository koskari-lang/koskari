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
  MATCH_COLUMNS: for each name of the tuple in field 2, the list of
  its values in the passes of the items list at field 0, stored into
  the slot at the same position in the index tuple in field 3 of the
  mark at field 1. Row depth, depth, const, const.
*/
KOS_OP_HANDLER(MATCH_COLUMNS) {
  KOS_SYNC_PC(ctx, insn);
  const KosExtra *row = KosOp_ROW(insn);
  (void) kos_op_delivery_;

  PyObject *list = kos_match_items_at(ctx, KosExtra_IMM(row, 0));
  if (unlikely(! list))
    RESULT_RAISE();
  PyObject *mark = kos_match_writable_at(ctx, row, 1);
  if (unlikely(! mark))
    RESULT_RAISE();
  if (unlikely(KosMatchMark_Columns(
          mark, list, KosExtra_OBJ(row, 2), KosExtra_OBJ(row, 3)) < 0))
    RESULT_RAISE();
  RESULT_NEXT(ctx);
}


// The end.
