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
   CALL_VAR_ATTR_NAMES: arg = extra index. Schema VAR (name), CONST (attr),
   CONST (kw names tuple), COUNT (npos). Stack [pos_0, ..., pos_{N-1},
   kwd_val_0, ..., kwd_val_{M-1}]. Obj from namespace by name; resolve
   obj.name, call.
*/

KOS_OP_HANDLER(CALL_VAR_ATTR_NAMES) {
  KosCode *code = KosEvalState_CurrentCode(es);
  KosExtra row = KosCode_GetExtra(code, KosOp_ARG(insn));
  uint32_t name_cpool_idx = row.a0;
  uint32_t attr_cpool_idx = row.a1;
  uint32_t names_cpool_idx = row.a2;
  unsigned opa = row.a3;

  // borrow reference
  PyObject *name = KosCode_GetConstant((PyObject *) code, name_cpool_idx);
  if (! name)
    return KOS_OP_POST_ERR();

  // owned reference
  PyObject *obj = frame_ns_deref(es, name);
  if (! obj)
    return KOS_OP_POST_ERR();

  // borrowed reference
  PyObject *attr_name = KosCode_GetConstant((PyObject *) code, attr_cpool_idx);
  if (! attr_name)
    return KOS_OP_POST_ERR();

  // borrowed reference
  PyObject *names = KosCode_GetConstant((PyObject *) code, names_cpool_idx);
  if (! names) {
    Py_DECREF(obj);
    return KOS_OP_POST_ERR();
  }

  if (! PyTuple_CheckExact(names)) {
    PyErr_Format(PyExc_RuntimeError, "CALL_VAR_ATTR_NAMES names must be tuple"
                 ", not %R", names);
    Py_DECREF(obj);
    return KOS_OP_POST_ERR();
  }

  Py_ssize_t npos = (Py_ssize_t) opa;
  Py_ssize_t nkwds = PyTuple_GET_SIZE(names);
  PyObject *const *ptr = NULL;
  if (npos + nkwds > 0) {
    ptr = KosEvalState_StackTailPtr(es, (size_t)(npos + nkwds));
    if (! ptr) {
      Py_DECREF(obj);
      return KOS_OP_POST_ERR();
    }
  }

  Py_ssize_t total = npos + nkwds;
  size_t ntotal = (size_t) total;

  PyObject *resolved;
  int send_self = kos_resolve_named_attr(obj, attr_name, NULL, &resolved);
  if (send_self < 0)
    return KOS_OP_POST_ERR();

  if (KosCallable_Check(resolved)) {
    KosStackSteal steal = { 0 };
    int steal_ok;
    if (send_self == 0) {
      steal_ok = KosEvalState_StackSteal_TailPrepend(es, &steal, obj, ntotal);
    } else {
      steal_ok = KosEvalState_StackSteal_Tail(es, &steal, ntotal);
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
      nargs = npos + 1;
    } else {
      args = (total > 0) ? steal.vals : NULL;
      nargs = npos;
    }
    int r = KosEvalState_Call(es, out_ev, resolved, args, nargs, names, 0);
    KosStackSteal_Free(&steal);
    Py_DECREF(resolved);
    Py_DECREF(obj);
    return kos_op_outcome_from_call_int(es, r);
  }

  PyObject *result;
  if (send_self == 0) {
    KosStackSteal steal = { 0 };
    if (KosEvalState_StackSteal_TailPrepend(es, &steal, obj, ntotal) < 0) {
      Py_DECREF(resolved);
      Py_DECREF(obj);
      return KOS_OP_POST_ERR();
    }
    result = PyObject_Vectorcall(resolved, steal.vals, (size_t)(npos + 1), names);
    KosStackSteal_Free(&steal);

  } else {
    result = PyObject_Vectorcall(resolved, ptr, (size_t) npos, names);
    KosEvalState_StackDropNoErr(es, ntotal);
  }

  Py_DECREF(resolved);
  Py_DECREF(obj);

  return kos_op_outcome_PUSH_OR_AWAIT(es, out_ev, result);
}


// The end.
