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
  MATCH_NEXT_APPEND: Schema LABEL, then the depths of the cursor and of a list under the top. Pull from the cursor and append the boxed element to the list.
  (design/interpreter/matcher-bytecode.md).
*/
KOS_OP_HANDLER(MATCH_NEXT_APPEND) {
  (void) kos_op_delivery_;
  TAILCALL(kos_match_next_append, es, insn, NULL);
}


// The end.
