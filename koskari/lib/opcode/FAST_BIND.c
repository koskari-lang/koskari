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
  KosContext *ctx = KosEvalState_CONTEXT(es);
  (void) out_ev;

  PyObject *ns = CURRENT_NS(ctx);
  if (unlikely(! ns)) {
    PyErr_SetString(PyExc_RuntimeError, "FAST_BIND invoked on NULL namespace");
    return KOS_OP_POST_ERR();
  }

  if (unlikely(! KosNamespace_SupportsFast(ns))) {
    PyErr_Format(PyExc_RuntimeError, "FAST_BIND invoked on %R", ns);
    return KOS_OP_POST_ERR();
  }

  size_t idx = (size_t) KosOp_ARG(insn);
  if (unlikely(idx >= KosNamespace_FAST_COUNT(ns))) {
    PyErr_Format(PyExc_NameError, "Namespace fast index out of bounds, %zu",
                 idx);
    return KOS_OP_POST_ERR();
  }

  PyObject *val = KosEvalState_StackPopRef(es);
  if (unlikely(! val))
    return KOS_OP_POST_ERR();

  int res = KosNamespace_FastBind(ns, idx, val);
  Py_DECREF(val);

  return kos_op_outcome_from_int_step(res);
}
