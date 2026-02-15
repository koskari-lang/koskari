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
   JUMP_TRUE: arg = target PC. Branch if TOS is true.
*/

KOS_OP_HANDLER(JUMP_TRUE) {
  PyObject *val = KosStackBlock_PopRef(&CURRENT_STACK(ctx));
  if (! val)
    return -1;
  int res = PyObject_IsTrue(val);
  Py_DECREF(val);
  if (res < 0)
    return -1;
  if (res)
    CURRENT_PC(ctx) = insn->arg;
  (void) out_ev;
  return 0;
}
