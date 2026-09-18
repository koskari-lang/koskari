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
   ITEM_CONST_STORE_FAST: arg = extra index. Schema CONST, FASTVAR,
   NOARG, NOARG. Stack [obj]. Object from the stack, key from the
   pool, value from a fast slot. obj[key] = val; push nothing. The
   access runs through kos_item_store (eval/eval_item.c) on the
   site's one KOS_CACHE_ITEM slot.
*/

KOS_OP_HANDLER(ITEM_CONST_STORE_FAST) {
  const KosExtra *row = KosOp_ROW(insn);
  PyObject *ns = KosEvalState_NAMESPACE(es);
  KosSegment **bl = KosEvalState_BLOCK(es);
  PyObject *const *tail;
  PyObject *obj;
  PyObject *key = KosExtra_OBJ(row, 0);
  PyObject *val;
  int rc;

  KOS_OP_REQUIRE_NS(ns);
  val = kos_ns_fast_deref(ns, (size_t) KosExtra_IMM(row, 1));
  if (unlikely(! val))
    RESULT_RAISE();
  tail = kos_stack_tail_ptr_fast(*bl, 1);
  if (unlikely(! tail)) {
    Py_DECREF(val);
    RESULT_RAISE();
  }
  obj = tail[0];
  rc = kos_item_store(KosOp_SLOT(insn), obj, key, val);
  if (unlikely(rc < 0))
    RESULT_RAISE();
  kos_stack_drop_noerr_fast(bl, 1);
  RESULT_NEXT(es);
}


// The end.
