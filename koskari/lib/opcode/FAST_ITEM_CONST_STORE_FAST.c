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
   FAST_ITEM_CONST_STORE_FAST: arg = extra index. Schema FASTVAR,
   CONST, FASTVAR, NOARG. No stack operands. Object from a fast slot,
   key from the pool, value from a fast slot. obj[key] = val; push
   nothing. The access runs through kos_item_store (eval/eval_item.c)
   on the site's one KOS_CACHE_ITEM slot.
*/

KOS_OP_HANDLER(FAST_ITEM_CONST_STORE_FAST) {
  const KosExtra *row = KosOp_ROW(insn);
  PyObject *ns = KosEvalState_NAMESPACE(es);
  PyObject *obj, *obj_ref;
  PyObject *key = KosExtra_OBJ(row, 1);
  PyObject *val;
  int rc;

  KOS_OP_REQUIRE_NS(ns);
  KOS_NS_FAST_BORROW(obj, obj_ref, ns, KosExtra_IMM(row, 0),
                     { RESULT_RAISE(); });
  val = kos_ns_fast_deref(ns, (size_t) KosExtra_IMM(row, 2));
  if (unlikely(! val)) {
    Py_XDECREF(obj_ref);
    RESULT_RAISE();
  }
  rc = kos_item_store(KosOp_SLOT(insn), obj, key, val);
  Py_XDECREF(obj_ref);
  if (unlikely(rc < 0))
    RESULT_RAISE();
  RESULT_NEXT(es);
}


// The end.
