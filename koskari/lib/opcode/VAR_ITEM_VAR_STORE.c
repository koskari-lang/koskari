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
  VAR_ITEM_VAR_STORE: arg = extra index. Schema VAR, VAR, NOARG,
  NOARG. Stack [val]. Object from the frame namespace by name, key
  from the frame namespace by name, value from the stack. obj[key] =
  val; push nothing. The access runs through kos_item_store
  (eval/eval_item.c) with no slot; the cold twin of the FAST row.
*/
KOS_OP_HANDLER(VAR_ITEM_VAR_STORE) {
  const KosExtra *row = KosOp_ROW(insn);
  KosSegment **bl = KosEvalState_BLOCK(es);
  PyObject *obj;
  PyObject *key;
  PyObject *val;
  int rc;

  obj = frame_ns_deref(es, KosExtra_OBJ(row, 0));
  if (unlikely(! obj))
    RESULT_RAISE();
  key = frame_ns_deref(es, KosExtra_OBJ(row, 1));
  if (unlikely(! key)) {
    Py_DECREF(obj);
    RESULT_RAISE();
  }

  val = kos_stack_pop_ref_fast(bl);
  if (unlikely(! val)) {
    Py_DECREF(obj);
    Py_DECREF(key);
    RESULT_RAISE();
  }

  rc = kos_item_store(KosOp_SLOT(insn), obj, key, val);
  Py_DECREF(obj);
  Py_DECREF(key);

  if (unlikely(rc < 0))
    RESULT_RAISE();
  RESULT_NEXT(es);
}


// The end.
