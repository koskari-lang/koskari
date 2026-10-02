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
  FAST_LIST_APPEND: arg = slot index. Pop TOS (value), append to the list
  in the fast slot. Does not push. TypeError when the slot value is not a
  list.
*/
KOS_OP_HANDLER(FAST_LIST_APPEND) {

  PyObject *ns = CURRENT_NS();
  KOS_OP_REQUIRE_NS(ns);

  size_t idx = (size_t) KosOp_ARG(insn);
  PyObject *list, *list_ref;

  /* The list is borrowed from its slot: the append runs no user code,
     and the type error names the type rather than repr the value. */
  KOS_NS_FAST_BORROW(list, list_ref, ns, idx, { RESULT_RAISE(); });

  if (unlikely(! PyList_Check(list))) {
    PyErr_Format(PyExc_TypeError,
                 "#list-append requires a list, not %.200s",
                 Py_TYPE(list)->tp_name);
    Py_XDECREF(list_ref);
    RESULT_RAISE();
  }

  PyObject *value = KosEvalState_StackPopRef(es);
  if (unlikely(! value)) {
    Py_XDECREF(list_ref);
    RESULT_RAISE();
  }

  int rc = PyList_Append(list, value);
  Py_XDECREF(list_ref);
  Py_DECREF(value);
  if (unlikely(rc < 0))
    RESULT_RAISE();
  RESULT_NEXT(es);
}


// The end.
