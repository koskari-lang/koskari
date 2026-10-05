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
  BRACKET_PROPER: arg = count. Stack (bottom to top) = first element ...
  last element. Build the proper bracket list [elem_0 ... elem_{count-1}]
  terminated by bracket-nil, the twin of PROPER over bracket pairs:
  bracket-nil is pushed as the implicit tail, then BRACKET_CONS over
  count + 1 slots. count 0 pushes bracket-nil.
*/
KOS_OP_HANDLER(BRACKET_PROPER) {
  size_t count = (size_t) KosOp_ARG(insn);

  if (unlikely(KosEvalState_StackPushRef(es, KosBracketNil)))
    RESULT_RAISE();

  if (unlikely(count == 0))
    RESULT_NEXT(es);

  PyObject *const *ptr = KosEvalState_StackTailPtr(es, count + 1);
  if (unlikely(! ptr))
    RESULT_RAISE();

  PyObject *result = KosBracketPair_ConsFromPtr(ptr, (Py_ssize_t) (count + 1));
  if (unlikely(! result))
    RESULT_RAISE();

  KosEvalState_StackDropNoErr(es, count + 1);

  RESULT_STEAL(es, result);
}


// The end.
