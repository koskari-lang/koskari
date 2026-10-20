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
  MATCH_FIELD_KW: the attribute of TOS the constant in field 1
  names, pushed; a missing attribute is a miss. Row LABEL, const.
*/
KOS_OP_HANDLER(MATCH_FIELD_KW) {
  KOS_SYNC_PC(es, insn);
  const KosExtra *row = KosOp_ROW(insn);
  (void) kos_op_delivery_;

  PyObject *obj = KosEvalState_StackPeek(es);
  if (unlikely(! obj))
    RESULT_RAISE();

  PyObject *val = NULL;
  int r = KosMatch_FieldKw(obj, KosExtra_OBJ(row, 1), &val);
  if (unlikely(r < 0))
    RESULT_RAISE();
  if (! r)
    KOS_MATCH_MISS(row);
  RESULT_STEAL(es, val);
}


// The end.
