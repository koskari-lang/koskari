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
   FAST_REF: arg = slot index. Push reference to fast slot.
*/

KOS_OP_HANDLER(FAST_REF) {
  PyObject *ns = CURRENT_NS();
  if (! ns) {
    PyErr_SetString(PyExc_RuntimeError, "FAST_REF invoked on NULL namespace");
    return EVAL_RAISE();
  }
  if (! KosNamespace_SupportsFast(ns)) {
    PyErr_Format(PyExc_RuntimeError, "FAST_REF invoked on %R", ns);
    return EVAL_RAISE();
  }
  size_t idx = (size_t) KosOp_ARG(insn);
  PyObject *ref = KosNamespace_FastRef(ns, idx);
  if (! ref)
    return EVAL_RAISE();
  Py_INCREF(ref);
  return kos_op_result_PUSH(es, ref);
}


// The end.
