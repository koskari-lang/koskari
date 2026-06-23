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
   CALL_ATTR_NAMES: arg = extra index. Schema CONST (attr name),
   CONST (kwarg names tuple), COUNT (npos).
   Stack [obj, pos_0, ..., pos_{N-1}, kwd_val_0, ..., kwd_val_{M-1}].
   Resolve obj.name, then call like CALL_NAMES.
*/
KOS_OP_HANDLER(CALL_ATTR_NAMES) {
  KosCode *code = KosEvalState_CurrentCode(es);
  KosExtra row = KosCode_GetExtra(code, KosOp_ARG(insn));
  uint32_t attr_cpool_idx = row.a0;
  uint32_t names_cpool_idx = row.a1;
  unsigned opa = row.a2;

  PyObject *name = KosCode_GetConstant((PyObject *) code, attr_cpool_idx);
  if (! name)
    return EVAL_RAISE();

  PyObject *names = KosCode_GetConstant((PyObject *) code, names_cpool_idx);
  if (! names)
    return EVAL_RAISE();

  if (! PyTuple_CheckExact(names)) {
    PyErr_Format(PyExc_RuntimeError, "CALL_ATTR_NAMES names must be tuple"
                 ", not %R", names);
    return EVAL_RAISE();
  }

  Py_ssize_t npos = (Py_ssize_t) opa;
  Py_ssize_t nkwds = PyTuple_GET_SIZE(names);
  Py_ssize_t total = npos + nkwds;
  size_t ntotal = (size_t)(total + 1);

  PyObject *const *ptr = KosEvalState_StackTailPtr(es, ntotal);
  if (! ptr)
    return EVAL_RAISE();

  PyObject *obj = ptr[0];

  PyObject *resolved;
  int send_self = kos_resolve_named_attr(obj, name, NULL, &resolved);
  if (send_self < 0)
    return EVAL_RAISE();

  PyObject *result;
  if (KosCallable_Check(resolved)) {
    KosStackSteal steal = { 0 };
    if (KosEvalState_StackSteal_Tail(es, &steal, ntotal) < 0) {
      Py_DECREF(resolved);
      return EVAL_RAISE();
    }
    PyObject *const *args;
    Py_ssize_t nargs;
    if (send_self == 0) {
      args = &steal.vals[0];
      nargs = npos + 1;
    } else {
      args = (total > 0) ? &steal.vals[1] : NULL;
      nargs = npos;
    }
    KosOpResult r = KosEvalState_Call(es, resolved, args, nargs,
                            names, 0);
    KosStackSteal_Free(&steal);
    Py_DECREF(resolved);
    return r;
  }

  if (send_self == 0) {
    result = PyObject_Vectorcall(resolved, &ptr[0], (size_t)(npos + 1),
                                 names);
  } else {
    result = PyObject_Vectorcall(resolved,
        (total > 0) ? &ptr[1] : NULL, (size_t) npos, names);
  }
  Py_DECREF(resolved);
  if (! result)
    return EVAL_RAISE();

  KosEvalState_StackDropNoErr(es, ntotal);
  return kos_op_result_PUSH_OR_AWAIT(es, result);
}


// The end.
