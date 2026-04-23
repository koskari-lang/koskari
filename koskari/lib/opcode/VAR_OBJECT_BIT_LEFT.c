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
   VAR_OBJECT_BIT_LEFT: arg = constant index (name). Pop TOS (rhs), fetch obj
   from name in frame, PyNumber_InPlaceLshift(obj, rhs), store result back.
   Does not push.
*/

KOS_OP_HANDLER(VAR_OBJECT_BIT_LEFT) {
  KosContext *ctx = KosEvalState_CONTEXT(es);
  (void) out_ev;

  PyObject *name = get_name_from_insn(CURRENT_CODE(ctx), insn);
  if (unlikely(! name))
    return KOS_OP_POST_ERR();

  PyObject *obj = frame_ns_deref(ctx, name);
  if (unlikely(! obj))
    return KOS_OP_POST_ERR();

  PyObject *rhs = KosEvalState_StackPopRef(es);
  if (unlikely(! rhs)) {
    Py_DECREF(obj);
    return KOS_OP_POST_ERR();
  }

  PyObject *result = PyNumber_InPlaceLshift(obj, rhs);
  Py_DECREF(obj);
  Py_DECREF(rhs);
  if (unlikely(! result))
    return KOS_OP_POST_ERR();

  int res = frame_ns_store(ctx, name, result);
  Py_DECREF(result);
  return kos_op_outcome_from_int_step(res);
}
