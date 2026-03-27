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
   OBJECT_FORMAT: arg = 1 or 2.
   arg=1 pops object and calls PyObject_Format(obj, NULL).
   arg=2 pops rules then object and calls PyObject_Format(obj, rules).
*/

KOS_OP_HANDLER(OBJECT_FORMAT) {
  int opa = (int) KosOp_ARG(insn);
  PyObject *obj = NULL;
  PyObject *rules = NULL;
  PyObject *res = NULL;

  if (opa != 1 && opa != 2) {
    PyErr_SetString(PyExc_RuntimeError,
                    "OBJECT_FORMAT: only arg 1 or 2 supported");
    return -1;
  }

  if (opa == 2) {
    rules = KosStackBlock_PopRef(&CURRENT_STACK(ctx));
    if (! rules)
      return -1;
  }

  obj = KosStackBlock_PopRef(&CURRENT_STACK(ctx));
  if (! obj) {
    Py_XDECREF(rules);
    return -1;
  }

  res = PyObject_Format(obj, rules);
  Py_DECREF(obj);
  Py_XDECREF(rules);
  if (! res)
    return -1;

  return push_result(ctx, res);
}
