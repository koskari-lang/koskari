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
  JUMP_AND_FAST: arg = extra index. Schema FASTVAR (slot), LABEL
  (target pc). Short-circuit and: read the fast slot; if the value is
  falsy, push it and jump, else fall through with nothing pushed.
  Raises NameError when the slot is empty, as FAST would. The
  convert_fast lowering of JUMP_AND_VAR.
*/
KOS_OP_HANDLER(JUMP_AND_FAST) {
  KOS_SYNC_PC(ctx, insn);
  const KosExtra *row = KosOp_ROW(insn);
  PyObject *ns = CURRENT_NS();
  KOS_OP_REQUIRE_NS(ns);

  /* Borrowed arm: a bool or None settles on its identity, the push
     taking its own reference and the fall-through touching none. Any
     other value takes the owned path, which raises for an empty slot. */
  PyObject *val = kos_ns_fast_peek(ns, (size_t) KosExtra_IMM(row, 0));
  if (val == Py_False || val == Py_None) {
    if (unlikely(KosEvalState_StackPushRef(ctx, val) < 0))
      RESULT_RAISE();
    RESULT_JUMP_AT(ctx, KosExtra_TARGET(row, 1));
  }
  if (val == Py_True)
    RESULT_NEXT(ctx);

  val = kos_ns_fast_deref(ns, (size_t) KosExtra_IMM(row, 0));
  if (unlikely(! val))
    RESULT_RAISE();

  int res = kos_object_not(val);
  if (unlikely(res < 0)) {
    Py_DECREF(val);
    RESULT_RAISE();
  }

  if (res) {
    if (unlikely(KosEvalState_StackPUSH(ctx, val) < 0))
      RESULT_RAISE();
    RESULT_JUMP_AT(ctx, KosExtra_TARGET(row, 1));
  }

  Py_DECREF(val);
  RESULT_NEXT(ctx);
}


// The end.
