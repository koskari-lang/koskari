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
  with names at TOS. KosCallable: steal then KosEvalState_Call. Plain Python:
  TailPtr + PyObject_Vectorcall + DropNoErr.
*/
KOS_OP_HANDLER_COLD(CALL_NAMES) {
  unsigned opa = KosOp_ARG(insn);

  PyObject *names = KosEvalState_StackPeek(es);
  if (! names)
    RESULT_RAISE();
  if (! PyTuple_CheckExact(names)) {
    PyErr_Format(PyExc_RuntimeError, "CALL_NAMES names must be tuple"
                 ", not %R", names);
    RESULT_RAISE();
  }

  Py_ssize_t npos = (Py_ssize_t) opa;
  Py_ssize_t nkwds = PyTuple_GET_SIZE(names);
  Py_ssize_t total = npos + nkwds;

  /* Pop the names tuple off the top; the remaining window is the
     standard [callable, pos..., kw_vals...] shape for the core. */
  names = KosEvalState_StackPopRef(es);
  if (! names)
    RESULT_RAISE();

  PyObject *const *ptr = KosEvalState_StackTailPtr(es, (size_t) total + 1);
  if (! ptr) {
    Py_DECREF(names);
    RESULT_RAISE();
  }

  KosOpResult r = kos_call_invoke(es, ptr[0], NULL,
                                  total ? ptr + 1 : NULL, npos,
                                  nkwds ? names : NULL,
                                  KOS_CALL_ARGV_WINDOW
                                  | KOS_CALL_WINDOW_CALLABLE);
  Py_DECREF(names);
  RESULT_RELAY(es, r);
}


// The end.
