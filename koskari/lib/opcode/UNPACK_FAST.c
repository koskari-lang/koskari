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
  KOS_SYNC_PC(ctx, insn);
  const KosExtra *row = KosOp_ROW(insn);
  size_t idx0 = (size_t) KosExtra_IMM(row, 0);
  size_t idx1 = (size_t) KosExtra_IMM(row, 1);

  PyObject *ns = CURRENT_NS();
  KOS_OP_REQUIRE_NS(ns);

  if (unlikely(idx0 >= KosNamespace_FAST_COUNT(ns) || idx1 >= KosNamespace_FAST_COUNT(ns))) {
    PyErr_Format(PyExc_NameError,
                 "UNPACK_FAST fast index out of bounds");
    RESULT_RAISE();
  }

  PyObject *seq = KosContext_PopUnpackSequence(ctx, 2, 1, "UNPACK_FAST");
  if (unlikely(! seq))
    RESULT_RAISE();

  /* own both items before either store: the first store releases the
     slot's old value, and its finalizer may change the list */
  PyObject *val0 = PySequence_Fast_GET_ITEM(seq, 0);
  PyObject *val1 = PySequence_Fast_GET_ITEM(seq, 1);
  Py_INCREF(val0);
  Py_INCREF(val1);
  Py_DECREF(seq);

  int res = kos_ns_fast_store(ns, idx0, val0) || kos_ns_fast_store(ns, idx1, val1);
  Py_DECREF(val0);
  Py_DECREF(val1);
  if (unlikely(res))
    RESULT_RAISE();

  RESULT_NEXT(ctx);
}


// The end.
