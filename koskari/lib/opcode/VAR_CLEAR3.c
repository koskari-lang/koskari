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
   VAR_CLEAR3: arg = extra index. Schema VAR, VAR, VAR (name
   constants). Clear the 3 names in the current frame namespace in
   row order; an unbound name stays unbound.
*/
KOS_OP_HANDLER(VAR_CLEAR3) {
  KOS_SYNC_PC(es, insn);
  const KosExtra *row = KosOp_ROW(insn);
  if (frame_ns_clear(es, KosExtra_OBJ(row, 0)) < 0)
    RESULT_RAISE();
  if (frame_ns_clear(es, KosExtra_OBJ(row, 1)) < 0)
    RESULT_RAISE();
  if (frame_ns_clear(es, KosExtra_OBJ(row, 2)) < 0)
    RESULT_RAISE();
  RESULT_NEXT(es);
}


// The end.
