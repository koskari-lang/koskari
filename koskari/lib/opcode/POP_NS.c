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
   POP_NS: arg unused. Verify dyn_top is KOS_DYN_NS and pop the NS node.
*/
KOS_OP_HANDLER(POP_NS) {
  KOS_SYNC_PC(ctx, insn);
  (void) insn;

  KosDynNode *top = KosEvalState_CurrentNode(ctx);
  if (unlikely(kos_dyn_pop_ns(ctx, (KosDynNs *) top) < 0))
    RESULT_RAISE();

  RESULT_NEXT(ctx);
}


// The end.
