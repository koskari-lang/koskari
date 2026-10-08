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
  BUILD_LIST: arg = count of elements.
  Stack: [..., v_0, ..., v_{n-1}] -> [..., list].
  Pop n elements into a new list and push it.
*/
KOS_OP_HANDLER(BUILD_LIST) {
  KOS_SYNC_PC(es, insn);
  unsigned opa = KosOp_ARG(insn);
  size_t count = (size_t) opa;

  PyObject *built = PyList_New((Py_ssize_t) count);
  if (unlikely(! built))
    RESULT_RAISE();

  if (count) {
    if (unlikely(KosEvalState_StackPopTailPtr(
          es, count, &PyList_GET_ITEM(built, 0)) < 0)) {
      Py_DECREF(built);
      RESULT_RAISE();
    }
  }

  RESULT_STEAL(es, built);
}


// The end.
