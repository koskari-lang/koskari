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

  Pool trust follows KosEvalState_CONST / KOS_EVAL_SAFETYNET.
*/
KOS_OP_HANDLER(CONST) {
  PyObject *val = KosEvalState_CONST(es, KosOp_ARG(insn));
  if (unlikely(! val))
    RESULT_RAISE();

  RESULT_PUSH(es, val);
}


// The end.
