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
   RAISE: arg = 0 (reraise current exception), 1 (raise TOS: instance or class).
   Set the current exception; control will transfer to a handler by the eval loop.
*/

KOS_OP_HANDLER(RAISE) {
  unsigned n = KosOp_ARG(insn);
  if (n == 0) {
    PyObject *typ = NULL, *val = NULL, *tb = NULL;
    PyErr_GetExcInfo(&typ, &val, &tb);
    if (! typ || typ == Py_None) {
      PyErr_SetString(PyExc_RuntimeError, "No active exception to reraise");
      Py_XDECREF(typ);
      Py_XDECREF(val);
      Py_XDECREF(tb);
      return -1;
    }
    PyErr_Restore(typ, val, tb);
    PyErr_SetExcInfo(NULL, NULL, NULL);

  } else if (n == 1) {
    PyObject *exc = KosStackBlock_PopRef(&CURRENT_STACK(ctx));
    if (! exc) {
      return -1;
    }
    if (PyExceptionInstance_Check(exc)) {
      PyObject *typ = (PyObject *) Py_TYPE(exc);
      PyErr_SetObject(typ, exc);
    } else if (PyExceptionClass_Check(exc)) {
      PyObject *inst = PyObject_CallNoArgs(exc);
      if (inst) {
        PyErr_SetObject(exc, inst);
        Py_DECREF(inst);
      }
      Py_DECREF(exc);
      if (PyErr_Occurred())
        return -1;
    } else {
      Py_DECREF(exc);
      PyErr_SetString(PyExc_TypeError, "exceptions must derive from BaseException");
      return -1;
    }
    Py_DECREF(exc);

  } else {
    PyErr_Format(PyExc_RuntimeError, "RAISE with unsupported arg count %u", n);
    return -1;
  }

  (void) out_ev;
  return 0;
}
