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
  MATCH_SAME: TOS, which stays, must equal slot field 2 of the mark
  at field 1, and misses when that slot is empty. Row LABEL, depth,
  index.
*/
KOS_OP_HANDLER(MATCH_SAME) {
  KOS_SYNC_PC(ctx, insn);
  const KosExtra *row = KosOp_ROW(insn);
  (void) kos_op_delivery_;

  PyObject *prev = NULL;
  if (unlikely(kos_match_slot_at(
          ctx, KosExtra_IMM(row, 1), KosExtra_IMM(row, 2), &prev) < 0))
    RESULT_RAISE();

  PyObject *elem = KosContext_StackPeek(ctx);
  if (unlikely(! elem))
    RESULT_RAISE();
  if (! prev)
    KOS_MATCH_MISS(row);

  int r = KosMatch_SameTest(elem, prev);
  if (unlikely(r < 0))
    RESULT_RAISE();
  if (! r)
    KOS_MATCH_MISS(row);
  RESULT_NEXT(ctx);
}


// The end.
