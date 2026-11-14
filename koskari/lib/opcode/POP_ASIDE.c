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
   POP_ASIDE: arg unused. Closes the instructions of an aside. The
   frame the aside is written in passes from the PUSH_ASIDE to the
   instruction after this one, and the aside's own frame leaves by its
   RETURN, so no frame runs it: one that does ran an aside past its
   end.
*/

KOS_OP_HANDLER(POP_ASIDE) {
  KOS_SYNC_PC(ctx, insn);
  PyErr_SetString(PyExc_RuntimeError, "POP_ASIDE: an aside ran past its end");
  RESULT_RAISE();
}


// The end.
