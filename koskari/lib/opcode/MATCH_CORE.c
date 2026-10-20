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
  MATCH_CORE: TOS, which stays, must be an instance of the core type
  whose ordinal is field 1. Row LABEL, ordinal.
*/
KOS_OP_HANDLER(MATCH_CORE) {
  KOS_SYNC_PC(es, insn);
  const KosExtra *row = KosOp_ROW(insn);
  (void) kos_op_delivery_;

  PyObject *elem = KosEvalState_StackPeek(es);
  if (unlikely(! elem))
    RESULT_RAISE();

  int r = KosMatch_CoreTypeTest(elem, KosExtra_IMM(row, 1));
  if (unlikely(r < 0))
    RESULT_RAISE();
  if (! r)
    KOS_MATCH_MISS(row);
  RESULT_NEXT(es);
}


// The end.
