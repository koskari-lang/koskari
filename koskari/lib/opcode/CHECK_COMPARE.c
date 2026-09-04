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
   CHECK_COMPARE: arg = extra index. Schema CMP (KosCompare selector),
   COUNT, LABEL. Chain-compare the top COUNT operands under the selector;
   jump to LABEL if the chain fails. The operands are dropped either way.
*/

KOS_OP_HANDLER(CHECK_COMPARE) {
  const KosExtra *row = KosOp_ROW(insn);
  size_t count = (size_t) KosExtra_IMM(row, 1);

  if (unlikely(count < 2)) {
    PyErr_SetString(PyExc_TypeError,
                    "CHECK_COMPARE requires at least 2 elements");
    RESULT_RAISE();
  }

  PyObject *const *args = KosEvalState_StackTailPtr(es, count);
  if (unlikely(! args))
    RESULT_RAISE();

  int res = kos_compare_chain((KosCompare) KosExtra_IMM(row, 0), args, count);
  KosEvalState_StackDropNoErr(es, count);

  if (unlikely(res < 0))
    RESULT_RAISE();

  if (res == 0)
    RESULT_JUMP(es, KosExtra_IMM(row, 2));

  RESULT_NEXT(es);
}


// The end.
