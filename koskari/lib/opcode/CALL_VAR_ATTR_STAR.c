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
   CALL_VAR_ATTR_STAR: arg = extra index. Schema VAR (name), CONST (attr name),
   COUNT (opa bitflags: 1 = has *args, 2 = has **kwargs).
   Stack [pos, kwd] or subsets per flags; obj from frame namespace by VAR
   name (not on stack). Ensure pos is tuple, resolve obj.attr, prepend obj
   to args if send_self, call via KosEvalState_CallArgs.
*/


static int ensure_tuple_var_attr_star(PyObject **val) {
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


KOS_OP_HANDLER_COLD(CALL_VAR_ATTR_STAR) {
  KosCode *code = KosEvalState_CurrentCode(es);
  KosExtra row = KosCode_GetExtra(code, KosOp_ARG(insn));
  uint32_t name_cpool_idx = row.a0;
  uint32_t attr_cpool_idx = row.a1;
  unsigned opa = row.a2;

  PyObject *var_name = KosEvalState_CONST(es, name_cpool_idx);
  if (! var_name)
    RESULT_RAISE();

  PyObject *attr_name = KosEvalState_CONST(es, attr_cpool_idx);
  if (! attr_name)
    RESULT_RAISE();

  PyObject *obj = frame_ns_deref(es, var_name);
  if (! obj)
    RESULT_RAISE();

  PyObject *pos = NULL;
  PyObject *kwd = NULL;

  if ((opa & 3) == 3) {
    kwd = KosEvalState_StackPopRef(es);
    if (! kwd) {
      Py_DECREF(obj);
      RESULT_RAISE();
    }
    pos = KosEvalState_StackPopRef(es);
    if (! pos) {
      Py_DECREF(obj);
      Py_DECREF(kwd);
      RESULT_RAISE();
    }
    if (ensure_tuple_var_attr_star(&pos)) {
      Py_DECREF(obj);
      Py_DECREF(pos);
      Py_DECREF(kwd);
      RESULT_RAISE();
    }
  } else if (opa & 2) {
    kwd = KosEvalState_StackPopRef(es);
    if (! kwd) {
      Py_DECREF(obj);
      RESULT_RAISE();
    }
    pos = PyTuple_New(0);
    if (! pos) {
      Py_DECREF(obj);
      Py_DECREF(kwd);
      RESULT_RAISE();
    }
  } else if (opa & 1) {
    pos = KosEvalState_StackPopRef(es);
    if (! pos) {
      Py_DECREF(obj);
      RESULT_RAISE();
    }
    if (ensure_tuple_var_attr_star(&pos)) {
      Py_DECREF(obj);
      Py_DECREF(pos);
      RESULT_RAISE();
    }
    kwd = NULL;
  } else {
    pos = PyTuple_New(0);
    if (! pos) {
      Py_DECREF(obj);
      RESULT_RAISE();
    }
    kwd = NULL;
  }

  PyObject *resolved;
  uint16_t cache_ix = KosOp_CACHE(insn);
  KosCacheSlot *cache = (cache_ix == KOS_CACHE_UNSET)
      ? NULL : &KosCode_CACHE(code)[cache_ix];
  int send_self = kos_resolve_named_attr(obj, attr_name, cache, &resolved);
  if (send_self < 0) {
    Py_DECREF(obj);
    Py_DECREF(pos);
    Py_XDECREF(kwd);
    RESULT_RAISE();
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
      RESULT_RAISE();
    }
    args_for_call = PySequence_Concat(combined, pos);
    Py_DECREF(combined);
    if (! args_for_call) {
      Py_DECREF(obj);
      Py_DECREF(resolved);
      Py_DECREF(pos);
      Py_XDECREF(kwd);
      RESULT_RAISE();
    }
    Py_DECREF(pos);
  }

  Py_DECREF(obj);

  KosOpResult res = KosEvalState_CallArgs(es, resolved, args_for_call, kwd, 0);

  Py_DECREF(resolved);
  Py_DECREF(args_for_call);
  Py_XDECREF(kwd);
  RESULT_RELAY(es, res);
}


// The end.
