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
  VAR2: arg = extra index. Schema VAR, VAR (name constants). Look up
  both names in the current frame namespace and push the values in row
  order. Both names resolve before either push, so a missing name
  raises with nothing pushed.
*/
KOS_OP_HANDLER_COLD(VAR2) {
  KosCode *code = KosEvalState_CurrentCode(es);
  KosExtra row = KosCode_GetExtra(code, KosOp_ARG(insn));

  PyObject *name = KosEvalState_CONST(es, row.a0);
  if (unlikely(! name))
    return EVAL_RAISE();

  PyObject *a = frame_ns_deref(es, name);
  if (unlikely(! a))
    return EVAL_RAISE();

  name = KosEvalState_CONST(es, row.a1);
  if (unlikely(! name)) {
    Py_DECREF(a);
    return EVAL_RAISE();
  }

  PyObject *b = frame_ns_deref(es, name);
  if (unlikely(! b)) {
    Py_DECREF(a);
    return EVAL_RAISE();
  }

  if (unlikely(KosEvalState_StackPUSH(es, a) < 0)) {
    Py_DECREF(b);
    return EVAL_RAISE();
  }

  return kos_op_result_PUSH(es, b);
}


// The end.
