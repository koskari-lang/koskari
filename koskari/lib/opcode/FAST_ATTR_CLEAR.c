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
   FAST_ATTR_CLEAR: arg = extra index. Schema FASTVAR (slot), CONST (attr name).
   delattr(obj, attr) where obj = KosNamespace_FastDeref(ns, slot).
*/

KOS_OP_HANDLER(FAST_ATTR_CLEAR) {
  KosCode *code = CURRENT_CODE(ctx);
  KosExtra row = KosCode_GetExtra(code, KosOp_ARG(insn));
  PyObject *ns = CURRENT_NS(ctx);
  if (! ns) {
    PyErr_SetString(PyExc_RuntimeError, "FAST_ATTR_CLEAR invoked on NULL namespace");
    return -1;
  }
  if (! KosNamespace_SupportsFast(ns)) {
    PyErr_Format(PyExc_RuntimeError, "FAST_ATTR_CLEAR invoked on %R", ns);
    return -1;
  }
  PyObject *attr_name = KosCode_GetConstant((PyObject *) code, row.a1);
  if (! attr_name)
    return -1;
  size_t idx = (size_t) row.a0;
  PyObject *obj = KosNamespace_FastDeref(ns, idx);
  if (! obj)
    return -1;
  int res = PyObject_DelAttr(obj, attr_name);
  Py_DECREF(obj);
  if (res < 0)
    return -1;
  (void) out_ev;
  return 0;
}
