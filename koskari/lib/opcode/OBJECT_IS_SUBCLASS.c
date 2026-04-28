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
   OBJECT_IS_SUBCLASS: arg = 2 (class, base) or N (class, base, ... bases).
   Pop class and base(s), push issubclass(class, base_or_tuple).
*/

KOS_OP_HANDLER(OBJECT_IS_SUBCLASS) {
  int opa = (int) KosOp_ARG(insn);
  PyObject *value = NULL, *match = NULL;

  if (opa < 2) {
    match = KosEvalState_StackPopRef(es);
    if (! match)
      return KOS_OP_POST_ERR();
    value = KosEvalState_StackPopRef(es);
    if (! value) {
      Py_DECREF(match);
      return KOS_OP_POST_ERR();
    }
  } else {
    match = KosEvalState_StackPopTail(es, (size_t) opa);
    if (! match)
      return KOS_OP_POST_ERR();
    value = KosEvalState_StackPopRef(es);
    if (! value) {
      Py_DECREF(match);
      return KOS_OP_POST_ERR();
    }
  }

  int res = PyObject_IsSubclass(value, match);
  Py_DECREF(value);
  Py_DECREF(match);
  if (res < 0)
    return KOS_OP_POST_ERR();

  PyObject *out = (res ? Py_True : Py_False);
  Py_INCREF(out);
  return kos_op_outcome_PUSH(es, out);
}
