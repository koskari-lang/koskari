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
   CALL_VAR_ATTR: arg = extra index. Schema VAR (name), CONST (attr name),
   COUNT (arg count). Stack [arg_0, ..., arg_{N-1}] only; obj is loaded
   from the frame namespace by VAR name. Resolve obj.attr, then call.
   Unbound fn: fn(obj, *args). Bound/other: callable(*args).
*/
KOS_OP_HANDLER(CALL_VAR_ATTR) {
  KosContext *ctx = KosEvalState_Context(es);
  KosCode *code = CURRENT_CODE(ctx);
  KosExtra row = KosCode_GetExtra(code, KosOp_ARG(insn));
  uint32_t name_cpool_idx = row.a0;
  uint32_t attr_cpool_idx = row.a1;
  unsigned n = row.a2;

  PyObject *var_name = KosCode_GetConstant((PyObject *) code, name_cpool_idx);
  if (! var_name)
    return KOS_OP_POST_ERR();

  PyObject *attr_name = KosCode_GetConstant((PyObject *) code, attr_cpool_idx);
  if (! attr_name)
    return KOS_OP_POST_ERR();

  PyObject *obj = frame_ns_deref(ctx, var_name);
  if (! obj)
    return KOS_OP_POST_ERR();

  PyObject *const *ptr = NULL;
  if (n > 0) {
    ptr = KosEvalState_StackTailPtr(es, (size_t) n);
    if (! ptr) {
      Py_DECREF(obj);
      return KOS_OP_POST_ERR();
    }
  }

  PyObject *resolved;
  int send_self = kos_resolve_named_attr(obj, attr_name, NULL, &resolved);
  if (send_self < 0) {
    Py_DECREF(obj);
    return KOS_OP_POST_ERR();
  }

  if (KosCallable_Check(resolved)) {
    KosStackSteal steal = { 0 };
    int steal_ok;
    if (send_self == 0) {
      steal_ok = KosEvalState_StackSteal_TailPrepend(es, &steal, obj, (size_t) n);
    } else {
      steal_ok = KosEvalState_StackSteal_Tail(es, &steal, (size_t) n);
    }
    if (steal_ok < 0) {
      Py_DECREF(resolved);
      Py_DECREF(obj);
      return KOS_OP_POST_ERR();
    }
    PyObject *const *args;
    Py_ssize_t nargs;
    if (send_self == 0) {
      args = steal.vals;
      nargs = (Py_ssize_t) n + 1;
    } else {
      args = (n > 0) ? steal.vals : NULL;
      nargs = (Py_ssize_t) n;
    }
    int r = KosContext_Call(ctx, out_ev, resolved, args, nargs, NULL, 0);
    Py_DECREF(obj);
    KosStackSteal_Free(&steal);
    Py_DECREF(resolved);
    return kos_op_outcome_from_call_int(ctx, r);
  }

  PyObject *result;
  if (send_self == 0) {
    KosStackSteal steal = { 0 };
    if (KosEvalState_StackSteal_TailPrepend(es, &steal, obj, (size_t) n) < 0) {
      Py_DECREF(resolved);
      Py_DECREF(obj);
      return KOS_OP_POST_ERR();
    }
    result = PyObject_Vectorcall(resolved, steal.vals, (size_t) n + 1, NULL);
    KosStackSteal_Free(&steal);

  } else {
    result = PyObject_Vectorcall(resolved, ptr, (size_t) n, NULL);
    KosEvalState_StackDropNoErr(es, (size_t) n);
  }

  Py_DECREF(resolved);
  Py_DECREF(obj);

  return result ? kos_op_outcome_from_push_int(op_push_or_await(es, out_ev, result)) : KOS_OP_POST_ERR();
}


// The end.
