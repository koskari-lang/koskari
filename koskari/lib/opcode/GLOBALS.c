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
   GLOBALS: arg unused. Push the namespace of the outermost frame in the
   dynamic chain (globals) onto the stack.
*/

KOS_OP_HANDLER(GLOBALS) {
  if (KosContext_CHAIN_VERSION(ctx) != KosContext_GLOBALS_CACHE_VERSION(ctx)) {
    KosDynNode *n = (KosDynNode *) CURRENT_FRAME(ctx);
    KosDynFrame *outer = CURRENT_FRAME(ctx);
    while (n && n->parent) {
      n = n->parent;
      if (KosDynNode_IS_FRAME(n))
        outer = (KosDynFrame *) n;
    }
    KosContext_GLOBALS_FRAME(ctx) = outer;
    KosContext_GLOBALS_CACHE_VERSION(ctx) = KosContext_CHAIN_VERSION(ctx);
  }

  PyObject *ns = KosDynFrame_NS(KosContext_GLOBALS_FRAME(ctx));
  Py_INCREF(ns);
  return kos_op_outcome_from_int_step(push_result(ctx, ns));
}
