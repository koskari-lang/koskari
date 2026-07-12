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
   ATTR_STORE: arg = constant index (name). setattr(TOS1, name, TOS); pop val then obj.
*/

KOS_OP_HANDLER(ATTR_STORE) {
  PyObject *name = get_name_from_insn(es, insn);
  if (! name)
    return EVAL_RAISE();
  PyObject *val = KosEvalState_StackPopRef(es);
  if (! val)
    return EVAL_RAISE();
  PyObject *obj = KosEvalState_StackPopRef(es);
  if (! obj) {
    Py_DECREF(val);
    return EVAL_RAISE();
  }
  int res = PyObject_SetAttr(obj, name, val);
  Py_DECREF(obj);
  Py_DECREF(val);
  if (res < 0)
    return EVAL_RAISE();
  return EVAL_NORMAL();
}


// The end.
