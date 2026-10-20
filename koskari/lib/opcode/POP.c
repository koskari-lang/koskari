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
  POP: arg = number of stack slots to drop. Remove that many values from
  the top of the stack.
*/
KOS_OP_HANDLER(POP) {

  unsigned n = KosOp_ARG(insn);
  if (likely(n == 1)) {
    if (unlikely(KosEvalState_StackDrop1(ctx) < 0))
      RESULT_RAISE_AT(ctx, insn);
    RESULT_NEXT(ctx);
  }

  if (unlikely(KosEvalState_StackDrop(ctx, (size_t) n) < 0))
    RESULT_RAISE_AT(ctx, insn);
  RESULT_NEXT(ctx);
}


// The end.
