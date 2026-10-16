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
  MATCH_COLLECT_FAST: Schema LABEL, then the depths of the cursor and of a list under the top. Every remaining element, boxed from a pair spine, appended to the list; an improper tail is a miss.
  (design/interpreter/matcher-bytecode.md).
*/
KOS_OP_HANDLER(MATCH_COLLECT_FAST) {
  (void) kos_op_delivery_;
  TAILCALL(kos_match_collect, es, insn, NULL);
}


// The end.
