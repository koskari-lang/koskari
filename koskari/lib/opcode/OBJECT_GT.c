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
   OBJECT_GT: arg unused. Pop right then left, push (left > right).
*/

KOS_OP_HANDLER(OBJECT_GT) {
  int count = (int) KosOp_ARG(insn);

  if (count < 2) {
    PyErr_Format(PyExc_TypeError, "OBJECT_GT requires at least 2"
                 " elements");
    return KOS_OP_POST_ERR();
  }

  PyObject *const *args = KosEvalState_StackTailPtr(es, count);
  if (unlikely(! args))
    return KOS_OP_POST_ERR();

  PyObject *result = KosOperator_Gt(args, count);
  KosEvalState_StackDropNoErr(es, count);
  if (unlikely(! result))
    return KOS_OP_POST_ERR();

  return kos_op_outcome_PUSH(es, result);
}
