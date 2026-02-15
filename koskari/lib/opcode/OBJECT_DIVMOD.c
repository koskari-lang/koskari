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
   OBJECT_DIVMOD: pop right, left; call PyNumber_Divmod; push result.
*/
KOS_OP_HANDLER(OBJECT_DIVMOD) {
    if (insn->arg != 2) {
      PyErr_SetString(PyExc_RuntimeError, "OBJECT_DIVMOD: only arg 2 supported");
      return -1;
    }
    PyObject *right = KosStackBlock_PopRef(&CURRENT_STACK(ctx));
    if (! right) return -1;
    PyObject *left = KosStackBlock_PopRef(&CURRENT_STACK(ctx));
    if (! left) { Py_DECREF(right); return -1; }
    PyObject *res = PyNumber_Divmod(left, right);
    Py_DECREF(left);
    Py_DECREF(right);
    if (! res) return -1;
    return push_result(ctx, res);
}
