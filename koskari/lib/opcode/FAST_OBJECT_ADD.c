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
   FAST_OBJECT_ADD: arg = slot index. Pop TOS (rhs), fetch obj from fast
   slot, PyNumber_InPlaceAdd(obj, rhs), store result back. Does not push.
*/

KOS_OP_HANDLER(FAST_OBJECT_ADD) {
  PyObject *ns = CURRENT_NS(ctx);
  if (! ns) {
    PyErr_SetString(PyExc_RuntimeError,
                    "FAST_OBJECT_ADD invoked on NULL namespace");
    return -1;
  }
  if (! KosNamespace_SupportsFast(ns)) {
    PyErr_Format(PyExc_RuntimeError,
                 "FAST_OBJECT_ADD invoked on %R", ns);
    return -1;
  }
  size_t idx = (size_t) KosOp_ARG(insn);
  PyObject *rhs = KosStackBlock_PopRef(&CURRENT_STACK(ctx));
  if (! rhs)
    return -1;
  PyObject *obj = KosNamespace_FastDeref(ns, idx);
  if (! obj) {
    Py_DECREF(rhs);
    return -1;
  }
  PyObject *result = PyNumber_InPlaceAdd(obj, rhs);
  Py_DECREF(obj);
  Py_DECREF(rhs);
  if (! result)
    return -1;
  if (KosNamespace_FastStore(ns, idx, result) < 0) {
    Py_DECREF(result);
    return -1;
  }
  Py_DECREF(result);
  (void) out_ev;
  return 0;
}

// The end.
