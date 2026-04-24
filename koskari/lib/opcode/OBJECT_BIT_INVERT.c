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
   OBJECT_BIT_INVERT: pop TOS, call PyNumber_Invert, push result.
*/
KOS_OP_HANDLER(OBJECT_BIT_INVERT) {
    PyObject *val = KosEvalState_StackPopRef(es);
    if (! val) return KOS_OP_POST_ERR();
    PyObject *res = PyNumber_Invert(val);
    Py_DECREF(val);
    if (! res) return KOS_OP_POST_ERR();
    (void) insn;
    return kos_op_outcome_from_int_step(push_result(es, res));
}
