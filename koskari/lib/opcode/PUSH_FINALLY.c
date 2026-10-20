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
   PUSH_FINALLY: arg = target label for finally block. Push a FINALLY region
   onto the dynamic chain.
*/

KOS_OP_HANDLER(PUSH_FINALLY) {
  KOS_SYNC_PC(ctx, insn);
  KosDynNode *const region = (KosDynNode *) kos_dyn_push_finally(ctx, KosOp_ARG(insn));
  if (! region) {
    PyErr_NoMemory();
    RESULT_RAISE();
  }
  RESULT_NEXT(ctx);
}


// The end.
