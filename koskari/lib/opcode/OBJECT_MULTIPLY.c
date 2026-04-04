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
   OBJECT_MULTIPLY: arg = number of operands. Pop N, multiply, push result
   or await it.
*/

KOS_OP_HANDLER(OBJECT_MULTIPLY) {
  return kos_op_outcome_from_int_step(obj_op_reducing(ctx, out_ev, insn, KosOperator_Multiply));
}
