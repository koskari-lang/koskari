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
  OBJECT_NOT: pop TOS, call PyObject_Not, push Py_True/Py_False.
*/
KOS_OP_HANDLER(OBJECT_NOT) {
  KOS_SYNC_PC(ctx, insn);
  (void) insn;

  PyObject *val = KosContext_StackPopRef(ctx);
  if (unlikely(! val))
    RESULT_RAISE();

  int r = PyObject_Not(val);
  Py_DECREF(val);

  if (unlikely(r < 0))
    RESULT_RAISE();

  PyObject *res = (r ? Py_True : Py_False);
  RESULT_PUSH(ctx, res);
}


// The end.
