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
   CALL_ATTR_STAR: arg = extra index. Schema CONST (attr name),
   COUNT (opa bitflags: 1 = has *args, 2 = has **kwargs).
   Stack [obj, pos, kwd] or subsets per flags. Pop per flags, ensure
   pos is tuple, resolve obj.name, prepend obj to args if send_self,
   call via KosEvalState_CallArgs.
*/


static int ensure_tuple_attr_star(PyObject **val) {
  if (! (val && *val))
    return -1;

  if (! PyTuple_CheckExact(*val)) {
    PyObject *fixed = PySequence_Tuple(*val);
    Py_DECREF(*val);
    *val = fixed;

    if (! fixed)
      return -1;
  }

  return 0;
}


KOS_OP_HANDLER(CALL_ATTR_STAR) {
  KosEvent _op_ev = {0};
  KosCode *code = KosEvalState_CurrentCode(es);
  KosExtra row = KosCode_GetExtra(code, KosOp_ARG(insn));
  uint32_t attr_cpool_idx = row.a0;
  unsigned opa = row.a1;

  PyObject *name = KosCode_GetConstant((PyObject *) code, attr_cpool_idx);
  if (! name)
    return EVAL_RAISE();

  PyObject *obj = NULL;
  PyObject *pos = NULL;
  PyObject *kwd = NULL;

  if ((opa & 3) == 3) {
    kwd = KosEvalState_StackPopRef(es);
    if (! kwd)
      return EVAL_RAISE();
    pos = KosEvalState_StackPopRef(es);
    if (! pos) {
      Py_DECREF(kwd);
      return EVAL_RAISE();
    }
    obj = KosEvalState_StackPopRef(es);
    if (! obj) {
      Py_DECREF(pos);
      Py_DECREF(kwd);
      return EVAL_RAISE();
    }
    if (ensure_tuple_attr_star(&pos)) {
      Py_DECREF(obj);
      Py_DECREF(pos);
      Py_DECREF(kwd);
      return EVAL_RAISE();
    }
  } else if (opa & 2) {
    kwd = KosEvalState_StackPopRef(es);
    if (! kwd)
      return EVAL_RAISE();
    obj = KosEvalState_StackPopRef(es);
    if (! obj) {
      Py_DECREF(kwd);
      return EVAL_RAISE();
    }
    pos = PyTuple_New(0);
    if (! pos) {
      Py_DECREF(obj);
      Py_DECREF(kwd);
      return EVAL_RAISE();
    }
  } else if (opa & 1) {
    pos = KosEvalState_StackPopRef(es);
    if (! pos)
      return EVAL_RAISE();
    obj = KosEvalState_StackPopRef(es);
    if (! obj) {
      Py_DECREF(pos);
      return EVAL_RAISE();
    }
    if (ensure_tuple_attr_star(&pos)) {
      Py_DECREF(obj);
      Py_DECREF(pos);
      return EVAL_RAISE();
    }
    kwd = NULL;
  } else {
    obj = KosEvalState_StackPopRef(es);
    if (! obj)
      return EVAL_RAISE();
    pos = PyTuple_New(0);
    if (! pos) {
      Py_DECREF(obj);
      return EVAL_RAISE();
    }
    kwd = NULL;
  }

  PyObject *resolved;
  int send_self = kos_resolve_named_attr(obj, name, NULL, &resolved);
  if (send_self < 0) {
    Py_DECREF(obj);
    Py_DECREF(pos);
    Py_XDECREF(kwd);
    return EVAL_RAISE();
  }

  PyObject *args_for_call = pos;
  PyObject *combined = NULL;

  if (send_self == 0) {
    combined = PyTuple_Pack(1, obj);
    if (! combined) {
      Py_DECREF(obj);
      Py_DECREF(resolved);
      Py_DECREF(pos);
      Py_XDECREF(kwd);
      return EVAL_RAISE();
    }
    args_for_call = PySequence_Concat(combined, pos);
    Py_DECREF(combined);
    if (! args_for_call) {
      Py_DECREF(obj);
      Py_DECREF(resolved);
      Py_DECREF(pos);
      Py_XDECREF(kwd);
      return EVAL_RAISE();
    }
    Py_DECREF(pos);
  }

  Py_DECREF(obj);

  int res = KosEvalState_CallArgs(es, &_op_ev, resolved, args_for_call, kwd, 0);

  Py_DECREF(resolved);
  Py_DECREF(args_for_call);
  Py_XDECREF(kwd);
  return kos_op_result_from_call_int(es, res, &_op_ev);
}


// The end.
