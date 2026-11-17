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
   PUSH_WIND: arg = extra index. Schema ASIDE (the before segment, or
   None), ASIDE (the after segment, or None).

   Push a wind node over this code and row, then enter it: a before
   segment runs in a frame of its own above the node, over the
   namespace live here, and its return settles the wind. The body
   starts at the next instruction.
*/

KOS_OP_HANDLER(PUSH_WIND) {
  KOS_SYNC_PC(ctx, insn);
  KosDynFrame *const caller = KosContext_FRAME(ctx);
  KosDynWind *wind;
  KosOpResult wr;

  wind = kos_dyn_push_wind(ctx, KosDynFrame_CODE(caller), KosOp_ROW(insn));
  if (! wind)
    RESULT_RAISE();

  wr = kos_dyn_extent_enter(ctx, (KosDynNode *) wind);
  if (wr.kind == KOS_RES_RAISE)
    RESULT_RAISE();

  /* the before segment's frame is current: continue from ctx->pc runs
     it, and its return settles the wind */
  if (wr.kind == KOS_RES_REPROLOGUE)
    RESULT_CONTINUE(ctx);

  /* NORMAL: no before segment */
  RESULT_NEXT(ctx);
}


// The end.
