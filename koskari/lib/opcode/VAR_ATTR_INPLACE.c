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
   VAR_ATTR_INPLACE: arg = EXTRA row index. Schema OPCOUNT (op-type + count),
   VAR (obj name), CONST (attr name). obj = frame_ns_deref(name); the stack
   holds the count reduction values [v0, ..., v_{count-1}]. Reduce obj.attr in
   place with the operator and store it back; pushes nothing (the special
   appends CONST None). Resolution lowers this to FAST_ATTR_INPLACE when the
   name resolves to a fast slot. Cold: the op-type switch lives in the shared
   kos_inplace_reduce.

   Carries one per-site field cache (cache arity 1), like FAST_ATTR_INPLACE: a
   single resolved storage location serves both the read of obj.attr and the
   write-back. The receiver name is resolved through the frame namespace; the
   field cache is on obj.attr, independent of how obj itself is looked up.
*/

KOS_OP_HANDLER(VAR_ATTR_INPLACE) {
  KOS_SYNC_PC(ctx, insn);
  const KosExtra *row = KosOp_ROW(insn);
  KosOperand op = (KosOperand) KosOpCount_TYPE(KosExtra_IMM(row, 0));
  int count = (int) KosOpCount_COUNT(KosExtra_IMM(row, 0));
  PyObject *name = KosExtra_OBJ(row, 1);
  PyObject *attr = KosExtra_OBJ(row, 2);

  PyObject *obj = frame_ns_deref(ctx, name);
  if (! obj)
    RESULT_RAISE();

  PyObject *const *args =
      count ? KosEvalState_StackTailPtr(ctx, (size_t) count) : NULL;
  if (count && ! args) {
    Py_DECREF(obj);
    RESULT_RAISE();
  }

  KosCacheSlot *slot = KosOp_SLOT(insn);

  int res = kos_attr_inplace_cached(slot, obj, attr, op, args, count);
  KosEvalState_StackDropNoErr(ctx, (size_t) count);
  Py_DECREF(obj);
  if (unlikely(res < 0))
    RESULT_RAISE();
  RESULT_NEXT(ctx);
}


// The end.
