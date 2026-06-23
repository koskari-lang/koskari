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
   FAST_STORE: arg = slot index. Store TOS into fast slot.
*/

KOS_OP_HANDLER(FAST_STORE) {

  PyObject *ns = CURRENT_NS();
  if (! ns) {
    PyErr_SetString(PyExc_RuntimeError, "FAST_STORE invoked on NULL namespace");
    return EVAL_RAISE();
  }
  if (! KosNamespace_SupportsFast(ns)) {
    PyErr_Format(PyExc_RuntimeError, "FAST_STORE invoked on %R", ns);
    return EVAL_RAISE();
  }

  PyObject *val = KosEvalState_StackPopRef(es);
  if (! val)
    return EVAL_RAISE();

  size_t idx = (size_t) KosOp_ARG(insn);
  int res = KosNamespace_FastStore(ns, idx, val);
  Py_DECREF(val);

  if (res < 0)
    return EVAL_RAISE();

  return EVAL_NORMAL();
}


// The end.
