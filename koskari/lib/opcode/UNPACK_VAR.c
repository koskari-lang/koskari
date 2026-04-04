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
  KosCode *code = CURRENT_CODE(ctx);
  KosExtra row = KosCode_GetExtra(code, KosOp_ARG(insn));
  uint32_t name0_idx = row.a0;
  uint32_t name1_idx = row.a1;
  (void) out_ev;

  PyObject *name0 = KosCode_GetConstant((PyObject *) code, name0_idx);
  if (! name0)
    return KOS_OP_POST_ERR();

  PyObject *name1 = KosCode_GetConstant((PyObject *) code, name1_idx);
  if (! name1)
    return KOS_OP_POST_ERR();

  PyObject *raw = KosStackBlock_PopRef(&CURRENT_STACK(ctx));
  if (! raw)
    return KOS_OP_POST_ERR();

  PyObject *seq = PySequence_Fast(raw, NULL);
  Py_DECREF(raw);
  if (! seq)
    return KOS_OP_POST_ERR();

  Py_ssize_t slen = PySequence_Fast_GET_SIZE(seq);
  if (slen != 2) {
    Py_DECREF(seq);
    PyErr_Format(PyExc_ValueError,
                 "UNPACK_VAR expected sequence of length 2, got %zd",
                 slen);
    return KOS_OP_POST_ERR();
  }

  if (frame_ns_store(ctx, name0, PySequence_Fast_GET_ITEM(seq, 0)) ||
      frame_ns_store(ctx, name1, PySequence_Fast_GET_ITEM(seq, 1))) {
    Py_DECREF(seq);
    return KOS_OP_POST_ERR();
  }

  Py_DECREF(seq);
  return KOS_OP_RUN();
}


// The end.
