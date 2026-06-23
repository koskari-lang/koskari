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
   OBJECT_REPR: pop TOS, call PyObject_Repr, push result.
*/
KOS_OP_HANDLER(OBJECT_REPR) {
    PyObject *val = KosEvalState_StackPopRef(es);
    if (! val) return EVAL_RAISE();

    // PyObject *res = PyObject_Repr(val);
    PyObject *res = KosUtils_Repr(val);

    Py_DECREF(val);
    if (! res) return EVAL_RAISE();
    (void) insn;
    return kos_op_result_PUSH(es, res);
}


// The end.
