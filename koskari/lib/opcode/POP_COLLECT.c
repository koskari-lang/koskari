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
   POP_COLLECT: arg unused. Pop the COLLECT node at dyn_top and push the
   namespace it collected onto the value stack. The pop verifies the
   kind and hands the namespace out revealed.
*/

KOS_OP_HANDLER(POP_COLLECT) {
  KOS_SYNC_PC(ctx, insn);
  (void) insn;

  KosDynCollect *top = (KosDynCollect *) KosEvalState_CurrentNode(ctx);
  PyObject *collector;

  if (unlikely(kos_dyn_pop_collect(ctx, top, &collector) < 0))
    RESULT_RAISE();

  RESULT_STEAL(ctx, collector);
}


// The end.
