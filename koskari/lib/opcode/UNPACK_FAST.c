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
   UNPACK_FAST: arg = extra index. Schema FASTVAR, FASTVAR for two slot indices.
   Pop TOS, materialize as fast sequence, require len == 2, FastStore seq[0]
   and seq[1].
*/

KOS_OP_HANDLER(UNPACK_FAST) {
  KosContext *ctx = KosEvalState_Context(es);
  KosCode *code = CURRENT_CODE(ctx);
  KosExtra row = KosCode_GetExtra(code, KosOp_ARG(insn));
  size_t idx0 = (size_t) row.a0;
  size_t idx1 = (size_t) row.a1;
  (void) out_ev;

  PyObject *ns = CURRENT_NS(ctx);
  if (! ns) {
    PyErr_SetString(PyExc_RuntimeError,
                    "UNPACK_FAST invoked on NULL namespace");
    return KOS_OP_POST_ERR();
  }

  if (! KosNamespace_SupportsFast(ns)) {
    PyErr_Format(PyExc_RuntimeError, "UNPACK_FAST invoked on %R", ns);
    return KOS_OP_POST_ERR();
  }

  if (idx0 >= KosNamespace_FAST_COUNT(ns) || idx1 >= KosNamespace_FAST_COUNT(ns)) {
    PyErr_Format(PyExc_NameError,
                 "UNPACK_FAST fast index out of bounds");
    return KOS_OP_POST_ERR();
  }

  PyObject *raw = KosEvalState_StackPopRef(es);
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
                 "UNPACK_FAST expected sequence of length 2, got %zd",
                 slen);
    return KOS_OP_POST_ERR();
  }

  if (KosNamespace_FastStore(ns, idx0, PySequence_Fast_GET_ITEM(seq, 0)) ||
      KosNamespace_FastStore(ns, idx1, PySequence_Fast_GET_ITEM(seq, 1))) {
    Py_DECREF(seq);
    return KOS_OP_POST_ERR();
  }

  Py_DECREF(seq);
  return KOS_OP_RUN();
}


// The end.
