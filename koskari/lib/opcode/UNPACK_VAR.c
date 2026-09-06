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
KOS_OP_HANDLER_COLD(UNPACK_VAR) {
  const KosExtra *row = KosOp_ROW(insn);
  PyObject *name0 = KosExtra_OBJ(row, 0);
  PyObject *name1 = KosExtra_OBJ(row, 1);

  PyObject *seq = KosEvalState_PopUnpackSequence(es, 2, 1, "UNPACK_VAR");
  if (unlikely(! seq))
    RESULT_RAISE();

  if (unlikely(
      frame_ns_store(es, name0, PySequence_Fast_GET_ITEM(seq, 0)) ||
      frame_ns_store(es, name1, PySequence_Fast_GET_ITEM(seq, 1)))) {
    Py_DECREF(seq);
    RESULT_RAISE();
  }

  Py_DECREF(seq);
  RESULT_NEXT(es);
}


// The end.
