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
  MATCH_COUNT: Schema LABEL, then the depth of a list under the top and
  a count. The list must hold at least the count of items, else jump
  to the label: a repeat's minimum. The body is kos_match_count in
  eval/eval_match.c (design/interpreter/matcher-bytecode.md).
*/
KOS_OP_HANDLER(MATCH_COUNT) {
  (void) kos_op_delivery_;
  TAILCALL(kos_match_count, es, insn, NULL);
}


// The end.
