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
  UNPACK_VAR: arg = extra index. Schema VAR, VAR for two cpool name indices.
  Pop TOS, materialize as fast sequence, require len == 2, store seq[0] and
  seq[1] into the two names (VAR_STORE semantics).
*/
KOS_OP_HANDLER(UNPACK_VAR) {
  KOS_SYNC_PC(ctx, insn);
  const KosExtra *row = KosOp_ROW(insn);
  PyObject *name0 = KosExtra_OBJ(row, 0);
  PyObject *name1 = KosExtra_OBJ(row, 1);

  PyObject *seq = KosEvalState_PopUnpackSequence(ctx, 2, 1, "UNPACK_VAR");
  if (unlikely(! seq))
    RESULT_RAISE();

  /* own both items before either store: the first store releases the
     name's old value, and its finalizer may change the list */
  PyObject *val0 = PySequence_Fast_GET_ITEM(seq, 0);
  PyObject *val1 = PySequence_Fast_GET_ITEM(seq, 1);
  Py_INCREF(val0);
  Py_INCREF(val1);
  Py_DECREF(seq);

  int res = frame_ns_store(ctx, name0, val0)
      || frame_ns_store(ctx, name1, val1);
  Py_DECREF(val0);
  Py_DECREF(val1);
  if (unlikely(res))
    RESULT_RAISE();

  RESULT_NEXT(ctx);
}


// The end.
