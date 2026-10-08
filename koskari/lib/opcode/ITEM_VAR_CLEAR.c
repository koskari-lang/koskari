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
  ITEM_VAR_CLEAR: arg = name index. Stack [obj]. Object from the
  stack, key from the frame namespace by name. del obj[key]; push
  nothing. The access runs through kos_item_clear (eval/eval_item.c).
*/
KOS_OP_HANDLER(ITEM_VAR_CLEAR) {
  KOS_SYNC_PC(es, insn);
  KosSegment **bl = KosEvalState_BLOCK(es);
  PyObject *const *tail;
  PyObject *obj;
  PyObject *key;
  int rc;

  key = frame_ns_deref(es, KosOp_OBJ(insn));
  if (unlikely(! key))
    RESULT_RAISE();

  tail = kos_stack_tail_ptr_fast(*bl, 1);
  if (unlikely(! tail)) {
    Py_DECREF(key);
    RESULT_RAISE();
  }

  obj = tail[0];
  rc = kos_item_clear(obj, key);
  Py_DECREF(key);

  if (unlikely(rc < 0))
    RESULT_RAISE();

  kos_stack_drop_noerr_fast(bl, 1);
  RESULT_NEXT(es);
}


// The end.
