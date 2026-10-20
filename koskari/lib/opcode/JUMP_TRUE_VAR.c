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
  JUMP_TRUE_VAR: arg = extra index. Schema VAR (name), LABEL (target
  pc). Branch if the value the name resolves to in the current frame's
  namespace is truthy, raising NameError when it is unbound, as VAR
  would. The fusion of VAR name, JUMP_TRUE; lowers to
  JUMP_TRUE_FAST when the name has a fast slot. Stack unchanged.
*/
KOS_OP_HANDLER(JUMP_TRUE_VAR) {
  KOS_SYNC_PC(ctx, insn);
  const KosExtra *row = KosOp_ROW(insn);
  PyObject *val = frame_ns_deref(ctx, KosExtra_OBJ(row, 0));
  if (unlikely(! val))
    RESULT_RAISE();

  int res = kos_object_truth(val);
  Py_DECREF(val);

  if (unlikely(res < 0))
    RESULT_RAISE();
  if (res)
    RESULT_JUMP_AT(ctx, KosExtra_TARGET(row, 1));

  RESULT_NEXT(ctx);
}


// The end.
