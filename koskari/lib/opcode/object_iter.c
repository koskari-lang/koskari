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
   OBJECT_ITER: pop TOS, call PyObject_GetIter, push result.
*/
KOS_OP_HANDLER(OBJECT_ITER) {
    PyObject *val = KosStackBlock_PopRef(&CURRENT_STACK(ctx));
    if (! val) return -1;
    PyObject *res = PyObject_GetIter(val);
    Py_DECREF(val);
    if (! res) return -1;
    (void) insn;
    return push_result(ctx, res);
}
