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
  MATCH_APPEND: TOS popped and appended to the items list at field
  0, copied first when the entry does not hold it alone. Row depth.
  Every REPEAT pass that goes on ends here, ahead of its back-edge, so
  this is the loop's cooperation point.
*/
KOS_OP_HANDLER(MATCH_APPEND) {
  KOS_SYNC_PC(es, insn);
  const KosExtra *row = KosOp_ROW(insn);
  (void) kos_op_delivery_;

  /* the items list owned by its entry alone: the entry's list when
     nothing else holds it, else a copy stored at the entry, so a cloned
     segment keeps the items it had when the clone was taken */
  size_t under = KosExtra_IMM(row, 0);
  PyObject *list = KosEvalState_StackPeekUnder(es, under);
  if (unlikely(! list))
    RESULT_RAISE();
  if (unlikely(! PyList_Check(list))) {
    PyErr_Format(PyExc_TypeError,
                 "match append requires a list, not %.200s",
                 Py_TYPE(list)->tp_name);
    RESULT_RAISE();
  }
  if (Py_REFCNT(list) > 1) {
    list = PyList_GetSlice(list, 0, PyList_GET_SIZE(list));
    if (unlikely(! list))
      RESULT_RAISE();
    if (unlikely(KosEvalState_StackSetUnder(es, under, list) < 0))
      RESULT_RAISE();
  }

  PyObject *value = KosEvalState_StackPopRef(es);
  if (unlikely(! value))
    RESULT_RAISE();
  int rc = PyList_Append(list, value);
  Py_DECREF(value);
  if (unlikely(rc < 0))
    RESULT_RAISE();
  RESULT_NEXT_POLL(es);
}


// The end.
