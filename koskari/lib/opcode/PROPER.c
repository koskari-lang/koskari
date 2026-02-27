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

  if (count == 0) {
    Py_INCREF(KosNil);
    return push_result(ctx, KosNil);
  }

  /* Push nil onto the stack as the implicit proper tail. */
  Py_INCREF(KosNil);
  if (KosStackBlock_PushRef(&CURRENT_STACK(ctx), KosNil) < 0)
    return -1;

  /* Now the stack has count elements + nil = count+1 items. */
  PyObject *const *ptr = KosStackBlock_TailPtr(CURRENT_STACK(ctx), count + 1);
  if (! ptr)
    return -1;

  PyObject *result = KosPair_ConsFromPtr(ptr, (int)(count + 1));
  if (! result)
    return -1;

  if (KosStackBlock_Drop(&CURRENT_STACK(ctx), count + 1) < 0) {
    Py_DECREF(result);
    return -1;
  }

  return push_result(ctx, result);
}
