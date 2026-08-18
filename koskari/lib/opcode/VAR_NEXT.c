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
  VAR_NEXT: fused VAR + OBJECT_NEXT. Arg = extra index. Schema VAR
  (iterator name), COUNT (n = 1 or 2). Stack: [] or [default] per n.
  Resolve the iterator name and run the shared step core
  (object_next_step): value or default on exhaustion, 1-arg raises
  StopIteration, auto-await.
*/
KOS_OP_HANDLER_COLD(VAR_NEXT) {
  KosCode *code = KosEvalState_CurrentCode(es);
  KosExtra row = KosCode_GetExtra(code, KosOp_ARG(insn));
  uint16_t n = (uint16_t) row.a1;
  if (unlikely(n != 1 && n != 2)) {
    PyErr_Format(PyExc_ValueError, "VAR_NEXT arg must be 1 or 2, got %u", n);
    return EVAL_RAISE();
  }

  PyObject *var_name = KosEvalState_CONST(es, row.a0);
  if (unlikely(! var_name))
    return EVAL_RAISE();

  PyObject *default_val = NULL;
  if (n == 2) {
    default_val = KosEvalState_StackPopRef(es);
    if (unlikely(! default_val))
      return EVAL_RAISE();
  }

  PyObject *iter = frame_ns_deref(es, var_name);
  if (unlikely(! iter)) {
    Py_XDECREF(default_val);
    return EVAL_RAISE();
  }

  return object_next_step(es, iter, default_val, n);
}


// The end.
