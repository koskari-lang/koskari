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
   TAIL_CALL_STAR: arg = bitflags (1 = has *args, 2 = has **kwargs).
   Same stack layout as CALL_STAR: [callable, pos, kwd]. Tail call: do not
   push a new frame; remove or recycle the current one.

   KosEvalState_CallArgs handles frame behavior and dispatch.
*/


static int ensure_tuple_star(PyObject **val) {
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


KOS_OP_HANDLER_COLD(TAIL_CALL_STAR) {
  unsigned opa = KosOp_ARG(insn);

  PyObject *fun = NULL;
  PyObject *pos = NULL;
  PyObject *kwd = NULL;

  if ((opa & 3) == 3) {
    kwd = KosEvalState_StackPopRef(es);
    if (! kwd)
      RESULT_RAISE();
    pos = KosEvalState_StackPopRef(es);
    if (! pos) {
      Py_DECREF(kwd);
      RESULT_RAISE();
    }
    fun = KosEvalState_StackPopRef(es);
    if (! fun) {
      Py_DECREF(pos);
      Py_DECREF(kwd);
      RESULT_RAISE();
    }
    if (ensure_tuple_star(&pos)) {
      Py_DECREF(fun);
      Py_DECREF(pos);
      Py_DECREF(kwd);
      RESULT_RAISE();
    }
  } else if (opa & 2) {
    kwd = KosEvalState_StackPopRef(es);
    if (! kwd)
      RESULT_RAISE();
    fun = KosEvalState_StackPopRef(es);
    if (! fun) {
      Py_DECREF(kwd);
      RESULT_RAISE();
    }
    pos = PyTuple_New(0);
    if (! pos) {
      Py_DECREF(fun);
      Py_DECREF(kwd);
      RESULT_RAISE();
    }
  } else if (opa & 1) {
    pos = KosEvalState_StackPopRef(es);
    if (! pos)
      RESULT_RAISE();
    fun = KosEvalState_StackPopRef(es);
    if (! fun) {
      Py_DECREF(pos);
      RESULT_RAISE();
    }
    if (ensure_tuple_star(&pos)) {
      Py_DECREF(fun);
      Py_DECREF(pos);
      RESULT_RAISE();
    }
    kwd = NULL;
  } else {
    fun = KosEvalState_StackPopRef(es);
    if (! fun)
      RESULT_RAISE();
    pos = PyTuple_New(0);
    if (! pos) {
      Py_DECREF(fun);
      RESULT_RAISE();
    }
    kwd = NULL;
  }

  KosOpResult res = KosEvalState_CallArgs(es, fun, pos, kwd, 1);

  Py_DECREF(fun);
  Py_DECREF(pos);
  Py_XDECREF(kwd);
  return res;
}


// The end.
