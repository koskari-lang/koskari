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
  ITEM_CONST: arg = constant index. Stack [obj]. Object from the
  stack, key from the pool. Push obj[key]. The access runs through
  kos_item_load (eval/eval_item.c) on the site's one KOS_CACHE_ITEM
  slot.
*/
KOS_OP_HANDLER(ITEM_CONST) {
  KosSegment **bl = KosEvalState_BLOCK(es);
  PyObject *const *tail;
  PyObject *obj;
  PyObject *key = KosOp_OBJ(insn);
  PyObject *out;

  tail = kos_stack_tail_ptr_fast(*bl, 1);
  if (unlikely(! tail))
    RESULT_RAISE();

  obj = tail[0];
  out = kos_item_load(KosOp_SLOT(insn), obj, key);

  if (unlikely(! out))
    RESULT_RAISE();

  kos_item_replace_top(bl, out);
  RESULT_NEXT(es);
}


// The end.
