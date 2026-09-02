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
   EXCEPTION: arg unused. Push the current exception instance (from
   PyErr_GetExcInfo) onto the stack, or Py_None if no exception is set.
*/

KOS_OP_HANDLER(EXCEPTION) {
  PyObject *typ = NULL, *val = NULL, *tb = NULL;
  PyErr_GetExcInfo(&typ, &val, &tb);

  if (! typ) {
    RESULT_PUSH(es, Py_None);
  }

  if (! val)
    PyErr_NormalizeException(&typ, &val, &tb);

  Py_DECREF(typ);
  Py_XDECREF(tb);

  RESULT_STEAL(es, val);
}


// The end.
