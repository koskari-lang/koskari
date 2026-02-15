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
   ATTR: arg = constant index (name). Push getattr(TOS, name).
*/

KOS_OP_HANDLER(ATTR) {
  PyObject *name = get_name_from_insn(CURRENT_CODE(ctx), insn);
  if (! name)
    return -1;
  PyObject *obj = KosStackBlock_PopRef(&CURRENT_STACK(ctx));
  if (! obj)
    return -1;
  PyObject *val = PyObject_GetAttr(obj, name);
  Py_DECREF(obj);
  if (! val)
    return -1;
  return push_result(ctx, val);
}
