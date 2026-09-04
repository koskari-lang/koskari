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
   FAST_ATTR_TAKE: arg = extra index. Schema FASTVAR (slot), CONST (attr).
   getattr then delattr on obj = KosNamespace_FastDeref(ns, slot); push val.
*/

KOS_OP_HANDLER(FAST_ATTR_TAKE) {
  const KosExtra *row = KosOp_ROW(insn);

  PyObject *ns = CURRENT_NS();
  KOS_OP_REQUIRE_NS(ns);

  PyObject *attr_name = KosExtra_OBJ(row, 1);
  size_t idx = (size_t) KosExtra_IMM(row, 0);

  PyObject *obj = kos_ns_fast_deref(ns, idx);
  if (! obj)
    RESULT_RAISE();

  PyObject *val = PyObject_GetAttr(obj, attr_name);
  if (! val) {
    Py_DECREF(obj);
    RESULT_RAISE();
  }

  int res = PyObject_DelAttr(obj, attr_name);
  Py_DECREF(obj);
  if (res < 0) {
    Py_DECREF(val);
    RESULT_RAISE();
  }

  RESULT_STEAL(es, val);
}


// The end.
