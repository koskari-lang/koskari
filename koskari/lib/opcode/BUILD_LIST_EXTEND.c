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
  BUILD_LIST_EXTEND: stack [..., list, iterable] with iterable on TOS.
  Extend list in place from iterable; pop iterable; leave list on stack.

  The iterable may be any object the list API accepts (sequence or
  iterator); PySequence_InPlaceConcat drives list.extend semantics. Used for
  a *: splat segment of a list literal. TypeError when the list slot is not
  a list.
*/
KOS_OP_HANDLER(BUILD_LIST_EXTEND) {

  PyObject *iterable = KosEvalState_StackPopRef(es);
  if (unlikely(! iterable))
    RESULT_RAISE();

  PyObject *const *ptr = KosEvalState_StackTailPtr(es, 1);
  if (unlikely(! ptr)) {
    Py_DECREF(iterable);
    RESULT_RAISE();
  }

  PyObject *list = ptr[0];
  if (unlikely(! PyList_Check(list))) {
    Py_DECREF(iterable);
    PyErr_Format(PyExc_TypeError,
                 "BUILD_LIST_EXTEND requires list, not %R", list);
    RESULT_RAISE();
  }

  PyObject *result = PySequence_InPlaceConcat(list, iterable);
  Py_DECREF(iterable);
  if (unlikely(! result))
    RESULT_RAISE();
  Py_DECREF(result);

  RESULT_NEXT(es);
}


// The end.
