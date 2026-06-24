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
   BUILD_LIST_APPEND: stack [..., list, value] with value on TOS.
   Append value to list in place; pop value; leave list on stack.
   TypeError when the list slot is not a list.
*/

KOS_OP_HANDLER(BUILD_LIST_APPEND) {

  PyObject *value = KosEvalState_StackPopRef(es);
  if (unlikely(! value))
    return EVAL_RAISE();

  {
    PyObject *const *ptr = KosEvalState_StackTailPtr(es, 1);
    if (unlikely(! ptr)) {
      Py_DECREF(value);
      return EVAL_RAISE();
    }

    PyObject *list = ptr[0];
    if (unlikely(! PyList_Check(list))) {
      Py_DECREF(value);
      PyErr_Format(PyExc_TypeError,
                   "BUILD_LIST_APPEND requires list, not %R", list);
      return EVAL_RAISE();
    }

    if (unlikely(PyList_Append(list, value) < 0)) {
      Py_DECREF(value);
      return EVAL_RAISE();
    }
  }

  Py_DECREF(value);
  return EVAL_NORMAL();
}


// The end.
