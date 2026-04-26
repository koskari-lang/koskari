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
   JUMP_FALSE: arg = target PC. Branch if TOS is false.
*/

KOS_OP_HANDLER(JUMP_FALSE) {
  PyObject *val = KosEvalState_StackPopRef(es);
  if (! val)
    return KOS_OP_POST_ERR();
  int res = PyObject_Not(val);
  Py_DECREF(val);
  if (res < 0)
    return KOS_OP_POST_ERR();
  if (res) {
    KosEvalState_SET_PC(es, KosOp_ARG(insn));
  }
  (void) out_ev;
  return KOS_OP_RUN();
}
