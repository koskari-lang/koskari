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
   PUSH_COLLECT: arg = constant pool index of NamespaceSpec. Does not
   consume the stack. Creates CollectorNamespace from the spec, pushes
   KOS_DYN_COLLECT. Body runs in collector; POP_COLLECT pushes the
   collected namespace onto the stack.
*/

KOS_OP_HANDLER(PUSH_COLLECT) {
  KosCode *code = KosEvalState_CurrentCode(es);
  uint32_t cidx = KosOp_ARG(insn);
  PyObject *nspec = NULL;
  PyObject *parent = CURRENT_NS();
  PyObject *new_ns = NULL;
  KosDynCollect *col_node = NULL;

  if (cidx >= KosCode_CPOOL_LEN(code)) {
    PyErr_Format(PyExc_IndexError, "PUSH_COLLECT constant index out of"
                 " bounds: %u", cidx);
    return KOS_OP_POST_ERR();
  }

  nspec = KosCode_GetConstant((PyObject *) code, cidx);
  if (! nspec) {
    return KOS_OP_POST_ERR();
  }

  if (! KosNamespaceSpec_CheckExact(nspec)) {
    PyErr_Format(PyExc_RuntimeError,
                 "PUSH_COLLECT spec must be a NamespaceSpec, not %R", nspec);
    return KOS_OP_POST_ERR();
  }

  PyObject *spec_parent = KosNamespace_Check(parent) ? parent : NULL;
  new_ns = KosCollectorNamespace_New(spec_parent, nspec);
  if (! new_ns) {
    return KOS_OP_POST_ERR();
  }

  col_node = kos_dyn_push_collect(es, new_ns);
  if (! col_node) {
    return KOS_OP_POST_ERR();
  }

  KosDynNode_STACK((KosDynNode *) col_node) = KosStackBlock_NewEmpty();
  if (! KosDynNode_STACK((KosDynNode *) col_node)) {
    kos_dyn_pop_collect(es, col_node);
    return KOS_OP_POST_ERR();
  }
  kos_dyn_rebuild_live_from_chain(es);

  (void) out_ev;
  return KOS_OP_RUN();
}


// The end.
