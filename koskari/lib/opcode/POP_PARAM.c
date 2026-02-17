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
   POP_PARAM: arg unused. Pop KOS_DYN_PARAM extent from dynamic chain.
*/

KOS_OP_HANDLER(POP_PARAM) {
  KosDynNode *top = KosContext_DYN_TOP(ctx);
  if (! top || ! KosDynNode_IS_PARAM(top)) {
    PyErr_SetString(PyExc_RuntimeError, "POP_PARAM: top is not a PARAM extent");
    return -1;
  }

  kos_dyn_pop_param(ctx, (KosDynParam *) top);

  (void) out_ev;
  (void) insn;
  return 0;
}


// The end.
