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
  PyObject *key = KosEvalState_StackPopRef(es);
  if (! key)
    return KOS_OP_POST_ERR();

  PyObject *obj = KosEvalState_StackPopRef(es);
  if (unlikely(! obj)) {
    Py_DECREF(key);
    return KOS_OP_POST_ERR();
  }

  PyObject *item = PyObject_GetItem(obj, key);
  Py_DECREF(obj);
  Py_DECREF(key);

  if (unlikely(! item))
    return KOS_OP_POST_ERR();

  return kos_op_outcome_from_int_step(push_result(es, item));
}


// The end.
