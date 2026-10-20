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
  MATCH_DESCEND: the sequence gate on TOS read through Datum; the
  cursor over it replaces it. A miss pops TOS and jumps. Simple LABEL
  style.
*/
KOS_OP_HANDLER(MATCH_DESCEND) {
  KOS_SYNC_PC(ctx, insn);
  (void) kos_op_delivery_;

  PyObject *value = KosContext_StackPeek(ctx);
  if (unlikely(! value))
    RESULT_RAISE();

  PyObject *cursor = NULL;
  int r = KosMatchCursor_Descend(value, &cursor);
  if (unlikely(r < 0))
    RESULT_RAISE();

  if (! r) {
    if (unlikely(KosContext_StackDrop1(ctx) < 0))
      RESULT_RAISE();
    RESULT_JUMP_AT(ctx, KosOp_TARGET(insn));
  }
  if (unlikely(KosContext_StackSetUnder(ctx, 0, cursor) < 0))
    RESULT_RAISE();
  RESULT_NEXT(ctx);
}


// The end.
