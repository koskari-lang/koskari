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
   CALL_NAMES: arg = N (number of positional arguments). Stack:
   [callable, pos_0, ..., pos_{N-1}, kwd_val_0, ..., kwd_val_{M-1}, names]
   with names at TOS. names is a tuple of M keyword names; kwd_val_i
   maps to names[i]. Pops everything, builds a contiguous arg array from
   the popped-tuple items, and passes names directly as kwnames to
   kos_callable_vcall, avoiding a kwds dict allocation.
*/

KOS_OP_HANDLER(CALL_NAMES) {
  unsigned opa = KosOp_ARG(insn);

  KosStackBlock **stack = &CURRENT_STACK(ctx);

  PyObject *names = KosStackBlock_PopRef(stack);
  if (! names)
    return -1;

  if (! PyTuple_CheckExact(names)) {
    PyErr_Format(PyExc_RuntimeError, "CALL_NAMES names must be tuple"
                 ", not %R", names);
    Py_DECREF(names);
    return -1;
  }

  Py_ssize_t nkwds = PyTuple_GET_SIZE(names);
  Py_ssize_t npos = (Py_ssize_t) opa;
  Py_ssize_t total = npos + nkwds;

  PyObject *callable = NULL;

  if (total == 0) {
    Py_DECREF(names);
    callable = KosStackBlock_PopRef(stack);
    if (! callable)
      return -1;
    int r = kos_callable_vcall(ctx, out_ev, callable, NULL, 0, NULL);
    Py_DECREF(callable);
    return r;
  }

  /*
     Pop all positional + keyword values into a single contiguous tuple.
     The tuple layout is [pos_0, ..., pos_{N-1}, kwd_val_0, ..., kwd_val_{M-1}].
     Pass its item array as args and npos as nargs, with names as kwnames.
     This avoids building a dict while still transferring ownership out of
     the stack before the call (no push_or_await ordering hazard).
  */
  PyObject *all_args = KosStackBlock_PopTail(stack, (size_t) total);
  if (! all_args) {
    Py_DECREF(names);
    return -1;
  }

  callable = KosStackBlock_PopRef(stack);
  if (! callable) {
    Py_DECREF(all_args);
    Py_DECREF(names);
    return -1;
  }

  PyObject *const *argv = &PyTuple_GET_ITEM(all_args, 0);
  int r = kos_callable_vcall(ctx, out_ev, callable, argv, npos,
                             nkwds ? names : NULL);

  Py_DECREF(callable);
  Py_DECREF(all_args);
  Py_DECREF(names);
  return r;
}
