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
   VAR_CONST_ITEM: arg = extra index. Schema VAR (obj name), CONST (key).
   Push obj[key] with the object resolved by name in the frame
   namespace; nothing is popped. The cold twin of FAST_CONST_ITEM.
*/

KOS_OP_HANDLER(VAR_CONST_ITEM) {
  const KosExtra *row = KosOp_ROW(insn);
  PyObject *key = KosExtra_OBJ(row, 1);
  PyObject *obj = frame_ns_deref(es, KosExtra_OBJ(row, 0));
  if (! obj)
    RESULT_RAISE();

  PyObject *item = PyObject_GetItem(obj, key);
  Py_DECREF(obj);
  if (! item)
    RESULT_RAISE();
  RESULT_STEAL(es, item);
}


// The end.
