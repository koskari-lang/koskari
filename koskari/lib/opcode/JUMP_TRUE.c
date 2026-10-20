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
  JUMP_TRUE: arg = target PC. Branch if TOS is true.
*/
KOS_OP_HANDLER(JUMP_TRUE) {
  PyObject *val = KosEvalState_StackPopRef(ctx);
  if (unlikely(! val))
    RESULT_RAISE_AT(ctx, insn);

  int res = kos_object_truth(val);
  Py_DECREF(val);

  if (unlikely(res < 0))
    RESULT_RAISE_AT(ctx, insn);

  if (res)
    RESULT_JUMP_AT(ctx, KosOp_TARGET(insn));

  RESULT_NEXT(ctx);
}


// The end.
