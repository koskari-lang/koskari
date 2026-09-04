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
  CONST_ITEM_TAKE: arg = constant index (key). getitem then del TOS[key].
*/
KOS_OP_HANDLER(CONST_ITEM_TAKE) {
  PyObject *key = KosOp_OBJ(insn);

  if (unlikely(! key))
    RESULT_RAISE();

  PyObject *obj = KosEvalState_StackPopRef(es);
  if (unlikely(! obj))
    RESULT_RAISE();

  PyObject *item = PyObject_GetItem(obj, key);
  if (unlikely(! item)) {
    Py_DECREF(obj);
    RESULT_RAISE();
  }

  int res = PyObject_DelItem(obj, key);
  Py_DECREF(obj);
  if (unlikely(res < 0)) {
    Py_DECREF(item);
    RESULT_RAISE();
  }

  RESULT_STEAL(es, item);
}


// The end.
