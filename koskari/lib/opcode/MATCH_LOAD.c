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
  MATCH_LOAD: slot field 2 of the mark at field 1 pushed; an empty
  slot jumps to field 0, pushing nothing. Row LABEL, depth, index.
*/
KOS_OP_HANDLER(MATCH_LOAD) {
  const KosExtra *row = KosOp_ROW(insn);
  (void) kos_op_delivery_;

  PyObject *value = NULL;
  if (unlikely(kos_match_slot_at(
          es, KosExtra_IMM(row, 1), KosExtra_IMM(row, 2), &value) < 0))
    RESULT_RAISE_AT(es, insn);
  if (! value)
    KOS_MATCH_MISS(row);
  RESULT_PUSH(es, value);
}


// The end.
