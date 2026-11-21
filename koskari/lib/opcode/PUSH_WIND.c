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
   follows this instruction.
*/

KOS_OP_HANDLER(PUSH_WIND) {
  KOS_SYNC_PC(ctx, insn);
  const KosExtra *row = KosOp_ROW(insn);
  KosDynWind *wind;
  KosOpResult wr;

  wind = kos_dyn_push_wind(ctx, KosDynFrame_CODE(KosContext_FRAME(ctx)), row);
  if (! wind)
    RESULT_RAISE();

  wr = kos_dyn_extent_enter(ctx, (KosDynNode *) wind);
  if (wr.kind == KOS_RES_RAISE)
    RESULT_RAISE();

  /* ctx->pc is the before segment's frame, whose return settles the
     wind, or with no before segment the body */
  RESULT_CONTINUE(ctx);
}


// The end.
