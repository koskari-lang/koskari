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
  RETURN_CONST: arg = pool index of the constant to return; the object
  is resolved onto the record at finalize.  The fusion of CONST c,
  RETURN 1: the same delivery as RETURN 1 with the constant in place of
  the popped value.  Stack unchanged.
*/
KOS_OP_HANDLER(RETURN_CONST) {
  KOS_SYNC_PC(ctx, insn);
  PyObject *val = KosOp_OBJ(insn);
  Py_INCREF(val);
  TAILCALL(kos_return_deliver, ctx, insn, val);
}


// The end.
