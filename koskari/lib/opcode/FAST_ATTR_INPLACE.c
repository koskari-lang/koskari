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
   FAST_ATTR_INPLACE: arg = EXTRA row index. Schema OPCOUNT (op-type + count),
   FASTVAR (slot), CONST (attr name). obj = fast-slot deref; the stack holds the
   count reduction values [v0, ..., v_{count-1}]. Reduce obj.attr in place with
   the operator and store it back; pushes nothing (the special appends CONST
   None). The lowered form of VAR_ATTR_INPLACE. Hot: the row decode and cache
   lookup are inlined; the op-type switch stays out of line in the shared
   kos_inplace_reduce.

   Carries one per-site field cache (cache arity 1): a single resolved storage
   location serves both the read of obj.attr and the write-back, so a hit skips
   the getattr/setattr protocol on both ends. kos_attr_inplace_cached does the
   read-modify-write; the op-type and count travel in the OPCOUNT slot.
*/

KOS_OP_HANDLER(FAST_ATTR_INPLACE) {
  KosCode *code = KosEvalState_CurrentCode(es);
  KosExtra row = KosCode_GetExtra(code, KosOp_ARG(insn));
  KosOperand op = (KosOperand) KosOpCount_TYPE(row.a0);
  int count = (int) KosOpCount_COUNT(row.a0);
  size_t idx = (size_t) row.a1;
  PyObject *attr = KosEvalState_CONST(es, row.a2);
  if (! attr)
    return EVAL_RAISE();

  PyObject *ns = CURRENT_NS();
  KOS_OP_REQUIRE_NS(ns);
  PyObject *obj = kos_ns_fast_deref(ns, idx);
  if (! obj)
    return EVAL_RAISE();

  PyObject *const *args =
      count ? KosEvalState_StackTailPtr(es, (size_t) count) : NULL;
  if (count && ! args) {
    Py_DECREF(obj);
    return EVAL_RAISE();
  }

  uint16_t cache_ix = KosOp_CACHE(insn);
  KosCacheSlot *slot =
      (cache_ix != KOS_CACHE_UNSET) ? &KosCode_CACHE(code)[cache_ix] : NULL;

  int res = kos_attr_inplace_cached(slot, obj, attr, op, args, count);
  KosEvalState_StackDropNoErr(es, (size_t) count);
  Py_DECREF(obj);
  return kos_op_result_from_push_int(res);
}


// The end.
