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
  VAR_FORMAT_CONST: fused VAR + OBJECT_FORMAT_CONST. Arg = extra index.
  Schema VAR (obj name), CONST (rules string). Resolve the name in the
  current frame's namespace and push PyObject_Format(obj, rules).
*/
KOS_OP_HANDLER_COLD(VAR_FORMAT_CONST) {
  KosCode *code = KosEvalState_CurrentCode(es);
  KosExtra row = KosCode_GetExtra(code, KosOp_ARG(insn));

  PyObject *name = KosEvalState_CONST(es, row.a0);
  PyObject *rules = KosEvalState_CONST(es, row.a1);
  if (unlikely(! name || ! rules))
    return EVAL_RAISE();

  PyObject *val = frame_ns_deref(es, name);
  if (unlikely(! val))
    return EVAL_RAISE();

  PyObject *res = PyObject_Format(val, rules);
  Py_DECREF(val);
  if (unlikely(! res))
    return EVAL_RAISE();

  return kos_op_result_PUSH(es, res);
}


// The end.
