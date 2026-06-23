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

KOS_OP_HANDLER(UNPACK_STAR) {
  KosCode *code = KosEvalState_CurrentCode(es);
  KosExtra row = KosCode_GetExtra(code, KosOp_ARG(insn));
  unsigned left = row.a0;
  unsigned right = row.a1;

  if (left + right == 0) {
    PyErr_SetString(PyExc_ValueError,
                    "UNPACK_STAR requires left + right > 0");
    return EVAL_RAISE();
  }

  PyObject *raw = KosEvalState_StackPopRef(es);
  if (! raw)
    return EVAL_RAISE();

  PyObject *seq = PySequence_Fast(raw, NULL);
  Py_DECREF(raw);
  if (! seq)
    return EVAL_RAISE();

  Py_ssize_t slen = PySequence_Fast_GET_SIZE(seq);
  if (slen < (Py_ssize_t) (left + right)) {
    Py_DECREF(seq);
    PyErr_Format(PyExc_ValueError,
                 "UNPACK_STAR needs at least %u elements, got %zd",
                 left + right, slen);
    return EVAL_RAISE();
  }

  Py_ssize_t k;
  /* suffix: deepest first (len-1 .. len-right) */
  for (k = slen - 1; k >= slen - (Py_ssize_t) right; k--) {
    PyObject *item = PySequence_Fast_GET_ITEM(seq, k);
    if (KosEvalState_StackPushRef(es, item) < 0) {
      Py_DECREF(seq);
      return EVAL_RAISE();
    }
  }

  PyObject *mid_slice = PySequence_GetSlice(seq, (Py_ssize_t) left,
                                            slen - (Py_ssize_t) right);
  if (! mid_slice) {
    Py_DECREF(seq);
    return EVAL_RAISE();
  }

  PyObject *middle = PySequence_Tuple(mid_slice);
  Py_DECREF(mid_slice);
  if (! middle) {
    Py_DECREF(seq);
    return EVAL_RAISE();
  }

  if (KosEvalState_StackPUSH(es, middle) < 0) {
    Py_DECREF(seq);
    return EVAL_RAISE();
  }

  for (k = (Py_ssize_t) left - 1; k >= 0; k--) {
    PyObject *item = PySequence_Fast_GET_ITEM(seq, k);
    if (KosEvalState_StackPushRef(es, item) < 0) {
      Py_DECREF(seq);
      return EVAL_RAISE();
    }
  }

  Py_DECREF(seq);
  return EVAL_NORMAL();
}


// The end.
