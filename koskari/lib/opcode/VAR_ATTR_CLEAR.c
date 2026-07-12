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
   VAR_ATTR_CLEAR: arg = extra index. Schema VAR (obj name), CONST (attr name).
   delattr(obj, attr) where obj = frame_ns_deref(var_name).
*/

KOS_OP_HANDLER(VAR_ATTR_CLEAR) {
  KosCode *code = KosEvalState_CurrentCode(es);
  KosExtra row = KosCode_GetExtra(code, KosOp_ARG(insn));
  PyObject *var_name = KosEvalState_CONST(es, row.a0);
  PyObject *attr_name = KosEvalState_CONST(es, row.a1);
  if (! var_name || ! attr_name)
    return EVAL_RAISE();
  PyObject *obj = frame_ns_deref(es, var_name);
  if (! obj)
    return EVAL_RAISE();
  int res = PyObject_DelAttr(obj, attr_name);
  Py_DECREF(obj);
  if (res < 0)
    return EVAL_RAISE();
  return EVAL_NORMAL();
}


// The end.
