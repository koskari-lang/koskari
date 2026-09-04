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
  CONST4: arg = extra index. Schema CONST x4. Push the four constants in
  row order. A constant is a borrowed pool reference, so nothing is
  held across a failed push.
*/
KOS_OP_HANDLER(CONST4) {
  const KosExtra *row = KosOp_ROW(insn);

  PyObject *a = KosExtra_OBJ(row, 0);

  PyObject *b = KosExtra_OBJ(row, 1);

  PyObject *c = KosExtra_OBJ(row, 2);

  PyObject *d = KosExtra_OBJ(row, 3);

  if (unlikely(KosEvalState_StackPushRef(es, a) < 0))
    RESULT_RAISE();

  if (unlikely(KosEvalState_StackPushRef(es, b) < 0))
    RESULT_RAISE();

  if (unlikely(KosEvalState_StackPushRef(es, c) < 0))
    RESULT_RAISE();

  RESULT_PUSH(es, d);
}


// The end.
