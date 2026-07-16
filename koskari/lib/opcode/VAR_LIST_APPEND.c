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
   VAR_LIST_APPEND: arg = constant index (name). Pop TOS (value), append to
   the list bound to name. Does not push. TypeError when the binding is not
   a list.
*/

KOS_OP_HANDLER_COLD(VAR_LIST_APPEND) {

  PyObject *name = get_name_from_insn(es, insn);
  if (unlikely(! name))
    return EVAL_RAISE();

  PyObject *list = frame_ns_deref(es, name);
  if (unlikely(! list))
    return EVAL_RAISE();

  if (unlikely(! PyList_Check(list))) {
    PyErr_Format(PyExc_TypeError,
                 "VAR_LIST_APPEND requires list, not %R", list);
    Py_DECREF(list);
    return EVAL_RAISE();
  }

  PyObject *value = KosEvalState_StackPopRef(es);
  if (unlikely(! value)) {
    Py_DECREF(list);
    return EVAL_RAISE();
  }

  if (unlikely(PyList_Append(list, value) < 0)) {
    Py_DECREF(list);
    Py_DECREF(value);
    return EVAL_RAISE();
  }

  Py_DECREF(list);
  Py_DECREF(value);
  return EVAL_NORMAL();
}


// The end.
