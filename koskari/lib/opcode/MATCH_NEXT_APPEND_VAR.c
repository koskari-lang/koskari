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
  MATCH_NEXT_APPEND_VAR: Schema LABEL, slot, slot. Pull from the first slot and append the element, boxed from a pair spine, to the list in the second; miss jumps at the end. The body is kos_match_next_append in eval/eval_match.c
  (design/interpreter/matcher-bytecode.md).
*/
KOS_OP_HANDLER(MATCH_NEXT_APPEND_VAR) {
  (void) kos_op_delivery_;
  TAILCALL(kos_match_next_append, es, insn, NULL);
}


// The end.
