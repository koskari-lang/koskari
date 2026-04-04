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
   POP_COLLECT: arg unused. Verify dyn_top is KOS_DYN_COLLECT, restore
   frame ns from saved_ns, pop the node, push the collected namespace
   onto the value stack.
*/

KOS_OP_HANDLER(POP_COLLECT) {
  (void) out_ev;
  (void) insn;

  if (unlikely(! KosContext_DYN_TOP(ctx) ||
               ! KosDynNode_IS_COLLECT(KosContext_DYN_TOP(ctx)))) {
    PyErr_SetString(PyExc_RuntimeError,
                    "POP_COLLECT: top is not a collector scope");
    return KOS_OP_POST_ERR();
  }

  KosDynCollect *col_node = (KosDynCollect *) KosContext_DYN_TOP(ctx);
  PyObject *prior = KosDynCollect_SAVED_NS(col_node);
  PyObject *collector = CURRENT_NS(ctx);
  Py_INCREF(prior);
  Py_INCREF(collector);
  Py_DECREF(CURRENT_NS(ctx));
  CURRENT_NS(ctx) = prior;

  if (KosStackBlock_DropBlock(&CURRENT_STACK(ctx)) < 0) {
    Py_DECREF(collector);
    return KOS_OP_POST_ERR();
  }
  kos_dyn_pop_collect(ctx, col_node);

  int res = KosStackBlock_PushRef(&CURRENT_STACK(ctx), collector);
  Py_DECREF(collector);

  return kos_op_outcome_from_int_step(res);
}


// The end.
