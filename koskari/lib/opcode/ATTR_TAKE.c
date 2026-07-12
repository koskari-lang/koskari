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
   ATTR_TAKE: arg = constant index (name). getattr then delattr(TOS, name).
*/

KOS_OP_HANDLER(ATTR_TAKE) {
  PyObject *name = get_name_from_insn(es, insn);
  if (! name)
    return EVAL_RAISE();

  PyObject *obj = KosEvalState_StackPopRef(es);
  if (! obj)
    return EVAL_RAISE();

  PyObject *val = PyObject_GetAttr(obj, name);
  if (! val) {
    Py_DECREF(obj);
    return EVAL_RAISE();
  }

  int res = PyObject_DelAttr(obj, name);
  Py_DECREF(obj);
  if (res < 0) {
    Py_DECREF(val);
    return EVAL_RAISE();
  }

  return kos_op_result_PUSH(es, val);
}


// The end.
