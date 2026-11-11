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
   POP_DYN_WITH: arg unused. A dyn-with pops exactly as a plain with
   does; the node kind is the same, and the capability a transfer reads
   is the node's own affair. Defined after POP_WITH in handlers.incl.
*/

KOS_OP_HANDLER(POP_DYN_WITH) {
  KOS_SYNC_PC(ctx, insn);
  TAILCALL(op_POP_WITH, ctx, insn, kos_op_delivery_);
}


// The end.
