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
  MATCH_TYPE_BIND_FAST: Schema LABEL, slot, slot. Peek: TOS must be an instance of the type in the first slot; the BIND twins store TOS into the second; miss jumps, nothing popped. The body is kos_match_type in eval/eval_match.c
  (design/interpreter/matcher-bytecode.md).
*/
KOS_OP_HANDLER(MATCH_TYPE_BIND_FAST) {
  (void) kos_op_delivery_;
  TAILCALL(kos_match_type, es, insn, NULL);
}


// The end.
