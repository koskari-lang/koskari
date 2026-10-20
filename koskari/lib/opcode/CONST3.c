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
  CONST3: arg = extra index. Schema CONST x3. Push the three constants in
  row order. A constant is a borrowed pool reference, so nothing is
  held across a failed push.
*/
KOS_OP_HANDLER(CONST3) {
  KOS_SYNC_PC(ctx, insn);
  const KosExtra *row = KosOp_ROW(insn);

  /* the row's four fields are the pool objects, one pointer each,
     so the row is the vector */
  if (unlikely(kos_stack_push_ref_n_fast(
                   KosEvalState_BLOCK(ctx), KosExtra_OBJS(row), 3) < 0))
    RESULT_RAISE();
  RESULT_NEXT(ctx);
}


// The end.
