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
  CALL_VAR_NAMES: arg = extra index. Schema VAR (name), CONST (kw names),
  COUNT (npos). Call callable from namespace by name. Stack [pos_0, ...,
  pos_{N-1}, kwd_val_0, ..., kwd_val_{M-1}]. KosCallable: steal args then
  KosEvalState_Call. Plain Python: TailPtr + PyObject_Vectorcall + DropNoErr.
*/

KOS_OP_HANDLER_COLD(CALL_VAR_NAMES) {
  KosCode *code = KosEvalState_CurrentCode(es);
  KosExtra row = KosCode_GetExtra(code, KosOp_ARG(insn));
  uint32_t name_cpool_idx = row.a0;
  uint32_t names_cpool_idx = row.a1;
  Py_ssize_t npos = (Py_ssize_t) row.a2;

  PyObject *name = KosEvalState_CONST(es, name_cpool_idx);
  if (! name)
    RESULT_RAISE();

  PyObject *callable = frame_ns_deref(es, name);
  if (! callable)
    RESULT_RAISE();

  PyObject *names = KosEvalState_CONST(es, names_cpool_idx);
  if (! names) {
    Py_DECREF(callable);
    RESULT_RAISE();
  }
  if (! PyTuple_CheckExact(names)) {
    PyErr_Format(PyExc_RuntimeError, "CALL_VAR_NAMES names must be tuple"
                 ", not %R", names);
    Py_DECREF(callable);
    RESULT_RAISE();
  }

  Py_ssize_t nkwds = PyTuple_GET_SIZE(names);
  size_t ntotal = (size_t) (npos + nkwds);

  PyObject *const *argv = NULL;
  if (ntotal) {
    argv = KosEvalState_StackTailPtr(es, ntotal);
    if (! argv) {
      Py_DECREF(callable);
      RESULT_RAISE();
    }
  }

  return kos_call_invoke(es, callable, NULL, argv, npos, names,
                         KOS_CALL_ARGV_WINDOW | KOS_CALL_CALLABLE_OWNED);
}


// The end.
