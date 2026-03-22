/*
  This library is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 3 of the License, or
  (at your option) any later version.

  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this library; if not, see <http://www.gnu.org/licenses/>.
*/


/**
   CALL_ATTR_NAMES: arg = extra index. Schema CONST (attr name), COUNT (npos).
   Stack [obj, pos_0, ..., pos_{N-1}, kwd_val_0, ..., kwd_val_{M-1}, names].
   Resolve obj.name, then call like CALL_NAMES.
*/
KOS_OP_HANDLER(CALL_ATTR_NAMES) {
  KosCode *code = CURRENT_CODE(ctx);
  KosExtra row = KosCode_GetExtra(code, KosOp_ARG(insn));
  uint32_t attr_cpool_idx = row.a0;
  unsigned opa = row.a1;

  PyObject *name = KosCode_GetConstant((PyObject *) code, attr_cpool_idx);
  if (! name)
    return -1;

  KosStackBlock **stack = &CURRENT_STACK(ctx);

  PyObject *names = KosStackBlock_Peek(*stack);
  if (! names)
    return -1;
  if (! PyTuple_CheckExact(names)) {
    PyErr_Format(PyExc_RuntimeError, "CALL_ATTR_NAMES names must be tuple"
                 ", not %R", names);
    return -1;
  }

  Py_ssize_t npos = (Py_ssize_t) opa;
  Py_ssize_t nkwds = PyTuple_GET_SIZE(names);
  Py_ssize_t total = npos + nkwds;
  size_t ntotal = (size_t)(total + 2);

  PyObject *const *ptr = KosStackBlock_TailPtr(*stack, ntotal);
  if (! ptr)
    return -1;

  PyObject *obj = ptr[0];
  PyObject *kwnames = nkwds ? ptr[total + 1] : NULL;

  PyObject *resolved;
  int send_self = kos_resolve_named_attr(obj, name, NULL, &resolved);
  if (send_self < 0)
    return -1;

  PyObject *result;
  if (KosCallable_Check(resolved)) {
    KosStackSteal steal = { 0 };
    if (KosStackSteal_Tail(&steal, ntotal, stack) < 0) {
      Py_DECREF(resolved);
      return -1;
    }
    PyObject *kwnames_arg = nkwds ? steal.vals[total + 1] : NULL;
    PyObject *const *args;
    Py_ssize_t nargs;
    if (send_self == 0) {
      args = &steal.vals[0];
      nargs = npos + 1;
    } else {
      args = (npos > 0) ? &steal.vals[1] : NULL;
      nargs = npos;
    }
    int r = KosContext_Call(ctx, out_ev, resolved, args, nargs,
                            kwnames_arg, 0);
    KosStackSteal_Free(&steal);
    Py_DECREF(resolved);
    return r;
  }

  if (send_self == 0) {
    result = PyObject_Vectorcall(resolved, &ptr[0], (size_t)(npos + 1),
                                  kwnames);
  } else {
    result = PyObject_Vectorcall(resolved,
        (npos > 0) ? &ptr[1] : NULL, (size_t) npos, kwnames);
  }
  Py_DECREF(resolved);
  if (! result)
    return -1;

  KosStackBlock_DropNoErr(stack, ntotal);
  return kos_push_or_await(ctx, out_ev, result);
}
