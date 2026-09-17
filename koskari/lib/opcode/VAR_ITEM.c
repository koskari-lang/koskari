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
   VAR_ITEM: arg = extra index. Schema VAR (obj name), VAR (key name).
   Push obj[key] with both operands resolved by name in the frame
   namespace; nothing is popped. The cold twin of FAST_ITEM: a row that
   survives lowering names an object or key with no fast slot.
*/

KOS_OP_HANDLER(VAR_ITEM) {
  const KosExtra *row = KosOp_ROW(insn);
  PyObject *obj = frame_ns_deref(es, KosExtra_OBJ(row, 0));
  if (! obj)
    RESULT_RAISE();

  PyObject *key = frame_ns_deref(es, KosExtra_OBJ(row, 1));
  if (! key) {
    Py_DECREF(obj);
    RESULT_RAISE();
  }

  PyObject *item = PyObject_GetItem(obj, key);
  Py_DECREF(obj);
  Py_DECREF(key);
  if (! item)
    RESULT_RAISE();
  RESULT_STEAL(es, item);
}


// The end.
