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
  PyObject *val = KosCode_GetConstant((PyObject *) CURRENT_CODE(ctx),
                                      KosOp_ARG(insn));
  if (! val) {
    SAYF("[eval] CONST GetConstant failed arg=%u\n",
         (unsigned) KosOp_ARG(insn));
    return -1;
  }
  if (PyLong_Check(val))
    SAYF("[eval] CONST pushed %ld\n", PyLong_AsLong(val));
  Py_INCREF(val);
  return push_result(ctx, val);
}
