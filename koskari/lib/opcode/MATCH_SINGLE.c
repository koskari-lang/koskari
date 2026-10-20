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
  MATCH_SINGLE: a one-element cursor over TOS replaces it. No arg.
*/
KOS_OP_HANDLER(MATCH_SINGLE) {
  KOS_SYNC_PC(es, insn);
  (void) kos_op_delivery_;

  PyObject *value = KosEvalState_StackPeek(es);
  if (unlikely(! value))
    RESULT_RAISE();

  PyObject *cursor = KosMatchCursor_Single(value);
  if (unlikely(! cursor))
    RESULT_RAISE();
  if (unlikely(KosEvalState_StackSetUnder(es, 0, cursor) < 0))
    RESULT_RAISE();
  RESULT_NEXT(es);
}


// The end.
