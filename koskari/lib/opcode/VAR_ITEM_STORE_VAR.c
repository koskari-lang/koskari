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
  VAR_ITEM_STORE_VAR: arg = extra index. Schema VAR, VAR, NOARG,
  NOARG. Stack [key]. Object from the frame namespace by name, key
  from the stack, value from the frame namespace by name. obj[key] =
  val; push nothing. The access runs through kos_item_store
  (eval/eval_item.c) with no slot; the cold twin of the FAST row.
*/
KOS_OP_HANDLER(VAR_ITEM_STORE_VAR) {
  const KosExtra *row = KosOp_ROW(insn);
  KosSegment **bl = KosEvalState_BLOCK(es);
  PyObject *const *tail;
  PyObject *obj;
  PyObject *key;
  PyObject *val;
  int rc;

  obj = frame_ns_deref(es, KosExtra_OBJ(row, 0));
  if (unlikely(! obj))
    RESULT_RAISE();
  val = frame_ns_deref(es, KosExtra_OBJ(row, 1));
  if (unlikely(! val)) {
    Py_DECREF(obj);
    RESULT_RAISE();
  }

  tail = kos_stack_tail_ptr_fast(*bl, 1);
  if (unlikely(! tail)) {
    Py_DECREF(obj);
    Py_DECREF(val);
    RESULT_RAISE();
  }

  key = tail[0];
  rc = kos_item_store(KosOp_SLOT(insn), obj, key, val);
  Py_DECREF(obj);

  if (unlikely(rc < 0))
    RESULT_RAISE();

  kos_stack_drop_noerr_fast(bl, 1);
  RESULT_NEXT(es);
}


// The end.
