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
   ATTR_INPLACE: arg = EXTRA row index. Schema OPCOUNT (op-type + count),
   CONST (attr name). The object sits on the stack below the count reduction
   values: [obj, v0, ..., v_{count-1}]. Reduce obj.attr in place with the
   operator and store it back; pushes nothing (the special appends CONST None).
   Cold: the op-type switch lives in the shared kos_inplace_reduce.

   Carries one per-site field cache (cache arity 1), like the FAST / VAR attr
   in-place variants: a single resolved storage location serves both the read
   of obj.attr and the write-back. obj comes off the stack rather than a named
   local, so this site may be polymorphic; the field cache degrades to its
   megamorphic bypass when it is.
*/

KOS_OP_HANDLER_COLD(ATTR_INPLACE) {
  KosCode *code = KosEvalState_CurrentCode(es);
  KosExtra row = KosCode_GetExtra(code, KosOp_ARG(insn));
  KosOperand op = (KosOperand) KosOpCount_TYPE(row.a0);
  int count = (int) KosOpCount_COUNT(row.a0);
  PyObject *attr = KosEvalState_CONST(es, row.a1);
  if (! attr)
    return EVAL_RAISE();

  PyObject *const *tail = KosEvalState_StackTailPtr(es, (size_t) (count + 1));
  if (! tail)
    return EVAL_RAISE();

  uint16_t cache_ix = KosOp_CACHE(insn);
  KosCacheSlot *slot =
      (cache_ix != KOS_CACHE_UNSET) ? &KosCode_CACHE(code)[cache_ix] : NULL;

  int res = kos_attr_inplace_cached(slot, tail[0], attr, op, &tail[1], count);
  KosEvalState_StackDropNoErr(es, (size_t) (count + 1));
  return kos_op_result_from_push_int(res);
}


// The end.
