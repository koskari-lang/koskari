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
   POP_WITH: arg unused. Pop single-shot with context. Call exit(typ, val, tb);
   auto-await if exit returns awaitable. If exception and exit returns true,
   swallow; else re-raise. Drop stack block, pop KOS_DYN_WITH node, push None.
*/

KOS_OP_HANDLER(POP_WITH) {

  SAY("[eval] POP_WITH entry");
  KosDynNode *top = KosContext_DYN_TOP(ctx);
  if (! top || ! KosDynNode_IS_WITH(top)) {
    SAY("[eval] POP_WITH top is not a with node");
    PyErr_SetString(PyExc_RuntimeError, "POP_WITH: top is not a with node");
    return -1;
  }
  KosDynWith *with_node = (KosDynWith *) top;

  int unw = kos_dyn_with_unwind(ctx, with_node, out_ev);
  if (unw == KOS_DYN_UNWIND_ASYNC_STARTED)
    return KOS_DYN_UNWIND_ASYNC_STARTED;
  if (unw < 0)
    return -1;

  /* Sync path: kos_dyn_with_unwind already applied swallow/restore and DECREF result. */
  SAY("[eval] POP_WITH sync path: DropBlock then push None");
  if (KosStackBlock_DropBlock(&CURRENT_STACK(ctx)) < 0)
    return -1;
  kos_dyn_pop_with(ctx, with_node);
  if (KosStackBlock_PushRef(&CURRENT_STACK(ctx), Py_None) < 0)
    return -1;

  SAY("[eval] POP_WITH sync path done -> return 0");
  (void) out_ev;
  (void) insn;
  return 0;
}


// The end.
