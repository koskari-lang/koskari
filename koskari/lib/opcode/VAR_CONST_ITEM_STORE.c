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
   VAR_CONST_ITEM_STORE: arg = extra index. Schema VAR (obj name), CONST
   (key). Stack [val]; obj[key] = val with the object resolved by name
   in the frame namespace after the value was pushed, pop val, push
   nothing. The cold twin of FAST_CONST_ITEM_STORE.
*/

KOS_OP_HANDLER_COLD(VAR_CONST_ITEM_STORE) {
  const KosExtra *row = KosOp_ROW(insn);
  PyObject *key = KosExtra_OBJ(row, 1);
  PyObject *val = KosEvalState_StackPopRef(es);
  if (! val)
    RESULT_RAISE();

  PyObject *obj = frame_ns_deref(es, KosExtra_OBJ(row, 0));
  if (! obj) {
    Py_DECREF(val);
    RESULT_RAISE();
  }

  int res = PyObject_SetItem(obj, key, val);
  Py_DECREF(obj);
  Py_DECREF(val);
  if (res < 0)
    RESULT_RAISE();
  RESULT_NEXT(es);
}


// The end.
