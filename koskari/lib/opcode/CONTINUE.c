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

  KosDynRegion *region = kos_find_region_for_loop(ctx);
  if (! region) {
    Py_DECREF(result);
    PyErr_SetString(PyExc_RuntimeError, "continue outside loop");
    return -1;
  }

  /* Update region result with continue value */
  Py_XDECREF(KosDynRegion_RESULT(region));
  KosDynRegion_RESULT(region) = result;

  uint32_t continue_pc = KosDynRegion_CONTINUE_PC(region);

  /* Unwind inner regions until we reach the target loop region */
  while (KosContext_DYN_TOP(ctx) != (KosDynNode *) region) {
    KosDynNode *top = KosContext_DYN_TOP(ctx);
    if (KosDynNode_IS_REGION(top)) {
      KosDynRegion *r = (KosDynRegion *) top;
      if (KosDynRegion_STYLE(r) == KOS_REGION_LOOP || KosDynRegion_STYLE(r) == KOS_REGION_FOREACH) {
        kos_dyn_pop_region(ctx, r);
        if (KosStackBlock_DropBlock(&CURRENT_STACK(ctx)) < 0)
          return -1;
      } else {
        if (kos_dyn_pop_top(ctx) < 0)
          return -1;
      }
    } else {
      if (kos_dyn_pop_top(ctx) < 0)
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
