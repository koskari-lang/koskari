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
   CONST_ITEM_STORE: arg = constant index (key). TOS1[key] = TOS; pop val then obj.
*/

KOS_OP_HANDLER(CONST_ITEM_STORE) {
  PyObject *key = KosCode_GetConstant((PyObject *) CURRENT_CODE(ctx),
                                      KosOp_ARG(insn));
  if (! key)
    return KOS_OP_POST_ERR();
  PyObject *val = KosStackBlock_PopRef(&CURRENT_STACK(ctx));
  if (! val)
    return KOS_OP_POST_ERR();
  PyObject *obj = KosStackBlock_PopRef(&CURRENT_STACK(ctx));
  if (! obj) {
    Py_DECREF(val);
    return KOS_OP_POST_ERR();
  }
  int res = PyObject_SetItem(obj, key, val);
  Py_DECREF(obj);
  Py_DECREF(val);
  if (res < 0)
    return KOS_OP_POST_ERR();
  (void) out_ev;
  return KOS_OP_RUN();
}
