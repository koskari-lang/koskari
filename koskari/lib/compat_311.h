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


#define PyDict_NewPresized _PyDict_NewPresized
#define PyGen_FetchStopIterationValue _PyGen_FetchStopIterationValue
#define PyObject_LookupAttr _PyObject_LookupAttr

/* PyErr_GetRaisedException added in 3.12 */
#define PyErr_GetRaisedException koskari_Err_GetRaisedException


// In 3.11+ the PyFrameObject structure becomes opaque. However we
// need to be able get and set f_back and f_lineno, so I've copied the
// structure into this header.
struct _frame {
  PyObject_HEAD

  PyFrameObject *f_back;
  struct _PyInterpreterFrame *f_frame;
  PyObject *f_trace;
  int f_lineno;

  // we don't actually need anything below this point, and I don't
  // want to chase their structure changes for fields I am not using,
  // so it's void pointers the rest of the way down.
  void *opaque [1];
};


// The end.
