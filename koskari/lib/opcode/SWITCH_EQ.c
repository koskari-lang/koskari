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
  SWITCH_EQ: arg = target PC. Stack [..., left, right] (right = TOS).
  Compare left and right with Py_EQ. On match: pop both operands and
  jump to arg. On mismatch: pop right only and fall through, leaving
  left on the stack for further compares.
*/

KOS_OP_HANDLER(SWITCH_EQ) {
  PyObject *const *tail = KosEvalState_StackTailPtr(es, 2);
  if (unlikely(! tail))
    return KOS_OP_POST_ERR();

  int res = PyObject_RichCompareBool(tail[0], tail[1], Py_EQ);
  if (unlikely(res < 0))
    return KOS_OP_POST_ERR();

  if (res) {
    KosEvalState_StackDropNoErr(es, 2);
    KosEvalState_SET_PC(es, KosOp_ARG(insn));
  } else {
    KosEvalState_StackDropNoErr(es, 1);
  }

  (void) out_ev;
  return KOS_OP_RUN();
}


// The end.
