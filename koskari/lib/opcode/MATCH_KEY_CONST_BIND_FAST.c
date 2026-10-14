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
  MATCH_KEY_CONST_BIND_FAST: Schema LABEL, key, slot. Peek: the value under the key (the constant, or the value of the key slot) in the mapping on TOS, pushed, or stored into the slot by the BIND forms; an absent key jumps. The body is kos_match_key in eval/eval_match.c
  (design/interpreter/matcher-bytecode.md).
*/
KOS_OP_HANDLER(MATCH_KEY_CONST_BIND_FAST) {
  (void) kos_op_delivery_;
  TAILCALL(kos_match_key, es, insn, NULL);
}


// The end.
