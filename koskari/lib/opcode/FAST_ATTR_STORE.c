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
   FAST_ATTR_STORE: arg = extra index. Schema FASTVAR (slot), CONST (attr name).
   setattr(obj, attr, TOS) where obj = KosNamespace_FastDeref(ns, slot). Pop val.
*/

KOS_OP_HANDLER(FAST_ATTR_STORE) {
  KosCode *code = KosEvalState_CurrentCode(es);
  KosExtra row = KosCode_GetExtra(code, KosOp_ARG(insn));
  PyObject *ns = CURRENT_NS();
  if (! ns) {
    PyErr_SetString(PyExc_RuntimeError, "FAST_ATTR_STORE invoked on NULL namespace");
    return KOS_OP_POST_ERR();
  }
  if (! KosNamespace_SupportsFast(ns)) {
    PyErr_Format(PyExc_RuntimeError, "FAST_ATTR_STORE invoked on %R", ns);
    return KOS_OP_POST_ERR();
  }
  PyObject *attr_name = KosCode_GetConstant((PyObject *) code, row.a1);
  if (! attr_name)
    return KOS_OP_POST_ERR();
  PyObject *val = KosEvalState_StackPopRef(es);
  if (! val)
    return KOS_OP_POST_ERR();
  size_t idx = (size_t) row.a0;
  PyObject *obj = KosNamespace_FastDeref(ns, idx);
  if (! obj) {
    Py_DECREF(val);
    return KOS_OP_POST_ERR();
  }
  int res = PyObject_SetAttr(obj, attr_name, val);
  Py_DECREF(obj);
  Py_DECREF(val);
  if (res < 0)
    return KOS_OP_POST_ERR();
  (void) out_ev;
  return KOS_OP_RUN();
}
