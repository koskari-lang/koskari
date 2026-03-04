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


/* PyDict_NewPresized and PyGen_FetchStopIterationValue symbols may not be
   exported in 3.13+; use our wrappers. */
#define PyDict_NewPresized koskari_Dict_NewPresized
#define PyGen_FetchStopIterationValue koskari_FetchStopIterationValue

/* _PyList_Extend was removed in 3.13; PyList_Extend returns int.  Provide
   a wrapper that returns the list on success for code expecting the old API. */
PyObject *koskari_List_ExtendRet(PyObject *list, PyObject *iterable);
#define _PyList_Extend(list, iter) koskari_List_ExtendRet((PyObject *)(list), (iter))

/* _PyDictView_New was removed in 3.13.  Wrapper dispatches to items/keys. */
PyObject *koskari_DictView_New(PyObject *dict, PyTypeObject *type);
#define _PyDictView_New(dict, type) koskari_DictView_New((dict), (type))


// The end.
