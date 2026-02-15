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
   ITEM: arg unused. Push TOS1[TOS] (subscript). Stack: obj, key (TOS).
*/

KOS_OP_HANDLER(ITEM) {
  PyObject *key = KosStackBlock_PopRef(&CURRENT_STACK(ctx));
  if (! key)
    return -1;
  PyObject *obj = KosStackBlock_PopRef(&CURRENT_STACK(ctx));
  if (! obj) {
    Py_DECREF(key);
    return -1;
  }
  PyObject *item = PyObject_GetItem(obj, key);
  Py_DECREF(obj);
  Py_DECREF(key);
  if (! item)
    return -1;
  return push_result(ctx, item);
}
