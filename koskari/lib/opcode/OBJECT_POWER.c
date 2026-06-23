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
   OBJECT_POWER: arg = 2 (base, exp) or 3 (base, exp, mod). Pop operands,
   compute pow(base, exp[, mod]), push result or await it.
*/

KOS_OP_HANDLER(OBJECT_POWER) {
  int opa = (int) KosOp_ARG(insn);
  if (opa < 2 || opa > 3) {
    PyErr_Format(PyExc_RuntimeError,
                 "OBJECT_POWER requires 2 or 3 arguments, got %i", opa);
    return EVAL_RAISE();
  }

  PyObject *base = NULL, *exp = NULL, *mod = Py_None;

  if (opa == 3) {
    mod = KosEvalState_StackPopRef(es);
    if (! mod)
      return EVAL_RAISE();
  }

  exp = KosEvalState_StackPopRef(es);
  if (! exp) {
    if (opa == 3)
      Py_DECREF(mod);
    return EVAL_RAISE();
  }

  base = KosEvalState_StackPopRef(es);
  if (! base) {
    Py_DECREF(exp);
    if (opa == 3)
      Py_DECREF(mod);
    return EVAL_RAISE();
  }

  PyObject *val = PyNumber_Power(base, exp, mod);
  Py_DECREF(base);
  Py_DECREF(exp);
  if (opa == 3)
    Py_DECREF(mod);
  if (! val)
    return EVAL_RAISE();
  return kos_op_result_PUSH(es, val);
}


// The end.
