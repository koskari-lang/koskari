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
  OBJECT_COMPARE: arg = CMPCOUNT (KosCompare selector high byte, reduce
  count low byte). Chain-compare the top COUNT operands under the
  selector and push the result.
*/
KOS_OP_HANDLER(OBJECT_COMPARE) {
  uint16_t arg = KosOp_ARG(insn);
  size_t count = (size_t) KosCmpCount_COUNT(arg);

  if (unlikely(count < 2)) {
    PyErr_SetString(PyExc_TypeError,
                    "OBJECT_COMPARE requires at least 2 elements");
    return EVAL_RAISE();
  }

  PyObject *const *args = KosEvalState_StackTailPtr(es, count);
  if (unlikely(! args))
    return EVAL_RAISE();

  PyObject *result = kos_compare_reduce(
      (KosCompare) KosCmpCount_SEL(arg), args, count);
  KosEvalState_StackDropNoErr(es, count);

  if (unlikely(! result))
    return EVAL_RAISE();

  return kos_op_result_PUSH(es, result);
}


// The end.
