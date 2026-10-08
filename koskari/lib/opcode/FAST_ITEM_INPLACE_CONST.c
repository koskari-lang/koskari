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
  FAST_ITEM_INPLACE_CONST: arg = extra index. Schema OPERAND,
  FASTVAR, CONST, NOARG. Stack [key]. Object from a fast slot, key
  from the stack, operand from the pool. obj[key] = obj[key] OP
  operands, reduced left to right; push nothing. The access runs
  through kos_item_inplace (eval/eval_item.c) on the site's one
  KOS_CACHE_ITEM slot.
*/
KOS_OP_HANDLER(FAST_ITEM_INPLACE_CONST) {
  KOS_SYNC_PC(es, insn);
  const KosExtra *row = KosOp_ROW(insn);
  PyObject *ns = KosEvalState_NAMESPACE(es);
  KosSegment **bl = KosEvalState_BLOCK(es);
  PyObject *const *tail;
  KosOperand op = (KosOperand) KosExtra_IMM(row, 0);
  PyObject *rhs = KosExtra_OBJ(row, 2);
  PyObject *obj, *obj_ref;
  PyObject *key;
  int rc;

  KOS_OP_REQUIRE_NS(ns);
  KOS_NS_FAST_BORROW(obj, obj_ref, ns, KosExtra_IMM(row, 1),
                     { RESULT_RAISE(); });

  tail = kos_stack_tail_ptr_fast(*bl, 1);
  if (unlikely(! tail)) {
    Py_XDECREF(obj_ref);
    RESULT_RAISE();
  }

  key = tail[0];
  rc = kos_item_inplace(KosOp_SLOT(insn), obj, key, op, &rhs, 1);
  Py_XDECREF(obj_ref);

  if (unlikely(rc < 0))
    RESULT_RAISE();

  kos_stack_drop_noerr_fast(bl, 1);
  RESULT_NEXT(es);
}


// The end.
