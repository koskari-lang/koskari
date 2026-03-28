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
  realize as list, require len >= left + right and left + right > 0. Push
  suffix scalars (len-1 .. len-right), then middle as tuple, then prefix
  (left-1 .. 0) so first prefix is TOS.
*/

KOS_OP_HANDLER(UNPACK_STAR) {
  KosCode *code = CURRENT_CODE(ctx);
  KosExtra row = KosCode_GetExtra(code, KosOp_ARG(insn));
  unsigned left = row.a0;
  unsigned right = row.a1;
  (void) out_ev;

  if (left + right == 0) {
    PyErr_SetString(PyExc_ValueError,
                    "UNPACK_STAR requires left + right > 0");
    return -1;
  }

  PyObject *raw = KosStackBlock_PopRef(&CURRENT_STACK(ctx));
  if (! raw)
    return -1;

  PyObject *seq = PySequence_List(raw);
  Py_DECREF(raw);
  if (! seq)
    return -1;

  Py_ssize_t slen = PyList_GET_SIZE(seq);
  if (slen < (Py_ssize_t) (left + right)) {
    Py_DECREF(seq);
    PyErr_Format(PyExc_ValueError,
                 "UNPACK_STAR needs at least %u elements, got %zd",
                 left + right, slen);
    return -1;
  }

  Py_ssize_t k;
  /* suffix: deepest first (len-1 .. len-right) */
  for (k = slen - 1; k >= slen - (Py_ssize_t) right; k--) {
    PyObject *item = PyList_GET_ITEM(seq, k);
    if (KosStackBlock_PushRef(&CURRENT_STACK(ctx), item) < 0) {
      Py_DECREF(seq);
      return -1;
    }
  }

  PyObject *mid_slice = PySequence_GetSlice(seq, (Py_ssize_t) left,
                                            slen - (Py_ssize_t) right);
  if (! mid_slice) {
    Py_DECREF(seq);
    return -1;
  }

  PyObject *middle = PySequence_Tuple(mid_slice);
  Py_DECREF(mid_slice);
  if (! middle) {
    Py_DECREF(seq);
    return -1;
  }

  if (KosStackBlock_PUSH(&CURRENT_STACK(ctx), middle) < 0) {
    Py_DECREF(seq);
    return -1;
  }

  for (k = (Py_ssize_t) left - 1; k >= 0; k--) {
    PyObject *item = PyList_GET_ITEM(seq, k);
    if (KosStackBlock_PushRef(&CURRENT_STACK(ctx), item) < 0) {
      Py_DECREF(seq);
      return -1;
    }
  }

  Py_DECREF(seq);
  return 0;
}


// The end.
