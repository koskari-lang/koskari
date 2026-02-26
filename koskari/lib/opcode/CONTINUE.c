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
   CONTINUE: arg = result count (0 = None, 1 = single value, N = tuple of N).
   Jump to innermost loop/foreach continue target. Updates the region result
   and clears the loop stack block before jumping. Raises RuntimeError if
   no LOOP/FOREACH region on the chain.
*/

KOS_OP_HANDLER(CONTINUE) {
  unsigned opa = KosOp_ARG(insn);
  PyObject *result = NULL;

  /* Pop result value(s) from stack before any unwinding */
  if (opa == 0) {
    result = Py_None;
    Py_INCREF(result);
  } else if (opa == 1) {
    result = KosStackBlock_PopRef(&CURRENT_STACK(ctx));
    if (! result) {
      PyErr_SetString(PyExc_RuntimeError, "CONTINUE pop underrun");
      return -1;
    }
  } else {
    result = KosStackBlock_PopTail(&CURRENT_STACK(ctx), (size_t) opa);
    if (! result) {
      PyErr_SetString(PyExc_RuntimeError, "CONTINUE PopTail underrun");
      return -1;
    }
  }

  KosDynNode *region = kos_find_node_for_loop(ctx);
  if (! region) {
    Py_DECREF(result);
    PyErr_SetString(PyExc_RuntimeError, "continue outside loop");
    return -1;
  }

  /* Update region result with continue value */
  if (KosDynNode_IS_LOOP(region)) Py_XDECREF(KosDynLoop_RESULT((KosDynLoop *) region)); else Py_XDECREF(KosDynForeach_RESULT((KosDynForeach *) region));
  if (KosDynNode_IS_LOOP(region)) KosDynLoop_RESULT((KosDynLoop *) region) = result; else KosDynForeach_RESULT((KosDynForeach *) region) = result;

  uint32_t continue_pc = (KosDynNode_IS_LOOP(region) ? KosDynLoop_CONTINUE_PC((KosDynLoop *) region) : KosDynForeach_CONTINUE_PC((KosDynForeach *) region));

  /* Unwind inner regions until we reach the target loop region */
  while (KosContext_DYN_TOP(ctx) != (KosDynNode *) region) {
    KosDynNode *top = KosContext_DYN_TOP(ctx);
    if (KosDynNode_IS_REGION(top)) {
      KosDynNode *r = (KosDynNode *) top;
      if (KosDynNode_IS_LOOP(r)) {
        kos_dyn_pop_loop(ctx, (KosDynLoop *) r);
      } else if (KosDynNode_IS_FOREACH(r)) {
        kos_dyn_pop_foreach(ctx, (KosDynForeach *) r);
        if (KosStackBlock_DropBlock(&CURRENT_STACK(ctx)) < 0)
          return -1;
      } else {
        int pop_res = kos_dyn_pop_top(ctx, out_ev);
        if (pop_res == KOS_DYN_UNWIND_ASYNC_STARTED)
          return 0;
        if (pop_res < 0)
          return -1;
      }
    } else {
      int pop_res = kos_dyn_pop_top(ctx, out_ev);
      if (pop_res == KOS_DYN_UNWIND_ASYNC_STARTED)
        return 0;
      if (pop_res < 0)
        return -1;
    }
  }

  /* Clear the loop stack block contents for next iteration */
  if (KosStackBlock_ClearBlock(&CURRENT_STACK(ctx)) < 0)
    return -1;

  CURRENT_PC(ctx) = continue_pc;
  (void) out_ev;
  return 0;
}
