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
   POP_TRY: arg = count of values to move from try block to parent before
   dropping the block. Verify top of dynamic chain is a TRY region, clear
   exception state, then pop the region.
*/

KOS_OP_HANDLER(POP_TRY) {
  if (! KosContext_DYN_TOP(ctx) || ! KosDynNode_IS_REGION(KosContext_DYN_TOP(ctx))) {
    PyErr_SetString(PyExc_RuntimeError, "POP_TRY: top is not a region");
    return -1;
  }
  KosDynRegion *region = (KosDynRegion *) KosContext_DYN_TOP(ctx);
  if (KosDynRegion_STYLE(region) != KOS_REGION_TRY) {
    PyErr_SetString(PyExc_RuntimeError, "POP_TRY: top is not a TRY region");
    return -1;
  }

  PyErr_Clear();
  PyErr_SetExcInfo(NULL, NULL, NULL);
  unsigned opa = (unsigned) KosOp_ARG(insn);
  if (KosStackBlock_PopBlock(&CURRENT_STACK(ctx), (size_t) opa) < 0)
    return -1;
  kos_dyn_pop_region(ctx, region);
  (void) out_ev;
  return 0;
}
