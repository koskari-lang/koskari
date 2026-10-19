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
  MATCH_MIN: the items list at field 1 must hold at least field 2
  items. Row LABEL, depth, count.
*/
KOS_OP_HANDLER(MATCH_MIN) {
  const KosExtra *row = KosOp_ROW(insn);
  (void) kos_op_delivery_;

  PyObject *list = kos_match_items_at(es, KosExtra_IMM(row, 1));
  if (unlikely(! list))
    RESULT_RAISE_AT(es, insn);
  if (PyList_GET_SIZE(list) < (Py_ssize_t) KosExtra_IMM(row, 2))
    KOS_MATCH_MISS(row);
  RESULT_NEXT(es);
}


// The end.
