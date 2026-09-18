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
   VAR_ATTR_TAKE: arg = extra index. Schema VAR (obj name), CONST (attr).
   getattr then delattr on obj = frame_ns_deref(var_name); push value.
*/

KOS_OP_HANDLER(VAR_ATTR_TAKE) {
  const KosExtra *row = KosOp_ROW(insn);

  PyObject *var_name = KosExtra_OBJ(row, 0);
  PyObject *attr_name = KosExtra_OBJ(row, 1);

  PyObject *obj = frame_ns_deref(es, var_name);
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
