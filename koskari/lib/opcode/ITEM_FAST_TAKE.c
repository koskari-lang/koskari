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
  ITEM_FAST_TAKE: arg = fast slot. Stack [obj]. Object from the
  stack, key from a fast slot. Push obj[key], then del obj[key]. The
  access runs through kos_item_take (eval/eval_item.c).
*/
KOS_OP_HANDLER(ITEM_FAST_TAKE) {
  KOS_SYNC_PC(ctx, insn);
  PyObject *ns = KosEvalState_NAMESPACE(ctx);
  KosSegment **bl = KosEvalState_BLOCK(ctx);
  PyObject *const *tail;
  PyObject *obj;
  PyObject *key, *key_ref;
  PyObject *out;

  KOS_OP_REQUIRE_NS(ns);
  KOS_NS_FAST_BORROW(key, key_ref, ns, KosOp_ARG(insn),
                     { RESULT_RAISE(); });

  tail = kos_stack_tail_ptr_fast(*bl, 1);
  if (unlikely(! tail)) {
    Py_XDECREF(key_ref);
    RESULT_RAISE();
  }

  obj = tail[0];
  out = kos_item_take(obj, key);
  Py_XDECREF(key_ref);

  if (unlikely(! out))
    RESULT_RAISE();

  kos_item_replace_top(bl, out);
  RESULT_NEXT(ctx);
}


// The end.
