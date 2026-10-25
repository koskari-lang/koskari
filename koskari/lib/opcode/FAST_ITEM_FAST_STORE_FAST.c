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
  FAST_ITEM_FAST_STORE_FAST: arg = extra index. Schema FASTVAR,
  FASTVAR, FASTVAR, NOARG. No stack operands. Object from a fast
  slot, key from a fast slot, value from a fast slot. obj[key] = val;
  push nothing. The access runs through kos_item_store
  (eval/eval_item.c) on the site's one KOS_CACHE_ITEM slot.
*/
KOS_OP_HANDLER(FAST_ITEM_FAST_STORE_FAST) {
  KOS_SYNC_PC(ctx, insn);
  const KosExtra *row = KosOp_ROW(insn);
  PyObject *ns = KosContext_NAMESPACE(ctx);
  PyObject *obj, *obj_ref;
  PyObject *key, *key_ref;
  PyObject *val;
  int rc;

  KOS_OP_REQUIRE_NS(ns);
  KOS_NS_FAST_BORROW(obj, obj_ref, ns, KosExtra_IMM(row, 0),
                     { RESULT_RAISE(); });
  KOS_NS_FAST_BORROW_HOLDING(key, key_ref, ns, KosExtra_IMM(row, 1),
                             { KOS_NS_HOLD(obj, obj_ref); },
                             { Py_XDECREF(obj_ref); RESULT_RAISE(); });
  KOS_NS_FAST_DEREF_HOLDING(val, ns, KosExtra_IMM(row, 2),
                            { KOS_NS_HOLD(obj, obj_ref);
                              KOS_NS_HOLD(key, key_ref); },
                            { Py_XDECREF(obj_ref);
                              Py_XDECREF(key_ref);
                              RESULT_RAISE(); });

  rc = kos_item_store(KosOp_SLOT(insn), obj, key, val);
  Py_XDECREF(obj_ref);
  Py_XDECREF(key_ref);

  if (unlikely(rc < 0))
    RESULT_RAISE();
  RESULT_NEXT(ctx);
}


// The end.
