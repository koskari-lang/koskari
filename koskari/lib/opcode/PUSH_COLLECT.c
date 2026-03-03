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
  KosCode *code = CURRENT_CODE(ctx);
  uint32_t cidx = KosOp_ARG(insn);
  PyObject *nspec = NULL;
  PyObject *parent = CURRENT_NS(ctx);
  PyObject *new_ns = NULL;
  KosDynCollect *col_node = NULL;

  if (cidx >= KosCode_CPOOL_LEN(code)) {
    PyErr_Format(PyExc_IndexError, "PUSH_COLLECT constant index out of"
                 " bounds: %u", cidx);
    return -1;
  }

  nspec = KosCode_GetConstant((PyObject *) code, cidx);
  if (! nspec) {
    return -1;
  }

  if (! KosNamespaceSpec_CheckExact(nspec)) {
    PyErr_Format(PyExc_RuntimeError,
                 "PUSH_COLLECT spec must be a NamespaceSpec, not %R", nspec);
    return -1;
  }

  PyObject *spec_parent = KosNamespace_Check(parent) ? parent : NULL;
  new_ns = KosCollectorNamespace_New(spec_parent, nspec);
  if (! new_ns) {
    return -1;
  }

  Py_INCREF(parent);
  col_node = kos_dyn_push_collect(ctx, parent);
  if (! col_node) {
    Py_DECREF(parent);
    Py_DECREF(new_ns);
    return -1;
  }

  Py_DECREF(CURRENT_NS(ctx));
  CURRENT_NS(ctx) = new_ns;

  if (KosStackBlock_PushBlock(&CURRENT_STACK(ctx)) < 0) {
    Py_DECREF(new_ns);
    CURRENT_NS(ctx) = parent;
    Py_DECREF(parent);
    kos_dyn_pop_collect(ctx, col_node);
    return -1;
  }

  (void) out_ev;
  return 0;
}


// The end.
