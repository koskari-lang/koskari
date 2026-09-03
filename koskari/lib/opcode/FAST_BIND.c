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
   FAST_BIND: arg = slot index. Bind name to fast slot.
*/

KOS_OP_HANDLER(FAST_BIND) {

  PyObject *ns = CURRENT_NS();
  KOS_OP_REQUIRE_NS(ns);

  size_t idx = (size_t) KosOp_ARG(insn);
  if (unlikely(idx >= KosNamespace_FAST_COUNT(ns))) {
    PyErr_Format(PyExc_NameError, "Namespace fast index out of bounds, %zu",
                 idx);
    RESULT_RAISE();
  }

  PyObject *val = KosEvalState_StackPopRef(es);
  if (unlikely(! val))
    RESULT_RAISE();

  int res = KosNamespace_FastBind(ns, idx, val);
  Py_DECREF(val);

  if (unlikely(res < 0))
    RESULT_RAISE();
  RESULT_NEXT(es);
}


// The end.
