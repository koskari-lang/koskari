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
  MATCH_NEXT_CORE_TYPE_VAR: Schema LABEL, slot, RAW. Pull from the slot; the element must be an instance of the core type the RAW ordinal names; miss jumps with the cursor untouched. The body is kos_match_next_core_type in eval/eval_match.c
  (design/interpreter/matcher-bytecode.md).
*/
KOS_OP_HANDLER(MATCH_NEXT_CORE_TYPE_VAR) {
  (void) kos_op_delivery_;
  TAILCALL(kos_match_next_core_type, es, insn, NULL);
}


// The end.
