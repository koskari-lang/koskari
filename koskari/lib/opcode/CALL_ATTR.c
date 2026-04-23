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
   CALL_ATTR: arg = extra index. Schema CONST (attr name), COUNT (arg count).
   Stack [obj, arg_0, ..., arg_{N-1}]. Resolve obj.name, then call.
   Unbound fn: fn(obj, *args). Bound/other: callable(*args).
*/
KOS_OP_HANDLER(CALL_ATTR) {
  KosContext *ctx = KosEvalState_CONTEXT(es);
  KosCode *code = CURRENT_CODE(ctx);
  KosExtra row = KosCode_GetExtra(code, KosOp_ARG(insn));
  uint32_t attr_cpool_idx = row.a0;
  unsigned n = row.a1;
  unsigned count = n + 1;

  PyObject *name = KosCode_GetConstant((PyObject *) code, attr_cpool_idx);
  if (! name)
    return KOS_OP_POST_ERR();

  PyObject *const *ptr = KosEvalState_StackTailPtr(es, count);
  if (! ptr)
    return KOS_OP_POST_ERR();

  PyObject *obj = ptr[0];

  PyObject *resolved;
  int send_self = kos_resolve_named_attr(obj, name, NULL, &resolved);
  if (send_self < 0)
    return KOS_OP_POST_ERR();

  PyObject *result;
  if (KosCallable_Check(resolved)) {
    KosStackSteal steal = { 0 };
    if (KosEvalState_StackSteal_Tail(es, &steal, (size_t)count) < 0) {
      Py_DECREF(resolved);
      return KOS_OP_POST_ERR();
    }
    PyObject *const *args;
    Py_ssize_t nargs;
    if (send_self == 0) {
      args = &steal.vals[0];
      nargs = (Py_ssize_t) n + 1;
    } else {
      args = (n > 0) ? &steal.vals[1] : NULL;
      nargs = (Py_ssize_t) n;
    }
    int r = KosContext_Call(ctx, out_ev, resolved, args, nargs, NULL, 0);
    KosStackSteal_Free(&steal);
    Py_DECREF(resolved);
    return kos_op_outcome_from_call_int(ctx, r);
  }

  if (send_self == 0) {
    result = PyObject_Vectorcall(resolved, &ptr[0], (size_t) n + 1, NULL);
  } else {
    result = PyObject_Vectorcall(resolved,
        (n > 0) ? &ptr[1] : NULL, (size_t) n, NULL);
  }
  KosEvalState_StackDropNoErr(es, count);
  Py_DECREF(resolved);

  return result ? kos_op_outcome_from_push_int(op_push_or_await(es, out_ev, result)) : KOS_OP_POST_ERR();
}
