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
  FAST_ITEM_FAST_INPLACE_CONST: arg = extra index. Schema OPERAND,
  FASTVAR, FASTVAR, CONST. No stack operands. Object from a fast
  slot, key from a fast slot, operand from the pool. obj[key] =
  obj[key] OP operands, reduced left to right; push nothing. The
  access runs through kos_item_inplace (eval/eval_item.c) on the
  site's one KOS_CACHE_ITEM slot.
*/
KOS_OP_HANDLER(FAST_ITEM_FAST_INPLACE_CONST) {
  KOS_SYNC_PC(ctx, insn);
  const KosExtra *row = KosOp_ROW(insn);
  PyObject *ns = KosContext_NAMESPACE(ctx);
  KosOperand op = (KosOperand) KosExtra_IMM(row, 0);
  PyObject *rhs = KosExtra_OBJ(row, 3);
  PyObject *obj, *obj_ref;
  PyObject *key, *key_ref;
  int rc;

  KOS_OP_REQUIRE_NS(ns);
  KOS_NS_FAST_BORROW(obj, obj_ref, ns, KosExtra_IMM(row, 1),
                     { RESULT_RAISE(); });
  KOS_NS_FAST_BORROW_HOLDING(key, key_ref, ns, KosExtra_IMM(row, 2),
                             { KOS_NS_HOLD(obj, obj_ref); },
                             { Py_XDECREF(obj_ref); RESULT_RAISE(); });

  rc = kos_item_inplace(KosOp_SLOT(insn), obj, key, op, &rhs, 1);
  Py_XDECREF(obj_ref);
  Py_XDECREF(key_ref);

  if (unlikely(rc < 0))
    RESULT_RAISE();
  RESULT_NEXT(ctx);
}


// The end.
