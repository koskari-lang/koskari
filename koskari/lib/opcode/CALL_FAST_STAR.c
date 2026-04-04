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
   call via KosContext_CallArgs.
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
  KosCode *code = CURRENT_CODE(ctx);
  KosExtra row = KosCode_GetExtra(code, KosOp_ARG(insn));
  uint32_t slot_idx = row.a0;
  unsigned opa = row.a1;

  PyObject *ns = CURRENT_NS(ctx);
  if (! ns) {
    PyErr_SetString(PyExc_RuntimeError, "CALL_FAST_STAR on NULL namespace");
    return KOS_OP_POST_ERR();
  }
  if (! KosNamespace_SupportsFast(ns)) {
    PyErr_Format(PyExc_RuntimeError, "CALL_FAST_STAR on %R", ns);
    return KOS_OP_POST_ERR();
  }

  PyObject *callable = KosNamespace_FastDeref(ns, (size_t) slot_idx);
  if (! callable)
    return KOS_OP_POST_ERR();

  PyObject *pos = NULL;
  PyObject *kwd = NULL;

  KosStackBlock **stack = &CURRENT_STACK(ctx);

  if ((opa & 3) == 3) {
    kwd = KosStackBlock_PopRef(stack);
    if (! kwd) {
      Py_DECREF(callable);
      return KOS_OP_POST_ERR();
    }
    pos = KosStackBlock_PopRef(stack);
    if (! pos) {
      Py_DECREF(callable);
      Py_DECREF(kwd);
      return KOS_OP_POST_ERR();
    }
    if (ensure_tuple_fast_star(&pos)) {
      Py_DECREF(callable);
      Py_DECREF(pos);
      Py_DECREF(kwd);
      return KOS_OP_POST_ERR();
    }
  } else if (opa & 2) {
    kwd = KosStackBlock_PopRef(stack);
    if (! kwd) {
      Py_DECREF(callable);
      return KOS_OP_POST_ERR();
    }
    pos = PyTuple_New(0);
    if (! pos) {
      Py_DECREF(callable);
      Py_DECREF(kwd);
      return KOS_OP_POST_ERR();
    }
  } else if (opa & 1) {
    pos = KosStackBlock_PopRef(stack);
    if (! pos) {
      Py_DECREF(callable);
      return KOS_OP_POST_ERR();
    }
    if (ensure_tuple_fast_star(&pos)) {
      Py_DECREF(callable);
      Py_DECREF(pos);
      return KOS_OP_POST_ERR();
    }
    kwd = NULL;
  } else {
    pos = PyTuple_New(0);
    if (! pos) {
      Py_DECREF(callable);
      return KOS_OP_POST_ERR();
    }
    kwd = NULL;
  }

  int res = KosContext_CallArgs(ctx, out_ev, callable, pos, kwd, 0);

  Py_DECREF(callable);
  Py_DECREF(pos);
  Py_XDECREF(kwd);
  return kos_op_outcome_from_call_int(ctx, res);
}


// The end.
