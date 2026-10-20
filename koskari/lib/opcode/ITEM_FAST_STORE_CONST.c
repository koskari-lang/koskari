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
  ITEM_FAST_STORE_CONST: arg = extra index. Schema FASTVAR, CONST,
  NOARG, NOARG. Stack [obj]. Object from the stack, key from a fast
  slot, value from the pool. obj[key] = val; push nothing. The access
  runs through kos_item_store (eval/eval_item.c) on the site's one
  KOS_CACHE_ITEM slot.
*/
KOS_OP_HANDLER(ITEM_FAST_STORE_CONST) {
  KOS_SYNC_PC(ctx, insn);
  const KosExtra *row = KosOp_ROW(insn);
  PyObject *ns = KosContext_NAMESPACE(ctx);
  KosSegment **bl = KosContext_BLOCK(ctx);
  PyObject *const *tail;
  PyObject *obj;
  PyObject *key, *key_ref;
  PyObject *val;
  int rc;

  KOS_OP_REQUIRE_NS(ns);
  KOS_NS_FAST_BORROW(key, key_ref, ns, KosExtra_IMM(row, 0),
                     { RESULT_RAISE(); });
  val = KosExtra_OBJ(row, 1);
  Py_INCREF(val);

  tail = kos_stack_tail_ptr_fast(*bl, 1);
  if (unlikely(! tail)) {
    Py_XDECREF(key_ref);
    Py_DECREF(val);
    RESULT_RAISE();
  }

  obj = tail[0];
  rc = kos_item_store(KosOp_SLOT(insn), obj, key, val);
  Py_XDECREF(key_ref);

  if (unlikely(rc < 0))
    RESULT_RAISE();

  kos_stack_drop_noerr_fast(bl, 1);
  RESULT_NEXT(ctx);
}


// The end.
