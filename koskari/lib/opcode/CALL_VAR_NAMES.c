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
  KosCode *code = CURRENT_CODE(ctx);
  KosExtra row = KosCode_GetExtra(code, KosOp_ARG(insn));
  uint32_t name_cpool_idx = row.a0;
  uint32_t names_cpool_idx = row.a1;
  Py_ssize_t npos = (Py_ssize_t) row.a2;

  PyObject *name = KosCode_GetConstant((PyObject *) code, name_cpool_idx);
  if (! name)
    return -1;

  PyObject *callable = frame_ns_deref(ctx, name);
  if (! callable)
    return -1;

  PyObject *names = KosCode_GetConstant((PyObject *) code, names_cpool_idx);
  if (! names) {
    Py_DECREF(callable);
    return -1;
  }
  if (! PyTuple_CheckExact(names)) {
    PyErr_Format(PyExc_RuntimeError, "CALL_VAR_NAMES names must be tuple"
                 ", not %R", names);
    Py_DECREF(callable);
    return -1;
  }

  Py_ssize_t nkwds = PyTuple_GET_SIZE(names);
  Py_ssize_t total = npos + nkwds;
  size_t ntotal = (size_t) total;

  KosStackBlock **stack = &CURRENT_STACK(ctx);
  PyObject *const *ptr = KosStackBlock_TailPtr(*stack, ntotal);
  if (! ptr) {
    Py_DECREF(callable);
    return -1;
  }

  PyObject *kwnames = nkwds ? names : NULL;

  if (KosCallable_Check(callable)) {
    KosStackSteal steal = { 0 };
    if (KosStackSteal_Tail(&steal, ntotal, stack) < 0) {
      Py_DECREF(callable);
      return -1;
    }
    int r = KosContext_Call(ctx, out_ev, callable, steal.vals,
                            npos, kwnames, 0);
    Py_DECREF(callable);
    KosStackSteal_Free(&steal);
    return r;
  }

  PyObject *result = PyObject_Vectorcall(callable, ptr, (size_t) npos,
                                         kwnames);
  Py_DECREF(callable);
  if (! result)
    return -1;

  KosStackBlock_DropNoErr(stack, ntotal);
  return kos_push_or_await(ctx, out_ev, result);
}


// The end.
