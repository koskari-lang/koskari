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
  CONS: arg = count. Stack (bottom to top) = count items, the last of
  them the tail. Build (item_0 . (item_1 . ... . item_{count-1})) with
  KosPair_ConsFromPtr over the borrowed window, drop the count items, and
  push the result; count 0 pushes nil.
*/
KOS_OP_HANDLER(CONS) {
  KOS_SYNC_PC(es, insn);
  size_t count = (size_t) KosOp_ARG(insn);

  if (count == 0)
    RESULT_PUSH(es, KosNil);

  PyObject *const *ptr = KosEvalState_StackTailPtr(es, count);
  if (! ptr)
    RESULT_RAISE();

  PyObject *result = KosPair_ConsFromPtr(ptr, (Py_ssize_t) count);
  if (! result)
    RESULT_RAISE();

  if (KosEvalState_StackDrop(es, count) < 0) {
    Py_DECREF(result);
    RESULT_RAISE();
  }

  RESULT_STEAL(es, result);
}


// The end.
