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
KOS_OP_HANDLER(VAR_LIST_APPEND) {
  KOS_SYNC_PC(ctx, insn);

  PyObject *name = KosOp_OBJ(insn);

  PyObject *list = frame_ns_deref(ctx, name);
  if (unlikely(! list))
    RESULT_RAISE();

  /* the type error names the type rather than repr the value, as the
     FAST form does, so the two report one fault one way */
  if (unlikely(! PyList_Check(list))) {
    PyErr_Format(PyExc_TypeError,
                 "#list-append requires a list, not %.200s",
                 Py_TYPE(list)->tp_name);
    Py_DECREF(list);
    RESULT_RAISE();
  }

  PyObject *value = KosEvalState_StackPopRef(ctx);
  if (unlikely(! value)) {
    Py_DECREF(list);
    RESULT_RAISE();
  }

  if (unlikely(PyList_Append(list, value) < 0)) {
    Py_DECREF(list);
    Py_DECREF(value);
    RESULT_RAISE();
  }

  Py_DECREF(list);
  Py_DECREF(value);
  RESULT_NEXT(ctx);
}


// The end.
