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
  MATCH2_STORE: Row mark depth, index, cursor depth, names. v -> : v
  into the mark's slot; a Datum takes the slot's name as its binding
  when the cursor walks a pair spine.
  The body is kos_match2_store in eval/eval_match.c
  (design/interpreter/matcher-core.md).
*/
KOS_OP_HANDLER(MATCH2_STORE) {
  (void) kos_op_delivery_;
  TAILCALL(kos_match2_store, es, insn, NULL);
}


// The end.
