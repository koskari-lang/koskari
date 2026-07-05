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
   OBJECT_INPLACE_BIT_RIGHT: arg = number of operands (N >= 2). Pop N
   values, reduce left-to-right with KosOperator_InplaceRshift, push the
   result or await it. The in-place >> used by the inplace-item and
   inplace-attr special forms.
*/

KOS_OP_HANDLER(OBJECT_INPLACE_BIT_RIGHT) {
  return kos_op_result_from_push_int(
    obj_op_reducing(es, insn, KosOperator_InplaceRshift));
}


// The end.
