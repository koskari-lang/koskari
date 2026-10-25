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
   NILP: pop TOS, push Py_True if it is an empty-list singleton (nil or
   bracket-nil), else Py_False. Compression lowers a NILP followed by a
   JUMP_FALSE into a single CHECK_NIL.
*/
KOS_OP_HANDLER(NILP) {
  KOS_SYNC_PC(ctx, insn);
  PyObject *val = KosContext_StackPopRef(ctx);
  if (! val) RESULT_RAISE();
  PyObject *res = (KosNilP(val) ? Py_True : Py_False);
  Py_DECREF(val);
  Py_INCREF(res);
  (void) insn;
  RESULT_STEAL(ctx, res);
}


// The end.
