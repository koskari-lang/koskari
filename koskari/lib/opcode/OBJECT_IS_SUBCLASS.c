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
   OBJECT_IS_SUBCLASS: arg = 2 (class, base) or N (class, base, ... bases).
   Pop class and base(s), push issubclass(class, base_or_tuple).
*/

KOS_OP_HANDLER(OBJECT_IS_SUBCLASS) {
  int opa = (int) insn->arg;
  PyObject *value = NULL, *match = NULL;

  if (opa < 2) {
    match = KosStackBlock_PopRef(&CURRENT_STACK(ctx));
    if (! match)
      return -1;
    value = KosStackBlock_PopRef(&CURRENT_STACK(ctx));
    if (! value) {
      Py_DECREF(match);
      return -1;
    }
  } else {
    match = KosStackBlock_PopTail(&CURRENT_STACK(ctx), (size_t) opa);
    if (! match)
      return -1;
    value = KosStackBlock_PopRef(&CURRENT_STACK(ctx));
    if (! value) {
      Py_DECREF(match);
      return -1;
    }
  }

  int res = PyObject_IsSubclass(value, match);
  Py_DECREF(value);
  Py_DECREF(match);
  if (res < 0)
    return -1;

  PyObject *out = (res ? Py_True : Py_False);
  Py_INCREF(out);
  return push_result(ctx, out);
}
