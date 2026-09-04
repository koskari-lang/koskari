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
  UNPACK_STAR: arg = extra index. Schema COUNT left, COUNT right. Pop TOS,
  materialize as fast sequence, require len >= left + right and left + right
  > 0. Push suffix scalars (len-1 .. len-right), then middle as tuple, then
  prefix (left-1 .. 0) so first prefix is TOS.
*/
KOS_OP_HANDLER_COLD(UNPACK_STAR) {
  const KosExtra *row = KosOp_ROW(insn);
  unsigned left = KosExtra_IMM(row, 0);
  unsigned right = KosExtra_IMM(row, 1);

  if (unlikely(left + right == 0)) {
    PyErr_SetString(PyExc_ValueError,
                    "UNPACK_STAR requires left + right > 0");
    RESULT_RAISE();
  }

  PyObject *raw = KosEvalState_StackPopRef(es);
  if (unlikely(! raw))
    RESULT_RAISE();

  PyObject *seq = PySequence_Fast(raw, NULL);
  Py_DECREF(raw);
  if (unlikely(! seq))
    RESULT_RAISE();

  Py_ssize_t slen = PySequence_Fast_GET_SIZE(seq);
  if (unlikely(slen < (Py_ssize_t) (left + right))) {
    Py_DECREF(seq);
    PyErr_Format(PyExc_ValueError,
                 "UNPACK_STAR needs at least %u elements, got %zd",
                 left + right, slen);
    RESULT_RAISE();
  }

  Py_ssize_t k;
  /* suffix: deepest first (len-1 .. len-right) */
  for (k = slen - 1; k >= slen - (Py_ssize_t) right; k--) {
    PyObject *item = PySequence_Fast_GET_ITEM(seq, k);
    if (unlikely(KosEvalState_StackPushRef(es, item) < 0)) {
      Py_DECREF(seq);
      RESULT_RAISE();
    }
  }

  PyObject *mid_slice = PySequence_GetSlice(seq, (Py_ssize_t) left,
                                            slen - (Py_ssize_t) right);
  if (unlikely(! mid_slice)) {
    Py_DECREF(seq);
    RESULT_RAISE();
  }

  PyObject *middle = PySequence_Tuple(mid_slice);
  Py_DECREF(mid_slice);
  if (unlikely(! middle)) {
    Py_DECREF(seq);
    RESULT_RAISE();
  }

  if (unlikely(KosEvalState_StackPUSH(es, middle) < 0)) {
    Py_DECREF(seq);
    RESULT_RAISE();
  }

  for (k = (Py_ssize_t) left - 1; k >= 0; k--) {
    PyObject *item = PySequence_Fast_GET_ITEM(seq, k);
    if (unlikely(KosEvalState_StackPushRef(es, item) < 0)) {
      Py_DECREF(seq);
      RESULT_RAISE();
    }
  }

  Py_DECREF(seq);
  RESULT_NEXT(es);
}


// The end.
