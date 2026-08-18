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
  CHECK_NIL: arg = target PC. Pop TOS; branch when it is NOT an
  empty-list singleton. The squashed form of NILP followed by
  JUMP_FALSE.
*/
KOS_OP_HANDLER(CHECK_NIL) {
  return kos_op_result_from_push_int(kos_check_nil(es, insn));
}


// The end.
