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
  KosDynNode *region = (KosDynNode *) KosContext_DYN_TOP(ctx);
  if (! region || ! KosDynNode_IS_FINALLY(region)) {
    PyErr_SetString(PyExc_RuntimeError,
                    "CLEAR_FINALLY: top is not a FINALLY region");
    return KOS_OP_POST_ERR();
  }
  KosDynFinally_ENABLED((KosDynFinally *) region) = 0;
  (void) out_ev;
  (void) insn;
  return KOS_OP_RUN();
}
