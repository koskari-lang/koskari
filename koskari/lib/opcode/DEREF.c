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
  DEREF: no arg. Pop one value (a ref). Call KosRef_Deref() for C-level
  dispatch; push result. Fails if TOS has no deref handler.
*/
KOS_OP_HANDLER(DEREF) {
  KOS_SYNC_PC(ctx, insn);
  (void) insn;

  PyObject *ref = KosEvalState_StackPopRef(ctx);
  if (unlikely(! ref))
    RESULT_RAISE();

  PyObject *val = KosRef_Deref(ref);
  Py_DECREF(ref);

  if (unlikely(! val))
    RESULT_RAISE();

  RESULT_STEAL(ctx, val);
}


// The end.
