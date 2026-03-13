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
   BREAK: arg = result count (0 = None, 1 = single value, N = tuple of N).
   Exit innermost loop/foreach; jump to its break target. Pops the loop
   region and pushes the result value to the parent stack as the loop
   expression value. Raises RuntimeError if no LOOP/FOREACH region on chain.
*/

KOS_OP_HANDLER(BREAK) {
  unsigned opa = KosOp_ARG(insn);
  PyObject *result = NULL;

  /* Pop result value(s) from stack before any unwinding */
  if (opa == 0) {
    result = Py_None;
    Py_INCREF(result);
  } else if (opa == 1) {
    result = KosStackBlock_PopRef(&CURRENT_STACK(ctx));
    if (! result) {
      PyErr_SetString(PyExc_RuntimeError, "BREAK pop underrun");
      return -1;
    }
  } else {
    result = KosStackBlock_PopTail(&CURRENT_STACK(ctx), (size_t) opa);
    if (! result) {
      PyErr_SetString(PyExc_RuntimeError, "BREAK PopTail underrun");
      return -1;
    }
  }

  KosDynNode *region = kos_find_node_for_loop(ctx);
  if (! region) {
    Py_DECREF(result);
    PyErr_SetString(PyExc_RuntimeError, "break outside loop");
    return -1;
  }

  uint32_t break_pc = (KosDynNode_IS_LOOP(region) ? KosDynLoop_BREAK_PC((KosDynLoop *) region) : KosDynForeach_BREAK_PC((KosDynForeach *) region));

  /* Unwind inner regions until we reach the target loop region */
  while (KosContext_DYN_TOP(ctx) != (KosDynNode *) region) {
    KosDynNode *top = KosContext_DYN_TOP(ctx);
    if (KosDynNode_IS_REGION(top)) {
      KosDynNode *r = (KosDynNode *) top;
      if (KosDynNode_IS_LOOP(r)) {
        kos_dyn_pop_loop(ctx, (KosDynLoop *) r);
      } else if (KosDynNode_IS_FOREACH(r)) {
        kos_dyn_pop_foreach(ctx, (KosDynForeach *) r);
        if (KosStackBlock_DropBlock(&CURRENT_STACK(ctx)) < 0) {
          Py_DECREF(result);
          return -1;
        }
      } else {
        int pop_res = kos_dyn_pop_top(ctx, KOS_XFER_EVENT_BREAK, out_ev);
        if (pop_res == KOS_DYN_UNWIND_ASYNC_STARTED)
          return 0;
        if (pop_res < 0) {
          Py_DECREF(result);
          return -1;
        }
      }
    } else {
      int pop_res = kos_dyn_pop_top(ctx, KOS_XFER_EVENT_BREAK, out_ev);
      if (pop_res == KOS_DYN_UNWIND_ASYNC_STARTED)
        return 0;
      if (pop_res < 0) {
        Py_DECREF(result);
        return -1;
      }
    }
  }

  /* Pop the target loop region and its stack block */
  if (KosDynNode_IS_TRY(region)) kos_dyn_pop_try(ctx, (KosDynTry *) region); else if (KosDynNode_IS_FINALLY(region)) kos_dyn_pop_finally(ctx, (KosDynFinally *) region); else if (KosDynNode_IS_LOOP(region)) kos_dyn_pop_loop(ctx, (KosDynLoop *) region); else if (KosDynNode_IS_FOREACH(region)) kos_dyn_pop_foreach(ctx, (KosDynForeach *) region);
  if (KosStackBlock_DropBlock(&CURRENT_STACK(ctx)) < 0) {
    Py_DECREF(result);
    return -1;
  }

  /* Push result to parent stack as the loop expression value */
  if (KosStackBlock_PushRef(&CURRENT_STACK(ctx), result) < 0) {
    Py_DECREF(result);
    return -1;
  }
  Py_DECREF(result);

  CURRENT_PC(ctx) = break_pc;
  (void) out_ev;
  return 0;
}
