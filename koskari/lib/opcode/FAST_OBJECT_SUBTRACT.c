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
   FAST_OBJECT_SUBTRACT: arg = slot index. Pop TOS (rhs), fetch obj from fast
   slot, PyNumber_InPlaceSubtract(obj, rhs), store result back. Does not push.
*/

KOS_OP_HANDLER(FAST_OBJECT_SUBTRACT) {

  PyObject *ns = CURRENT_NS();
  if (unlikely(! ns)) {
    PyErr_SetString(PyExc_RuntimeError,
                    "FAST_OBJECT_SUBTRACT invoked on NULL namespace");
    return EVAL_RAISE();
  }

  size_t idx = (size_t) KosOp_ARG(insn);
  PyObject *obj = KosNamespace_FastDeref(ns, idx);
  if (unlikely(! obj))
    return EVAL_RAISE();

  PyObject *rhs = KosEvalState_StackPopRef(es);
  if (unlikely(! rhs)) {
    Py_DECREF(obj);
    return EVAL_RAISE();
  }

  PyObject *result = PyNumber_InPlaceSubtract(obj, rhs);
  Py_DECREF(obj);
  Py_DECREF(rhs);
  if (unlikely(! result))
    return EVAL_RAISE();

  int res = KosNamespace_FastStore(ns, idx, result);
  Py_DECREF(result);
  return kos_op_result_from_push_int(res);
}


// The end.
