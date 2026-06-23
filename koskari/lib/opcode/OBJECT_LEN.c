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
   OBJECT_LEN: arg unused. Pop TOS, push len(TOS) as an integer.
*/

KOS_OP_HANDLER(OBJECT_LEN) {
  PyObject *val = KosEvalState_StackPopRef(es);
  if (! val)
    return EVAL_RAISE();

  Py_ssize_t n = PyObject_Length(val);
  Py_DECREF(val);
  if (n < 0)
    return EVAL_RAISE();

  PyObject *res = PyLong_FromSsize_t(n);
  if (! res)
    return EVAL_RAISE();
  (void) insn;
  return kos_op_result_PUSH(es, res);
}


// The end.
