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
   CONST: arg = constant index. Push the code object constant at that index
   onto the stack.
*/

KOS_OP_HANDLER(CONST) {
  KosContext *ctx = KosEvalState_CONTEXT(es);
  KosCode *code = CURRENT_CODE(ctx);
  uint32_t index = KosOp_ARG(insn);

  if (unlikely(index >= KosCode_CPOOL_LEN(code))) {
    PyErr_Format(PyExc_IndexError, "constant index out of bounds, %u",
                 index);
    return KOS_OP_POST_ERR();
  }

  PyObject *val = KosCode_GET_CONST(code, index);
  Py_INCREF(val);
  return kos_op_outcome_from_int_step(push_result(es, val));
}
