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
   CLEAR_FINALLY: arg unused. Disable the top FINALLY region.
*/

KOS_OP_HANDLER(CLEAR_FINALLY) {
  if (! KosContext_DYN_TOP(ctx) || ! KosDynNode_IS_REGION(KosContext_DYN_TOP(ctx))) {
    PyErr_SetString(PyExc_RuntimeError, "CLEAR_FINALLY: top is not a region");
    return -1;
  }
  KosDynNode *region = (KosDynNode *) KosContext_DYN_TOP(ctx);
  if (! KosDynNode_IS_FINALLY(region)) {
    PyErr_SetString(PyExc_RuntimeError,
                    "CLEAR_FINALLY: top is not a FINALLY region");
    return -1;
  }
  if (KosDynNode_IS_TRY(region)) KosDynTry_ENABLED((KosDynTry *) region)=0; else if (KosDynNode_IS_FINALLY(region)) KosDynFinally_ENABLED((KosDynFinally *) region)=0; else if (KosDynNode_IS_LOOP(region)) KosDynLoop_ENABLED((KosDynLoop *) region)=0; else KosDynForeach_ENABLED((KosDynForeach *) region)=0;
  (void) out_ev;
  (void) insn;
  return 0;
}
