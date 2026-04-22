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
   JUMP_AND: arg = target PC. Short-circuit and: if TOS is falsy, leave on
   stack and jump; else pop and continue.
*/

KOS_OP_HANDLER(JUMP_AND) {
  KosContext *ctx = KosEvalState_Context(es);
  PyObject *val = KosEvalState_StackPopRef(es);
  if (! val)
    return KOS_OP_POST_ERR();
  int res = PyObject_Not(val);
  if (res < 0) {
    Py_DECREF(val);
    return KOS_OP_POST_ERR();
  }
  if (res) {
    if (KosEvalState_StackPushRef(es, val) < 0) {
      Py_DECREF(val);
      return KOS_OP_POST_ERR();
    }
    Py_DECREF(val);
    CURRENT_PC(ctx) = KosOp_ARG(insn);
  } else {
    Py_DECREF(val);
  }
  (void) out_ev;
  return KOS_OP_RUN();
}
