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
   EXCEPTION_MATCH: arg = number of exception types on stack. Pop that many
   types; if the current exception matches any of them, push True, else False.
*/

KOS_OP_HANDLER(EXCEPTION_MATCH) {
  int opa = (int) KosOp_ARG(insn);
  if (opa < 1) {
    PyErr_Format(PyExc_RuntimeError, "EXCEPTION_MATCH with non-positive"
                 " count, %i", opa);
    return -1;
  }

  PyObject *ret = Py_False;
  PyObject *typ = NULL, *val = NULL, *tb = NULL;
  PyErr_GetExcInfo(&typ, &val, &tb);

  if (! typ) {
    if (KosStackBlock_Drop(&CURRENT_STACK(ctx), (size_t) opa) < 0)
      return -1;
    Py_INCREF(ret);
    return push_result(ctx, ret);
  }

  PyObject *const *checks = KosStackBlock_TailPtr(CURRENT_STACK(ctx),
                                                  (size_t) opa);
  if (! checks) {
    Py_DECREF(typ);
    Py_XDECREF(val);
    Py_XDECREF(tb);
    return -1;
  }

  for (int i = 0; i < opa; i++) {
    if (PyErr_GivenExceptionMatches(typ, checks[i])) {
      ret = Py_True;
      break;
    }
  }

  KosStackBlock_DropNoErr(&CURRENT_STACK(ctx), (size_t) opa);

  Py_DECREF(typ);
  Py_XDECREF(val);
  Py_XDECREF(tb);

  Py_INCREF(ret);
  return push_result(ctx, ret);
}
