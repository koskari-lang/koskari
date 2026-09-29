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
   koskari/lib/exception.c

   Exception state handling: the throw-argument normalizer. The
   KosExcState helpers are small enough to live inline in exception.h.

   :author: Christopher O'Brien <obriencj@gmail.com>
   :license: GNU General Public License v3
*/


#include "lib.h"


int KosExc_NormalizeThrow(
    PyObject *typ,
    PyObject *val,
    PyObject *tb,
    PyObject **typ_out,
    PyObject **exc_out) {

  if (PyExceptionClass_Check(typ)) {
    Py_INCREF(typ);
    Py_XINCREF(val);
    Py_XINCREF(tb);
    PyErr_NormalizeException(&typ, &val, &tb);
    Py_XDECREF(tb);
    *typ_out = typ;
    *exc_out = val;
    return 0;
  }

  if (PyExceptionInstance_Check(typ)) {
    if (val && val != Py_None) {
      PyErr_SetString(PyExc_TypeError,
                      "instance exception may not have a separate value");
      return -1;
    }
    Py_INCREF(typ);
    *exc_out = typ;
    *typ_out = (PyObject *) Py_TYPE(typ);
    Py_INCREF(*typ_out);
    return 0;
  }

  PyErr_SetString(PyExc_TypeError,
                  "exceptions must be classes or instances deriving "
                  "from BaseException");
  return -1;
}


int KosExc_NormalizeThrowArgs(
    PyObject *args,
    const char *format,
    PyObject **typ_out,
    PyObject **exc_out) {

  PyObject *typ = NULL, *val = NULL, *tb = NULL;

  if (! PyArg_ParseTuple(args, format, &typ, &val, &tb))
    return -1;

  return KosExc_NormalizeThrow(typ, val, tb, typ_out, exc_out);
}


// The end.
