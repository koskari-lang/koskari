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
   CALL_STAR: arg = bitflags (1 = has *args, 2 = has **kwargs). Stack:
   [callable, pos, kwd] with kwd at TOS. pos = *args (tuple or
   iterable); kwd = **kwargs (dict). Pop per opa bits, ensure pos is
   tuple, call via KosContext_CallArgs.
*/


static int ensure_tuple(PyObject **val) {
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


KOS_OP_HANDLER(CALL_STAR) {
  KosContext *ctx = KosEvalState_CONTEXT(es);
  unsigned opa = KosOp_ARG(insn);

  PyObject *fun = NULL;
  PyObject *pos = NULL;
  PyObject *kwd = NULL;

  if ((opa & 3) == 3) {
    kwd = KosEvalState_StackPopRef(es);
    if (! kwd)
      return KOS_OP_POST_ERR();
    pos = KosEvalState_StackPopRef(es);
    if (! pos) {
      Py_DECREF(kwd);
      return KOS_OP_POST_ERR();
    }
    fun = KosEvalState_StackPopRef(es);
    if (! fun) {
      Py_DECREF(pos);
      Py_DECREF(kwd);
      return KOS_OP_POST_ERR();
    }
    if (ensure_tuple(&pos)) {
      Py_DECREF(fun);
      Py_DECREF(pos);
      Py_DECREF(kwd);
      return KOS_OP_POST_ERR();
    }
  } else if (opa & 2) {
    kwd = KosEvalState_StackPopRef(es);
    if (! kwd)
      return KOS_OP_POST_ERR();
    fun = KosEvalState_StackPopRef(es);
    if (! fun) {
      Py_DECREF(kwd);
      return KOS_OP_POST_ERR();
    }
    pos = PyTuple_New(0);
    if (! pos) {
      Py_DECREF(fun);
      Py_DECREF(kwd);
      return KOS_OP_POST_ERR();
    }
  } else if (opa & 1) {
    pos = KosEvalState_StackPopRef(es);
    if (! pos)
      return KOS_OP_POST_ERR();
    fun = KosEvalState_StackPopRef(es);
    if (! fun) {
      Py_DECREF(pos);
      return KOS_OP_POST_ERR();
    }
    if (ensure_tuple(&pos)) {
      Py_DECREF(fun);
      Py_DECREF(pos);
      return KOS_OP_POST_ERR();
    }
    kwd = NULL;
  } else {
    fun = KosEvalState_StackPopRef(es);
    if (! fun)
      return KOS_OP_POST_ERR();
    pos = PyTuple_New(0);
    if (! pos) {
      Py_DECREF(fun);
      return KOS_OP_POST_ERR();
    }
    kwd = NULL;
  }

  int res = KosContext_CallArgs(ctx, out_ev, fun, pos, kwd, 0);

  Py_DECREF(fun);
  Py_DECREF(pos);
  Py_XDECREF(kwd);
  return kos_op_outcome_from_call_int(ctx, res);
}
