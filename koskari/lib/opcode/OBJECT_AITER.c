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
   OBJECT_AITER: arg unused. Pop TOS (async iterable), call __aiter__, push
   the async iterator or await it.
*/

KOS_OP_HANDLER(OBJECT_AITER) {
  PyObject *val = KosEvalState_StackPopRef(es);
  if (! val)
    return KOS_OP_POST_ERR();

  PyObject *res = NULL;
  PyTypeObject *type = Py_TYPE(val);
  if (type->tp_as_async && type->tp_as_async->am_aiter)
    res = type->tp_as_async->am_aiter(val);
  if (! res) {
    PyErr_Format(PyExc_RuntimeError, "cannot await for elements of %R", val);
    Py_DECREF(val);
    return KOS_OP_POST_ERR();
  }
  Py_DECREF(val);
  (void) insn;
  return kos_op_outcome_from_int_step(push_result(es, res));
}
