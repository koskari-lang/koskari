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
   ATTR_CLEAR: arg = constant index (name). delattr(TOS, name).
*/

KOS_OP_HANDLER(ATTR_CLEAR) {
  KosContext *ctx = KosEvalState_Context(es);
  PyObject *name = get_name_from_insn(CURRENT_CODE(ctx), insn);
  if (! name)
    return KOS_OP_POST_ERR();
  PyObject *obj = KosEvalState_StackPopRef(es);
  if (! obj)
    return KOS_OP_POST_ERR();
  int res = PyObject_DelAttr(obj, name);
  Py_DECREF(obj);
  if (res < 0)
    return KOS_OP_POST_ERR();
  (void) out_ev;
  return KOS_OP_RUN();
}
