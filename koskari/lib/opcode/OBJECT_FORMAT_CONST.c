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
   OBJECT_FORMAT_CONST: arg = cpool index of rules string.
   Pops object and calls PyObject_Format(obj, rules_const).
*/

KOS_OP_HANDLER(OBJECT_FORMAT_CONST) {
  PyObject *rules = KosCode_GetConstant((PyObject *) KosEvalState_CurrentCode(es),
                                        KosOp_ARG(insn));
  PyObject *obj;
  PyObject *res;

  if (! rules)
    return EVAL_RAISE();

  obj = KosEvalState_StackPopRef(es);
  if (! obj)
    return EVAL_RAISE();

  res = PyObject_Format(obj, rules);
  Py_DECREF(obj);
  if (! res)
    return EVAL_RAISE();

  return kos_op_result_PUSH(es, res);
}


// The end.
