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
   TAIL_CALL_NAMES: arg = N (number of positional arguments). Same
   stack layout as CALL_NAMES: [callable, pos_0, ..., pos_{N-1},
   kwd_val_0, ..., kwd_val_{M-1}, names]. Call via kos_callable_tail_call.
*/

KOS_OP_HANDLER(TAIL_CALL_NAMES) {
  unsigned opa = KosOp_ARG(insn);

  KosStackBlock **stack = &CURRENT_STACK(ctx);

  PyObject *names = KosStackBlock_PopRef(stack);
  if (! names)
    return -1;

  if (! PyTuple_CheckExact(names)) {
    PyErr_Format(PyExc_RuntimeError, "TAIL_CALL_NAMES names must be tuple"
                 ", not %R", names);
    Py_DECREF(names);
    return -1;
  }

  size_t nkwds = PyTuple_GET_SIZE(names);
  size_t total = (size_t) opa + nkwds;

  PyObject *args = NULL;
  PyObject *kwds = NULL;
  PyObject *callable = NULL;

  if (total == 0) {
    Py_DECREF(names);
    callable = KosStackBlock_PopRef(stack);
    if (! callable)
      return -1;

    int r = kos_callable_tail_call(ctx, out_ev, callable, NULL, NULL);
    Py_DECREF(callable);
    return r;
  }

  if (nkwds > 0) {
    kwds = KosStackBlock_PopTailDictFromNames(stack, names);
    if (! kwds) {
      Py_DECREF(names);
      return -1;
    }
  }
  Py_DECREF(names);

  if (opa > 0) {
    args = KosStackBlock_PopTail(stack, opa);
    if (! args) {
      Py_XDECREF(kwds);
      return -1;
    }
  }

  callable = KosStackBlock_PopRef(stack);
  if (! callable) {
    Py_XDECREF(args);
    Py_XDECREF(kwds);
    return -1;
  }

  int r = kos_callable_tail_call(ctx, out_ev, callable, args, kwds);

  Py_DECREF(callable);
  Py_XDECREF(args);
  Py_XDECREF(kwds);
  return r;
}
