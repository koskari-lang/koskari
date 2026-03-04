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


typedef enum PySendResult {
  PYGEN_RETURN,
  PYGEN_NEXT,
  PYGEN_ERROR,
} PySendResult;


PySendResult PyIter_Send(PyObject *iter, PyObject *arg, PyObject **presult);


int _PyObject_LookupAttr(PyObject *obj, PyObject *name, PyObject **dest);


#define PyObject_CallNoArgs _PyObject_CallNoArg


#define PyObject_CallOneArg _PyObject_CallArg1


#define PyObject_CallMethodNoArgs(obj, name)            \
  PyObject_VectorcallMethod(name, &(obj), 1, NULL)


PyObject *PyObject_CallMethodOneArg(PyObject *obj, PyObject *name,
                                    PyObject *val);


#define Py_TPFLAGS_HAVE_VECTORCALL 0
#define Py_TPFLAGS_METHOD_DESCRIPTOR 0

#define PY_VECTORCALL_ARGUMENTS_OFFSET 0


#define PyVectorcall_NARGS(n) n


typedef PyObject *(*vectorcallfunc)
  (PyObject *callable, PyObject *const *args,
   size_t nargsf, PyObject *kwnames);


#define PyObject_Vectorcall(callable, args, nargsf, kwnames)            \
  _PyObject_FastCallKeywords(callable, (PyObject **) args,              \
                             nargsf, kwnames)


#define PyObject_VectorcallDict(callable, args, nargsf, kwargs)         \
  _PyObject_FastCallDict(callable, (PyObject **) args,                  \
                         nargsf, kwargs)


PyObject *PyObject_VectorcallMethod(
    PyObject *name,
    PyObject *const *args,
    size_t nargsf,
    PyObject *kwargs);


// The end.
