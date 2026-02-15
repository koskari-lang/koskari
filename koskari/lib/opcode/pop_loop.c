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
   POP_LOOP: arg unused. Pop LOOP/FOREACH region, drop stack block,
   and push KosDynRegion_RESULT(region) to the operand stack as the loop expression result.
*/

KOS_OP_HANDLER(POP_LOOP) {
  if (! KosContext_DYN_TOP(ctx) || ! KosDynNode_IS_REGION(KosContext_DYN_TOP(ctx))) {
    PyErr_SetString(PyExc_RuntimeError, "POP_LOOP: top is not a region");
    return -1;
  }
  KosDynRegion *region = (KosDynRegion *) KosContext_DYN_TOP(ctx);
  if (KosDynRegion_STYLE(region) != KOS_REGION_LOOP && KosDynRegion_STYLE(region) != KOS_REGION_FOREACH) {
    PyErr_SetString(PyExc_RuntimeError,
                    "POP_LOOP: top is not a LOOP or FOREACH region");
    return -1;
  }

  /* Get result before popping region (region owns the reference) */
  PyObject *result = KosDynRegion_RESULT(region);
  Py_INCREF(result);

  /* Drop the stack block (both LOOP and FOREACH now push blocks) */
  if (KosStackBlock_DropBlock(&CURRENT_STACK(ctx)) < 0) {
    Py_DECREF(result);
    return -1;
  }

  /* Pop the region (this DECREFs KosDynRegion_RESULT(region)) */
  kos_dyn_pop_region(ctx, region);

  /* Push result to stack as loop expression value */
  if (KosStackBlock_PushRef(&CURRENT_STACK(ctx), result) < 0) {
    Py_DECREF(result);
    return -1;
  }
  Py_DECREF(result);

  (void) out_ev;
  (void) insn;
  return 0;
}
