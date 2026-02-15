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
   POP_NS: arg unused. Verify dyn_top is KOS_DYN_NS, restore frame ns from
   saved_ns, pop the NS node.
*/

KOS_OP_HANDLER(POP_NS) {
  if (! KosContext_DYN_TOP(ctx) || ! KosDynNode_IS_NS(KosContext_DYN_TOP(ctx))) {
    PyErr_SetString(PyExc_RuntimeError, "POP_NS: top is not a namespace scope");
    return -1;
  }

  KosDynNs *ns_node = (KosDynNs *) KosContext_DYN_TOP(ctx);
  PyObject *prior = KosDynNs_SAVED_NS(ns_node);
  Py_INCREF(prior);
  Py_DECREF(CURRENT_NS(ctx));
  CURRENT_NS(ctx) = prior;
  kos_dyn_pop_ns(ctx, ns_node);

  (void) out_ev;
  (void) insn;
  return 0;
}
