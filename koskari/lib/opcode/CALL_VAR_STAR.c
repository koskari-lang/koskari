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
   CALL_VAR_STAR: arg = extra index. Schema VAR (name), STAR (bitflags).
   Call callable from namespace by name. Stack [pos, kwd] or subsets per
   flags (1 = *args, 2 = **kwargs). Pop per flags, ensure pos is tuple,
   call via KosEvalState_CallArgs.
*/


static int ensure_tuple_var_star(PyObject **val) {
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


KOS_OP_HANDLER(CALL_VAR_STAR) {
  KosCode *code = KosEvalState_CurrentCode(es);
  KosExtra row = KosCode_GetExtra(code, KosOp_ARG(insn));
  uint32_t name_cpool_idx = row.a0;
  unsigned opa = row.a1;

  PyObject *name = KosCode_GetConstant((PyObject *) code, name_cpool_idx);
  if (! name)
    return EVAL_RAISE();

  PyObject *callable = frame_ns_deref(es, name);
  if (! callable)
    return EVAL_RAISE();

  PyObject *pos = NULL;
  PyObject *kwd = NULL;

  if ((opa & 3) == 3) {
    kwd = KosEvalState_StackPopRef(es);
    if (! kwd) {
      Py_DECREF(callable);
      return EVAL_RAISE();
    }
    pos = KosEvalState_StackPopRef(es);
    if (! pos) {
      Py_DECREF(callable);
      Py_DECREF(kwd);
      return EVAL_RAISE();
    }
    if (ensure_tuple_var_star(&pos)) {
      Py_DECREF(callable);
      Py_DECREF(pos);
      Py_DECREF(kwd);
      return EVAL_RAISE();
    }
  } else if (opa & 2) {
    kwd = KosEvalState_StackPopRef(es);
    if (! kwd) {
      Py_DECREF(callable);
      return EVAL_RAISE();
    }
    pos = PyTuple_New(0);
    if (! pos) {
      Py_DECREF(callable);
      Py_DECREF(kwd);
      return EVAL_RAISE();
    }
  } else if (opa & 1) {
    pos = KosEvalState_StackPopRef(es);
    if (! pos) {
      Py_DECREF(callable);
      return EVAL_RAISE();
    }
    if (ensure_tuple_var_star(&pos)) {
      Py_DECREF(callable);
      Py_DECREF(pos);
      return EVAL_RAISE();
    }
    kwd = NULL;
  } else {
    pos = PyTuple_New(0);
    if (! pos) {
      Py_DECREF(callable);
      return EVAL_RAISE();
    }
    kwd = NULL;
  }

  KosOpResult res = KosEvalState_CallArgs(es, callable, pos, kwd, 0);

  Py_DECREF(callable);
  Py_DECREF(pos);
  Py_XDECREF(kwd);
  return res;
}


// The end.
