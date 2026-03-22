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
   VAR_ATTR_STORE: arg = extra index. Schema VAR (obj name), CONST (attr name).
   setattr(obj, attr, TOS) where obj = frame_ns_deref(var_name). Pop val.
*/

KOS_OP_HANDLER(VAR_ATTR_STORE) {
  KosCode *code = CURRENT_CODE(ctx);
  KosExtra row = KosCode_GetExtra(code, KosOp_ARG(insn));
  PyObject *var_name = KosCode_GetConstant((PyObject *) code, row.a0);
  PyObject *attr_name = KosCode_GetConstant((PyObject *) code, row.a1);
  if (! var_name || ! attr_name)
    return -1;
  PyObject *val = KosStackBlock_PopRef(&CURRENT_STACK(ctx));
  if (! val)
    return -1;
  PyObject *obj = frame_ns_deref(ctx, var_name);
  if (! obj) {
    Py_DECREF(val);
    return -1;
  }
  int res = PyObject_SetAttr(obj, attr_name, val);
  Py_DECREF(obj);
  Py_DECREF(val);
  if (res < 0)
    return -1;
  (void) out_ev;
  return 0;
}
