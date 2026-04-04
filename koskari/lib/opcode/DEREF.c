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
   DEREF: no arg. Pop one value (a ref with deref method). Call ref.deref(),
   push result. Fails if TOS has no deref method.

   :author: Christopher O'Brien <obriencj@preoccupied.net>
   :license: GNU General Public License v3
   :ai-assistant: Claude Opus 4.5 via Cursor
*/

KOS_OP_HANDLER(DEREF) {
  PyObject *ref = KosStackBlock_PopRef(&CURRENT_STACK(ctx));
  if (! ref)
    return KOS_OP_POST_ERR();

  PyObject *val = PyObject_CallMethod(ref, "deref", NULL);
  Py_DECREF(ref);

  if (! val)
    return KOS_OP_POST_ERR();

  return kos_op_outcome_from_int_step(push_result(ctx, val));
}
