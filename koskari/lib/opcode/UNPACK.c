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
   UNPACK: arg = n (COUNT). Pop TOS, materialize as fast sequence, require
   len == n, push n elements so index 0 is TOS (push indices n-1 .. 0).
*/

KOS_OP_HANDLER(UNPACK) {
  Py_ssize_t n = (Py_ssize_t) KosOp_ARG(insn);
  (void) out_ev;

  PyObject *raw = KosEvalState_StackPopRef(es);
  if (! raw)
    return KOS_OP_POST_ERR();

  PyObject *seq = PySequence_Fast(raw, NULL);
  Py_DECREF(raw);
  if (! seq)
    return KOS_OP_POST_ERR();

  Py_ssize_t slen = PySequence_Fast_GET_SIZE(seq);
  if (slen != n) {
    Py_DECREF(seq);
    PyErr_Format(PyExc_ValueError,
                 "UNPACK expected sequence of length %zd, got %zd",
                 n, slen);
    return KOS_OP_POST_ERR();
  }

  Py_ssize_t j = n;
  while (j--) {
    PyObject *item = PySequence_Fast_GET_ITEM(seq, j);

    if (KosEvalState_StackPushRef(es, item) < 0) {
      Py_DECREF(seq);
      return KOS_OP_POST_ERR();
    }
  }

  Py_DECREF(seq);
  return KOS_OP_RUN();
}


// The end.
