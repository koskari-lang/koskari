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
  SWITCH_EQ_FAST_ATTR: arg = extra index. Schema (LABEL, FASTVAR, CONST, NOARG):
  row.a0 = target PC, row.a1 = fast slot index, row.a2 = attribute
  name cpool index. Stack [..., s] with scrutinee = TOS. Load obj from
  fast slot, fetch getattr(obj, attr) and compare to s via Py_EQ. On
  match: pop scrutinee and jump to row.a0. On mismatch: fall through
  with stack unchanged.
*/

KOS_OP_HANDLER(SWITCH_EQ_FAST_ATTR) {
  KOS_SYNC_PC(ctx, insn);
  const KosExtra *row = KosOp_ROW(insn);

  PyObject *ns = CURRENT_NS();
  KOS_OP_REQUIRE_NS(ns);
  PyObject *attr_name = KosExtra_OBJ(row, 2);

  PyObject *obj = kos_ns_fast_deref(ns, (size_t) KosExtra_IMM(row, 1));
  if (unlikely(! obj))
    RESULT_RAISE();

  PyObject *rhs = PyObject_GetAttr(obj, attr_name);
  Py_DECREF(obj);
  if (unlikely(! rhs))
    RESULT_RAISE();

  PyObject *value = KosContext_StackPeek(ctx);
  if (unlikely(! value)) {
    Py_DECREF(rhs);
    RESULT_RAISE();
  }

  int res = PyObject_RichCompareBool(value, rhs, Py_EQ);
  Py_DECREF(rhs);
  if (unlikely(res < 0))
    RESULT_RAISE();

  if (res) {
    KosContext_StackDropNoErr(ctx, 1);
    RESULT_JUMP_AT(ctx, KosExtra_TARGET(row, 0));
  }

  RESULT_NEXT(ctx);
}


// The end.
