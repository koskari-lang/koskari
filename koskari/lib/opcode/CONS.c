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
  CONS: arg = count. Stack (bottom to top) = first element ... tail.
  Build (elem_0 . (elem_1 . ... (elem_{count-1} . tail))). Use
  TailPtr, KosPair_ConsFromPtr, then Drop count and push result.
*/
KOS_OP_HANDLER(CONS) {
  size_t count = (size_t) KosOp_ARG(insn);

  if (count == 0)
    RESULT_PUSH(es, KosNil);

  PyObject *const *ptr = KosEvalState_StackTailPtr(es, count);
  if (! ptr)
    RESULT_RAISE();

  PyObject *result = KosPair_ConsFromPtr(ptr, (int) count);
  if (! result)
    RESULT_RAISE();

  if (KosEvalState_StackDrop(es, count) < 0) {
    Py_DECREF(result);
    RESULT_RAISE();
  }

  RESULT_STEAL(es, result);
}


// The end.
