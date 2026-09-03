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
   CLEAR_TRY: arg unused. Disable the top TRY region (so it will not catch).
*/

KOS_OP_HANDLER(CLEAR_TRY) {
  KosDynNode *region = KosEvalState_CurrentNode(es);
  if (! region || ! KosDynNode_IS_TRY(region)) {
    PyErr_SetString(PyExc_RuntimeError, "CLEAR_TRY: top is not a TRY region");
    RESULT_RAISE();
  }
  KosDynTry_ENABLED((KosDynTry *) region) = 0;
  (void) insn;
  RESULT_NEXT(es);
}


// The end.
