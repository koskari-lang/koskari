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
   CALL_FAST_STAR: arg = extra index. Schema FASTVAR (slot), STAR (bitflags).
   Call callable from fast local slot. Stack [pos, kwd] or subsets per
   flags (1 = *args, 2 = **kwargs). Pop per flags, ensure pos is tuple,
   call via KosEvalState_CallArgs.
*/


static int ensure_tuple_fast_star(PyObject **val) {
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


KOS_OP_HANDLER(CALL_FAST_STAR) {
  KosEvent _op_ev = {0};
  KosCode *code = KosEvalState_CurrentCode(es);
  KosExtra row = KosCode_GetExtra(code, KosOp_ARG(insn));
  uint32_t slot_idx = row.a0;
  unsigned opa = row.a1;

  PyObject *ns = CURRENT_NS();
  if (! ns) {
    PyErr_SetString(PyExc_RuntimeError, "CALL_FAST_STAR on NULL namespace");
    return EVAL_RAISE();
  }
  if (! KosNamespace_SupportsFast(ns)) {
    PyErr_Format(PyExc_RuntimeError, "CALL_FAST_STAR on %R", ns);
    return EVAL_RAISE();
  }

  PyObject *callable = KosNamespace_FastDeref(ns, (size_t) slot_idx);
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
    if (ensure_tuple_fast_star(&pos)) {
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
    if (ensure_tuple_fast_star(&pos)) {
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

  int res = KosEvalState_CallArgs(es, &_op_ev, callable, pos, kwd, 0);

  Py_DECREF(callable);
  Py_DECREF(pos);
  Py_XDECREF(kwd);
  return kos_op_result_from_call_int(es, res, &_op_ev);
}


// The end.
