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
   CAR: no arg. Pop one value (a pair). Push its car. Fails if TOS is
   not a pair or is nil.
*/

KOS_OP_HANDLER(CAR) {
  PyObject *pair = KosStackBlock_PopRef(&CURRENT_STACK(ctx));
  if (! pair)
    return -1;
  if (! KosPair_Check(pair) || KosNil_Check(pair)) {
    Py_DECREF(pair);
    PyErr_SetString(PyExc_TypeError, "car argument must be pair");
    return -1;
  }
  PyObject *val = KosPair_CAR(pair);
  Py_INCREF(val);
  Py_DECREF(pair);
  return push_result(ctx, val);
}
