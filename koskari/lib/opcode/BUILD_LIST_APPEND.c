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
   BUILD_LIST_APPEND: arg = count of values.
   Stack [..., list, v_0, ..., v_{n-1}] with the values on top of the list.
   Append the n values to list in place; pop them; leave list on stack.
   TypeError when the list slot is not a list.
*/

KOS_OP_HANDLER(BUILD_LIST_APPEND) {
  unsigned opa = KosOp_ARG(insn);
  size_t count = (size_t) opa;

  PyObject *const *ptr = KosEvalState_StackTailPtr(es, count + 1);
  if (unlikely(! ptr))
    RESULT_RAISE();

  PyObject *list = ptr[0];
  if (unlikely(! PyList_Check(list))) {
    PyErr_Format(PyExc_TypeError,
                 "BUILD_LIST_APPEND requires list, not %R", list);
    RESULT_RAISE();
  }

  for (size_t i = 0; i < count; i++) {
    if (unlikely(PyList_Append(list, ptr[i + 1]) < 0))
      RESULT_RAISE();
  }

  KosEvalState_StackDropNoErr(es, count);
  RESULT_NEXT(es);
}


// The end.
