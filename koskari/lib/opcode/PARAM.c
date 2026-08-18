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
  KosCode *code = KosEvalState_CurrentCode(es);
  KosExtra row = KosCode_GetExtra(code, KosOp_ARG(insn));

  PyObject *name = KosEvalState_CONST(es, row.a0);
  if (unlikely(! name))
    return EVAL_RAISE();

  PyObject *cell = kos_param_lookup(KosEvalState_CONTEXT(es), name);
  PyObject *value = cell ? KosSymbolicRef_VALUE(cell) : NULL;

  if (value) {
    Py_INCREF(value);
    if (row.a1 && unlikely(KosEvalState_StackDrop1(es) < 0)) {
      Py_DECREF(value);
      return EVAL_RAISE();
    }
    return kos_op_result_PUSH(es, value);
  }

  if (likely(row.a1)) {
    // the stacked default is already in result position
    return EVAL_NORMAL();

  } else {
    PyErr_Format(
      PyExc_NameError,
      "Param %S has no binding and no default",
      name);
    return EVAL_RAISE();
  }
}


// The end.
