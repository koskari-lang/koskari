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
  PyObject *key = KosEvalState_CONST(es, KosOp_ARG(insn));
  if (unlikely(! key))
    return EVAL_RAISE();

  PyObject *val = KosEvalState_StackPopRef(es);
  if (unlikely(! val))
    return EVAL_RAISE();

  PyObject *obj = KosEvalState_StackPopRef(es);
  if (unlikely(! obj)) {
    Py_DECREF(val);
    return EVAL_RAISE();
  }

  int res = PyObject_SetItem(obj, key, val);
  Py_DECREF(obj);
  Py_DECREF(val);

  if (unlikely(res < 0))
    return EVAL_RAISE();

  return EVAL_NORMAL();
}


// The end.
