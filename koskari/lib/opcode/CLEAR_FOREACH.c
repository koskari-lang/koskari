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
   CLEAR_FOREACH: arg unused. Disable the top FOREACH region (used when
   exiting normally before POP_FOREACH).
*/

KOS_OP_HANDLER(CLEAR_FOREACH) {
  KosDynNode *region = KosEvalState_CurrentNode(es);
  if (! region || ! KosDynNode_IS_FOREACH(region)) {
    PyErr_SetString(PyExc_RuntimeError,
                    "CLEAR_FOREACH: top is not a FOREACH region");
    return KOS_OP_POST_ERR();
  }

  KosDynForeach_ENABLED((KosDynForeach *) region) = 0;
  if (KosEvalState_StackClearBlock(es) < 0)
    return KOS_OP_POST_ERR();

  (void) out_ev;
  (void) insn;
  return KOS_OP_RUN();
}


// The end.
