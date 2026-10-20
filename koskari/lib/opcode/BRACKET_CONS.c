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
  BRACKET_CONS: arg = count. Stack (bottom to top) = first element ...
  tail. Build the bracket pair chain
  [elem_0 . [elem_1 . ... [elem_{count-1} . tail]]], the twin of CONS
  over bracket pairs. count 0 pushes bracket-nil.
*/
KOS_OP_HANDLER(BRACKET_CONS) {
  KOS_SYNC_PC(ctx, insn);
  size_t count = (size_t) KosOp_ARG(insn);

  if (count == 0)
    RESULT_PUSH(ctx, KosBracketNil);

  PyObject *const *ptr = KosContext_StackTailPtr(ctx, count);
  if (! ptr)
    RESULT_RAISE();

  PyObject *result = KosBracketPair_ConsFromPtr(ptr, (Py_ssize_t) count);
  if (! result)
    RESULT_RAISE();

  if (KosContext_StackDrop(ctx, count) < 0) {
    Py_DECREF(result);
    RESULT_RAISE();
  }

  RESULT_STEAL(ctx, result);
}


// The end.
