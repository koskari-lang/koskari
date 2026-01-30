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
   Python version compatibility layer

   This module provides compatibility shims for different Python
   versions. Functions and macros are added here as needed by
   implementation units.
*/


#include "lib.h"


/* === string constants === */


static PyObject *_str_send = NULL;


/* === compatibility functions === */


/*
  TODO: As implementation units are ported over, add the necessary
  compatibility functions here:

  - PyObject_GetAwaitable (for coroutine support)
  - PyIter_Send (for Python < 3.10)
  - PyObject_CallMethodOneArg (for Python < 3.9)
  - PyObject_VectorcallMethod (for Python < 3.9)
  - _PyObject_LookupAttr (for Python < 3.7)
*/


/* === unit lifecycle === */


int koskari_compat_init(PyObject *mod) {

  STR_CONST(_str_send, "send");

  return 0;
}


int koskari_compat_clear(PyObject *mod) {

  Py_CLEAR(_str_send);

  return 0;
}


// The end.
