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
  CONST2: arg = extra index. Schema CONST, CONST. Push two code object
  constants in row order. Both constants resolve before either push, so
  a bad index raises with nothing pushed.

  Pool trust follows KosEvalState_CONST / KOS_EVAL_SAFETYNET.
*/
KOS_OP_HANDLER(CONST2) {
  KosCode *code = KosEvalState_CurrentCode(es);
  KosExtra row = KosCode_GetExtra(code, KosOp_ARG(insn));

  PyObject *a = KosEvalState_CONST(es, row.a0);
  if (unlikely(! a))
    return EVAL_RAISE();

  PyObject *b = KosEvalState_CONST(es, row.a1);
  if (unlikely(! b))
    return EVAL_RAISE();

  Py_INCREF(a);
  if (unlikely(KosEvalState_StackPUSH(es, a) < 0))
    return EVAL_RAISE();

  Py_INCREF(b);
  return kos_op_result_PUSH(es, b);
}


// The end.
