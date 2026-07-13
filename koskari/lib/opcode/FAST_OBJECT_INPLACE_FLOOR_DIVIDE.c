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
   FAST_OBJECT_INPLACE_FLOOR_DIVIDE: arg = slot index. Pop TOS (rhs), fetch obj from
   fast slot, PyNumber_InPlaceFloorDivide(obj, rhs), store result back.
   Does not push.
*/

KOS_OP_HANDLER(FAST_OBJECT_INPLACE_FLOOR_DIVIDE) {
  return kos_op_result_from_push_int(
      kos_fast_inplace_binop(es, KOS_BINOP_FLOOR_DIVIDE, (size_t) KosOp_ARG(insn)));
}


// The end.
