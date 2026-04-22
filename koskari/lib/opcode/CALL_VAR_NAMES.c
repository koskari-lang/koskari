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
  CALL_VAR_NAMES: arg = extra index. Schema VAR (name), CONST (kw names),
  COUNT (npos). Call callable from namespace by name. Stack [pos_0, ...,
  pos_{N-1}, kwd_val_0, ..., kwd_val_{M-1}]. KosCallable: steal args then
  KosContext_Call. Plain Python: TailPtr + PyObject_Vectorcall + DropNoErr.
*/

KOS_OP_HANDLER(CALL_VAR_NAMES) {
  KosContext *ctx = KosEvalState_Context(es);
  KosCode *code = CURRENT_CODE(ctx);
  KosExtra row = KosCode_GetExtra(code, KosOp_ARG(insn));
  uint32_t name_cpool_idx = row.a0;
  uint32_t names_cpool_idx = row.a1;
  Py_ssize_t npos = (Py_ssize_t) row.a2;

  PyObject *name = KosCode_GetConstant((PyObject *) code, name_cpool_idx);
  if (! name)
    return KOS_OP_POST_ERR();

  PyObject *callable = frame_ns_deref(ctx, name);
  if (! callable)
    return KOS_OP_POST_ERR();

  PyObject *names = KosCode_GetConstant((PyObject *) code, names_cpool_idx);
  if (! names) {
    Py_DECREF(callable);
    return KOS_OP_POST_ERR();
  }
  if (! PyTuple_CheckExact(names)) {
    PyErr_Format(PyExc_RuntimeError, "CALL_VAR_NAMES names must be tuple"
                 ", not %R", names);
    Py_DECREF(callable);
    return KOS_OP_POST_ERR();
  }

  Py_ssize_t nkwds = PyTuple_GET_SIZE(names);
  Py_ssize_t total = npos + nkwds;
  size_t ntotal = (size_t) total;

  PyObject *const *ptr = KosEvalState_StackTailPtr(es, ntotal);
  if (! ptr) {
    Py_DECREF(callable);
    return KOS_OP_POST_ERR();
  }

  if (KosCallable_Check(callable)) {
    KosStackSteal steal = { 0 };
    if (KosStackSteal_Tail(&steal, ntotal, KosEvalState_StackOperandSlot(es)) < 0) {
      Py_DECREF(callable);
      return KOS_OP_POST_ERR();
    }
    int r = KosContext_Call(ctx, out_ev, callable, steal.vals,
                            npos, names, 0);
    Py_DECREF(callable);
    KosStackSteal_Free(&steal);
    return kos_op_outcome_from_call_int(ctx, r);
  }

  PyObject *result = PyObject_Vectorcall(callable, ptr, (size_t) npos,
                                         names);
  KosEvalState_StackDropNoErr(es, ntotal);
  Py_DECREF(callable);

  return result ? kos_op_outcome_from_push_int(op_push_or_await(es, out_ev, result)) : KOS_OP_POST_ERR();
}


// The end.
