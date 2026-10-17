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
  MATCH2_SPAN: Row LABEL, mark depth, cursor depth. Pushes the span
  from the mark's position to the cursor's; an empty span jumps,
  pushing nothing.
  The body is kos_match2_span in eval/eval_match.c
  (design/interpreter/matcher-core.md).
*/
KOS_OP_HANDLER(MATCH2_SPAN) {
  (void) kos_op_delivery_;
  TAILCALL(kos_match2_span, es, insn, NULL);
}


// The end.
