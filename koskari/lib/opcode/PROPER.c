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
  PROPER: arg = count. Stack (bottom to top) = first element ... last element.
  Build a proper list (elem_0 . (elem_1 . ... (elem_{count-1} . nil))).
  Equivalent to pushing nil then calling CONS with count+1, but without
  requiring an explicit CONST nil before the CONS.
*/
KOS_OP_HANDLER(PROPER) {
  size_t count = (size_t) KosOp_ARG(insn);

  /* Push nil onto the stack as the implicit proper tail. */
  if (unlikely(KosEvalState_StackPushRef(es, KosNil)))
    RESULT_RAISE();

  /* PROPER 0 just returns nil, which we already pushed */
  if (unlikely(count == 0))
    RESULT_NEXT(es);

  /* Now the stack has count elements + nil = count+1 items. */
  PyObject *const *ptr = KosEvalState_StackTailPtr(es, count + 1);
  if (unlikely(! ptr))
    RESULT_RAISE();

  PyObject *result = KosPair_ConsFromPtr(ptr, (int)(count + 1));
  if (unlikely(! result))
    RESULT_RAISE();

  KosEvalState_StackDropNoErr(es, count + 1);

  RESULT_STEAL(es, result);
}


// The end.
