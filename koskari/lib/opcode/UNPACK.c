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
  KOS_SYNC_PC(ctx, insn);
  Py_ssize_t n = (Py_ssize_t) KosOp_ARG(insn);

  PyObject *seq = KosContext_PopUnpackSequence(ctx, n, 1, "UNPACK");
  if (unlikely(! seq))
    RESULT_RAISE();

  Py_ssize_t j = n;
  while (j--) {
    PyObject *item = PySequence_Fast_GET_ITEM(seq, j);

    if (unlikely(KosContext_StackPushRef(ctx, item) < 0)) {
      Py_DECREF(seq);
      RESULT_RAISE();
    }
  }

  Py_DECREF(seq);
  RESULT_NEXT(ctx);
}


// The end.
