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
   OBJECT_VARS: arg unused. Pop TOS, push getattr(TOS, "__dict__").
*/

KOS_OP_HANDLER(OBJECT_VARS) {
  PyObject *val = KosStackBlock_PopRef(&CURRENT_STACK(ctx));
  if (! val)
    return -1;

  PyObject *d = PyObject_GetAttrString(val, "__dict__");
  Py_DECREF(val);
  if (! d) {
    PyErr_Clear();
    PyErr_SetString(PyExc_TypeError,
                    "vars argument must have __dict__ attribute");
    return -1;
  }
  (void) insn;
  return push_result(ctx, d);
}
