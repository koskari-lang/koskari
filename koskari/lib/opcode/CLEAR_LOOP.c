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
   CLEAR_LOOP: arg unused. Disable the top LOOP region (used when
   exiting normally before POP_LOOP).
*/

KOS_OP_HANDLER(CLEAR_LOOP) {
  KosDynNode *region = KosEvalState_CurrentNode(es);
  if (! region || ! KosDynNode_IS_LOOP(region)) {
    PyErr_SetString(PyExc_RuntimeError,
                    "CLEAR_LOOP: top is not a LOOP region");
    RESULT_RAISE();
  }

  KosDynLoop_ENABLED((KosDynLoop *) region) = 0;
  if (KosEvalState_StackClear(es) < 0)
    RESULT_RAISE();

  (void) insn;
  RESULT_NEXT(es);
}


// The end.
