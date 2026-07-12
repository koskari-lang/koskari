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
   FAST_OBJECT_INPLACE_FLOOR_DIVIDE: arg = slot index. Pop TOS (rhs), fetch obj from
   fast slot, PyNumber_InPlaceFloorDivide(obj, rhs), store result back.
   Does not push.
*/

KOS_OP_HANDLER(FAST_OBJECT_INPLACE_FLOOR_DIVIDE) {

  PyObject *ns = CURRENT_NS();
  KOS_OP_REQUIRE_NS(ns);

  size_t idx = (size_t) KosOp_ARG(insn);
  PyObject *obj = kos_ns_fast_deref(ns, idx);
  if (unlikely(! obj))
    return EVAL_RAISE();

  PyObject *rhs = KosEvalState_StackPopRef(es);
  if (unlikely(! rhs)) {
    Py_DECREF(obj);
    return EVAL_RAISE();
  }

  PyObject *result = PyNumber_InPlaceFloorDivide(obj, rhs);
  Py_DECREF(obj);
  Py_DECREF(rhs);
  if (unlikely(! result))
    return EVAL_RAISE();

  int res = kos_ns_fast_store(ns, idx, result);
  Py_DECREF(result);
  return kos_op_result_from_push_int(res);
}


// The end.
