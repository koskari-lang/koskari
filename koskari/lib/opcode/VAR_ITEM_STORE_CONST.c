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
   VAR_ITEM_STORE_CONST: arg = extra index. Schema VAR (obj name), VAR
   (key name), CONST (val). obj[key] = val with both operands resolved
   by name in the frame namespace and the value from the pool; no stack
   effect. The cold twin of FAST_ITEM_STORE_CONST.
*/

KOS_OP_HANDLER_COLD(VAR_ITEM_STORE_CONST) {
  const KosExtra *row = KosOp_ROW(insn);
  PyObject *val = KosExtra_OBJ(row, 2);
  PyObject *obj = frame_ns_deref(es, KosExtra_OBJ(row, 0));
  if (! obj)
    RESULT_RAISE();

  PyObject *key = frame_ns_deref(es, KosExtra_OBJ(row, 1));
  if (! key) {
    Py_DECREF(obj);
    RESULT_RAISE();
  }

  int res = PyObject_SetItem(obj, key, val);
  Py_DECREF(obj);
  Py_DECREF(key);
  if (res < 0)
    RESULT_RAISE();
  RESULT_NEXT(es);
}


// The end.
