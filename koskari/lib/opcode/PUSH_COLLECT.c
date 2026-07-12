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
  uint32_t cidx = KosOp_ARG(insn);
  PyObject *nspec = NULL;
  PyObject *parent = CURRENT_NS();
  PyObject *new_ns = NULL;

  nspec = KosEvalState_CONST(es, cidx);
  if (! nspec) {
    return EVAL_RAISE();
  }

  if (! KosNamespaceSpec_CheckExact(nspec)) {
    PyErr_Format(PyExc_RuntimeError,
                 "PUSH_COLLECT spec must be a NamespaceSpec, not %R", nspec);
    return EVAL_RAISE();
  }

  PyObject *spec_parent = KosNamespace_Check(parent) ? parent : NULL;
  new_ns = KosCollectorNamespace_New(spec_parent, nspec);
  if (! new_ns) {
    return EVAL_RAISE();
  }

  KosDynCollect *const col_node = kos_dyn_push_collect(es, new_ns);
  if (! col_node)
    return EVAL_RAISE();

  return EVAL_NORMAL();
}


// The end.
