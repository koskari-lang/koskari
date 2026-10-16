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
  MATCH_NEXT_TYPE_BIND_FAST: Schema LABEL, then the depth of the cursor under the top, the type's slot and the capture slot. The typed pull, the boxed element stored into the capture slot.
  (design/interpreter/matcher-bytecode.md).
*/
KOS_OP_HANDLER(MATCH_NEXT_TYPE_BIND_FAST) {
  (void) kos_op_delivery_;
  TAILCALL(kos_match_next_type, es, insn, NULL);
}


// The end.
