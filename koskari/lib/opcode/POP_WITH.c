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
  PyObject *exit_awaitable = NULL;

  int unw = kos_dyn_with_unwind(ctx, with_node, &exit_awaitable);
  SAYF("[eval] POP_WITH unw=%d exit_awaitable=%d\n", unw, exit_awaitable ? 1 : 0);
  if (unw == KOS_DYN_UNWIND_ASYNC_STARTED) {
    SAY("[eval] POP_WITH async exit started -> kos_with_exit_start_await");
    int r = kos_with_exit_start_await(ctx, out_ev, exit_awaitable);
    if (r == 1)
      return 1;
    if (r < 0)
      return -1;
    /* r == 0: exit completed synchronously; push_or_await already pushed
       the exit result. Consume it and push None (design: with value is None). */
    return kos_complete_pending_with_exit(ctx);
  }
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
