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
  JUMP_OR_VAR: arg = extra index. Schema VAR (name), LABEL (target
  pc). Short-circuit or: read the value the name resolves to in the
  current frame's namespace; if truthy, push it and jump, else fall
  through with nothing pushed. Raises NameError when the name is
  unbound, as VAR would. The fusion of VAR name, JUMP_OR; lowers to
  JUMP_OR_FAST when the name has a fast slot.
*/
KOS_OP_HANDLER(JUMP_OR_VAR) {
  KOS_SYNC_PC(ctx, insn);
  const KosExtra *row = KosOp_ROW(insn);
  PyObject *val = frame_ns_deref(ctx, KosExtra_OBJ(row, 0));
  if (unlikely(! val))
    RESULT_RAISE();

  int res = kos_object_truth(val);
  if (unlikely(res < 0)) {
    Py_DECREF(val);
    RESULT_RAISE();
  }

  if (res) {
    if (unlikely(KosContext_StackPUSH(ctx, val) < 0))
      RESULT_RAISE();
    RESULT_JUMP_AT(ctx, KosExtra_TARGET(row, 1));
  }

  Py_DECREF(val);
  RESULT_NEXT(ctx);
}


// The end.
