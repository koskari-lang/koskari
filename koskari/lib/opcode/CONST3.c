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
  CONST3: arg = extra index. Schema CONST x3. Push the three constants in
  row order. A constant is a borrowed pool reference, so nothing is
  held across a failed push.
*/
KOS_OP_HANDLER(CONST3) {
  KosCode *code = KosEvalState_CurrentCode(es);
  KosExtra row = KosCode_GetExtra(code, KosOp_ARG(insn));

  PyObject *a = KosEvalState_CONST(es, row.a0);
  if (unlikely(! a))
    RESULT_RAISE();

  PyObject *b = KosEvalState_CONST(es, row.a1);
  if (unlikely(! b))
    RESULT_RAISE();

  PyObject *c = KosEvalState_CONST(es, row.a2);
  if (unlikely(! c))
    RESULT_RAISE();

  if (unlikely(KosEvalState_StackPushRef(es, a) < 0))
    RESULT_RAISE();

  if (unlikely(KosEvalState_StackPushRef(es, b) < 0))
    RESULT_RAISE();

  RESULT_PUSH(es, c);
}


// The end.
