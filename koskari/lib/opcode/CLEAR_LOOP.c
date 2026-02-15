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
   CLEAR_LOOP: arg unused. Disable the top LOOP/FOREACH region (used when
   exiting normally before POP_LOOP).
*/

KOS_OP_HANDLER(CLEAR_LOOP) {
  if (! KosContext_DYN_TOP(ctx) || ! KosDynNode_IS_REGION(KosContext_DYN_TOP(ctx))) {
    PyErr_SetString(PyExc_RuntimeError, "CLEAR_LOOP: top is not a region");
    return -1;
  }
  KosDynRegion *region = (KosDynRegion *) KosContext_DYN_TOP(ctx);
  if (KosDynRegion_STYLE(region) != KOS_REGION_LOOP && KosDynRegion_STYLE(region) != KOS_REGION_FOREACH) {
    PyErr_SetString(PyExc_RuntimeError,
                    "CLEAR_LOOP: top is not a LOOP or FOREACH region");
    return -1;
  }
  KosDynRegion_ENABLED(region) = 0;
  (void) out_ev;
  (void) insn;
  return 0;
}
