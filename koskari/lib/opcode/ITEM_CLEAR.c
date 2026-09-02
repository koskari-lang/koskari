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
  ITEM_CLEAR: arg unused. del TOS1[TOS]. Stack: obj, key (TOS).
*/
KOS_OP_HANDLER(ITEM_CLEAR) {
  PyObject *key = KosEvalState_StackPopRef(es);
  if (unlikely(! key))
    RESULT_RAISE();

  PyObject *obj = KosEvalState_StackPopRef(es);
  if (unlikely(! obj)) {
    Py_DECREF(key);
    RESULT_RAISE();
  }

  int res = PyObject_DelItem(obj, key);
  Py_DECREF(obj);
  Py_DECREF(key);

  if (unlikely(res < 0))
    RESULT_RAISE();

  RESULT_NEXT(es);
}


// The end.
