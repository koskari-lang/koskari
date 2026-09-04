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
   BUILD_LIST_EXTEND_CONST: arg = constant index of a sequence (typically a
   pooled tuple of constant elements). Extend the list on TOS in place with
   that sequence's elements; the list is left on the stack.

   The list on TOS was minted by an earlier BUILD_LIST* op, so it is uniquely
   owned and safe to mutate. TypeError when the TOS slot is not a list.
*/

KOS_OP_HANDLER(BUILD_LIST_EXTEND_CONST) {
  PyObject *seq = KosOp_OBJ(insn);

  PyObject *const *ptr = KosEvalState_StackTailPtr(es, 1);
  if (unlikely(! ptr))
    RESULT_RAISE();

  PyObject *list = ptr[0];
  if (unlikely(! PyList_Check(list))) {
    PyErr_Format(PyExc_TypeError,
                 "BUILD_LIST_EXTEND_CONST requires list, not %R", list);
    RESULT_RAISE();
  }

  if (likely(PyTuple_CheckExact(seq))) {
    Py_ssize_t n = PyTuple_GET_SIZE(seq);
    for (Py_ssize_t i = 0; i < n; i++) {
      if (unlikely(PyList_Append(list, PyTuple_GET_ITEM(seq, i)) < 0))
        RESULT_RAISE();
    }
    RESULT_NEXT(es);
  }

  {
    PyObject *it = PyObject_GetIter(seq);
    if (unlikely(! it))
      RESULT_RAISE();

    PyObject *item;
    while ((item = PyIter_Next(it))) {
      int rc = PyList_Append(list, item);
      Py_DECREF(item);
      if (unlikely(rc < 0)) {
        Py_DECREF(it);
        RESULT_RAISE();
      }
    }
    Py_DECREF(it);
    if (unlikely(PyErr_Occurred()))
      RESULT_RAISE();
  }

  RESULT_NEXT(es);
}


// The end.
