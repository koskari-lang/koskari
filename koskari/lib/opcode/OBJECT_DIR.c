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
  OBJECT_DIR: pop TOS, call PyObject_Dir, push result.
*/
KOS_OP_HANDLER(OBJECT_DIR) {
  KOS_SYNC_PC(ctx, insn);
  (void) insn;

  PyObject *val = KosEvalState_StackPopRef(ctx);
  if (! val) RESULT_RAISE();

  PyObject *res = PyObject_Dir(val);
  Py_DECREF(val);

  if (! res) RESULT_RAISE();
  RESULT_STEAL(ctx, res);
}


// The end.
