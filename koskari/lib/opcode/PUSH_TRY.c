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
   PUSH_TRY: arg = target label for exception handler. Push a TRY region
   onto the dynamic chain.
*/

KOS_OP_HANDLER(PUSH_TRY) {
  KosDynNode *region = (KosDynNode *) kos_dyn_push_try(ctx, KosOp_ARG(insn));
  if (! region) {
    PyErr_NoMemory();
    return -1;
  }
  if (KosStackBlock_PushBlock(&CURRENT_STACK(ctx)) < 0) {
    if (KosDynNode_IS_TRY(region)) kos_dyn_pop_try(ctx, (KosDynTry *) region); else if (KosDynNode_IS_FINALLY(region)) kos_dyn_pop_finally(ctx, (KosDynFinally *) region); else if (KosDynNode_IS_LOOP(region)) kos_dyn_pop_loop(ctx, (KosDynLoop *) region); else if (KosDynNode_IS_FOREACH(region)) kos_dyn_pop_foreach(ctx, (KosDynForeach *) region);
    return -1;
  }
  (void) out_ev;
  return 0;
}
