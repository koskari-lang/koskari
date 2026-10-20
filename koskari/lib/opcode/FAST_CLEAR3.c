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
   FAST_CLEAR3: arg = extra index. Schema FASTVAR, FASTVAR, FASTVAR. Clear
   the 3 fast slots in row order; an empty slot stays empty.
*/
KOS_OP_HANDLER(FAST_CLEAR3) {
  KOS_SYNC_PC(es, insn);
  const KosExtra *row = KosOp_ROW(insn);
  PyObject *ns = CURRENT_NS();
  KOS_OP_REQUIRE_NS(ns);
  if (KosNamespace_FastClear(ns, (size_t) KosExtra_IMM(row, 0)) < 0)
    RESULT_RAISE();
  if (KosNamespace_FastClear(ns, (size_t) KosExtra_IMM(row, 1)) < 0)
    RESULT_RAISE();
  if (KosNamespace_FastClear(ns, (size_t) KosExtra_IMM(row, 2)) < 0)
    RESULT_RAISE();
  RESULT_NEXT(es);
}


// The end.
