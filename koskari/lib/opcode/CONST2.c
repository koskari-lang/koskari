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
  CONST2: arg = extra index. Schema CONST, CONST. Push two code object
  constants in row order. Both constants resolve before either push, so
  a bad index raises with nothing pushed.

  The pool object is resolved onto the record at finalize.
*/
KOS_OP_HANDLER(CONST2) {
  const KosExtra *row = KosOp_ROW(insn);

  PyObject *a = KosExtra_OBJ(row, 0);

  PyObject *b = KosExtra_OBJ(row, 1);

  if (unlikely(KosEvalState_StackPushRef(es, a) < 0))
    RESULT_RAISE();

  RESULT_PUSH(es, b);
}


// The end.
