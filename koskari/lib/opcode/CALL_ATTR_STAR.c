/*
  This library is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 3 of the License, or
  (at your option) any later version.

  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this library; if not, see <http://www.gnu.org/licenses/>.
*/


/**
   CALL_ATTR_STAR: arg = extra index. Schema CONST (attr name), COUNT
   (bitflags: 1 = has *args, 2 = has **kwargs). Stack [obj, pos, kwd]
   per flags; obj.name is resolved through the per-site cache slot.
*/
KOS_OP_HANDLER_COLD(CALL_ATTR_STAR) {
  const KosExtra *row = KosOp_ROW(insn);

  KosOpResult res = kos_op_call_star(
      es, NULL, KosExtra_OBJ(row, 0), KosOp_SLOT(insn),
      KosExtra_IMM(row, 1), 0);
  RESULT_RELAY(es, res);
}


// The end.
