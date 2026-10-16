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
  MATCH_NEXT_CORE_TYPE: Schema LABEL, then the depth of the cursor under the top and the core type's ordinal. Pull from the cursor; the element must be an instance of the core type.
  (design/interpreter/matcher-bytecode.md).
*/
KOS_OP_HANDLER(MATCH_NEXT_CORE_TYPE) {
  (void) kos_op_delivery_;
  TAILCALL(kos_match_next_core_type, es, insn, NULL);
}


// The end.
