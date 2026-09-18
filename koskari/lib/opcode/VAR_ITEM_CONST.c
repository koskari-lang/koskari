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
  VAR_ITEM_CONST: arg = extra index. Schema VAR, CONST, NOARG, NOARG.
  No stack operands. Object from the frame namespace by name, key
  from the pool. Push obj[key]. The access runs through kos_item_load
  (eval/eval_item.c) with no slot; the cold twin of the FAST row.
*/
KOS_OP_HANDLER(VAR_ITEM_CONST) {
  const KosExtra *row = KosOp_ROW(insn);
  PyObject *obj;
  PyObject *key = KosExtra_OBJ(row, 1);
  PyObject *out;

  obj = frame_ns_deref(es, KosExtra_OBJ(row, 0));
  if (unlikely(! obj))
    RESULT_RAISE();

  out = kos_item_load(KosOp_SLOT(insn), obj, key);
  Py_DECREF(obj);

  if (unlikely(! out))
    RESULT_RAISE();
  RESULT_STEAL(es, out);
}


// The end.
