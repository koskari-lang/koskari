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
  MATCH_ITERATION: Schema CONST names, then the first capture slot, the start and the cursor, or on the NAMED forms the start and the cursor with the captures read by name. Push the captures, a DatumFrame with its source over a pair spine and a dict otherwise, an unbound slot absent; the ITERATION forms push one repeat iteration's value, the captures when any slot fired and the boxed span otherwise. The body is kos_match_captures in eval/eval_match.c
  (design/interpreter/matcher-bytecode.md).
*/
KOS_OP_HANDLER(MATCH_ITERATION) {
  (void) kos_op_delivery_;
  TAILCALL(kos_match_captures, es, insn, NULL);
}


// The end.
