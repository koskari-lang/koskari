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
  ITEM_STORE: arg unused. TOS2[TOS1] = TOS; pop val, key, obj.
*/
KOS_OP_HANDLER(ITEM_STORE) {
  struct {
    PyObject *obj;
    PyObject *key;
    PyObject *val;
  } args = {0};

  if (unlikely(KOS_EVALSTATE_POP_ARGS(es, args)))
    return EVAL_RAISE();

  int res = PyObject_SetItem(args.obj, args.key, args.val);
  Py_DECREF(args.obj);
  Py_DECREF(args.key);
  Py_DECREF(args.val);

  if (unlikely(res < 0))
    return EVAL_RAISE();

  return EVAL_NORMAL();
}


// The end.
