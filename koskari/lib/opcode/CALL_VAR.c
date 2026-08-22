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
  CALL_VAR: arg = extra index. Schema VAR (name), COUNT (arg count).
  Call callable from namespace by name. Stack [arg_0, ..., arg_{N-1}].
  KosCallable: steal args then KosEvalState_Call. Plain Python: TailPtr +
  PyObject_Vectorcall + DropNoErr.
*/
KOS_OP_HANDLER_COLD(CALL_VAR) {
  KosCode *code = KosEvalState_CurrentCode(es);
  KosExtra row = KosCode_GetExtra(code, KosOp_ARG(insn));
  uint32_t name_cpool_idx = row.a0;
  unsigned n = row.a1;

  PyObject *name = KosEvalState_CONST(es, name_cpool_idx);
  if (unlikely(! name))
    return EVAL_RAISE();

  PyObject *callable = frame_ns_deref(es, name);
  SAYF("CALL_VAR: callable=%R", callable);
  if (unlikely(! callable))
    return EVAL_RAISE();

  PyObject *const *argv = NULL;
  if (n) {
    argv = KosEvalState_StackTailPtr(es, n);
    if (unlikely(! argv)) {
      Py_DECREF(callable);
      return EVAL_RAISE();
    }
  }

  return kos_call_invoke(es, callable, NULL, argv, (Py_ssize_t) n, NULL,
                         KOS_CALL_ARGV_WINDOW | KOS_CALL_CALLABLE_OWNED);
}


// The end.
