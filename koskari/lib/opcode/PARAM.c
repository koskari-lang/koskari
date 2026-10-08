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
  PARAM: arg = extra index. Schema CONST (parameter name), COUNT (0 or
  1 stacked defaults).
  Stack [] -> [value] at count 0, [default] -> [value] at count 1.
  Resolves the enclosing parameterize extent, then the root scope. On a
  miss the stacked default answers, or NameError when there is none.
  Creates nothing in either store.
*/
KOS_OP_HANDLER(PARAM) {
  KOS_SYNC_PC(es, insn);
  const KosExtra *row = KosOp_ROW(insn);

  PyObject *name = KosExtra_OBJ(row, 0);

  PyObject *cell = kos_param_lookup(KosEvalState_CONTEXT(es), name);
  PyObject *value = cell ? KosSymbolicRef_VALUE(cell) : NULL;

  if (value) {
    Py_INCREF(value);
    if (KosExtra_IMM(row, 1) && unlikely(KosEvalState_StackDrop1(es) < 0)) {
      Py_DECREF(value);
      RESULT_RAISE();
    }
    RESULT_STEAL(es, value);
  }

  if (likely(KosExtra_IMM(row, 1))) {
    // the stacked default is already in result position
    RESULT_NEXT(es);

  } else {
    PyErr_Format(
      PyExc_NameError,
      "Param %S has no binding and no default",
      name);
    RESULT_RAISE();
  }
}


// The end.
