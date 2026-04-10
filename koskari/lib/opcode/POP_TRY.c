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
   POP_TRY: NOARG. Promote one value (TOS) from try block to parent before
   dropping. Verify top of dynamic chain is a TRY region, clear exception
   state, then pop the region.
*/

KOS_OP_HANDLER(POP_TRY) {
  KosDynNode *region = (KosDynNode *) KosContext_DYN_TOP(ctx);
  if (! region || ! KosDynNode_IS_TRY(region)) {
    PyErr_SetString(PyExc_RuntimeError, "POP_TRY: top is not a TRY region");
    return KOS_OP_POST_ERR();
  }

  PyErr_Clear();
  PyErr_SetExcInfo(NULL, NULL, NULL);
  if (KosStackBlock_PopBlock(&CURRENT_STACK(ctx), 1) < 0)
    return KOS_OP_POST_ERR();
  kos_dyn_pop_try(ctx, (KosDynTry *) region);
  (void) out_ev;
  return KOS_OP_RUN();
}
