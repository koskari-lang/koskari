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
  Included by dynchain.c. The KOS_DYN_COLLECT node: the collector
  namespace scope a class body runs in.

  :author: Christopher O'Brien <obriencj@gmail.com>
  :license: GNU General Public License v3
*/

/* === KosDynCollect implementation === */


static void kos_dyncollect_free(KosDynCollect *col_node);


/**
   COLLECT region: active collector scope namespace.
*/
static KosDynCollect *kos_dyncollect_new(
    KosDynNode *parent,
    PyObject *ns) {
  KosDynCollect *col_node = (KosDynCollect *) kos_dynnode_alloc();
  if (! col_node) {
    Py_DECREF(ns);
    return NULL;
  }

  kos_dynnode_init((KosDynNode *) col_node, KOS_DYN_COLLECT, parent,
                   KOS_DYN_CAP_OWNS_NS);
  KosDynNode_NS((KosDynNode *) col_node) = ns;
  /* segment attaches at push (kos_dyn_ensure_region_segment): a clone
     built through this constructor must arrive stackless so the
     centralized copy in kos_dyn_clone can carry the source's values */
  return col_node;
}


/**
   Clone for a continuation segment: the clone shares the node's
   namespace, revealed so that only the collector, and not each holder,
   reports its contents (see kos_dynns_clone).
*/
static KosDynCollect *kos_dyncollect_clone(KosDynCollect *col_node) {
  if (! col_node)
    return NULL;

  PyObject *ns = KosNamespace_Reveal(KosDynNode_NS((KosDynNode *) col_node));
  KosDynCollect *clone = (KosDynCollect *) kos_dynnode_clone_base(
      (KosDynNode *) col_node);
  if (! clone) {
    Py_DECREF(ns);
    return NULL;
  }
  KosDynNode_NS((KosDynNode *) clone) = ns;  /* owned */
  return clone;
}


/**
   Return node to freelist (which releases the owned namespace).
*/
static void kos_dyncollect_free(KosDynCollect *col_node) {
  if (! col_node)
    return;

  kos_dynnode_return_to_freelist((KosDynNode *) col_node);
}


static int dynnode_to_dict_collect(KosDynNode *node, PyObject *data) {

  (void) node;
  (void) data;
  return 0;
}


/**
   Push COLLECT as DYN_TOP. Callers: PUSH_COLLECT.
*/
KosDynCollect *kos_dyn_push_collect(
    KosContext *ctx,
    PyObject *ns) {
  // CONTEXT: dirty, touches context directly
  KosDynCollect *col_node = kos_dyncollect_new(
      KosContext_DYN_TOP(ctx), ns);
  if (! col_node)
    return NULL;

  if (kos_dyn_push(ctx, (KosDynNode *) col_node) < 0) {
    kos_dyncollect_free(col_node);
    return NULL;
  }
  return col_node;
}


/**
   Pop COLLECT when top, handing out the collected namespace. The node
   owns that namespace and the pop frees the node, so the reveal
   happens here, after the checks and before the free. Callers:
   POP_COLLECT.
*/
int kos_dyn_pop_collect(
    KosContext *ctx,
    KosDynCollect *node,
    PyObject **out_ns) {

  if (! KosContext_DYN_TOP(ctx) ||
      KosContext_DYN_TOP(ctx) != (KosDynNode *) node) {
    PyErr_SetString(PyExc_RuntimeError, "COLLECT node is not at top of chain");
    return -1;
  }

  KOS_DYN_POP_REQUIRE_KIND(node, KOS_DYN_COLLECT, "POP_COLLECT", "COLLECT");
  {
    KosDynNode *parent = KosDynNode_PARENT((KosDynNode *) node);

    *out_ns = KosNamespace_Reveal(KosDynNode_NS(node));
    kos_dyn_commit_dyn_top(ctx, parent);
    KosDynNode_PARENT((KosDynNode *) node) = NULL;
    kos_dyncollect_free(node);
  }
  return 0;
}


/* === GC traversal === */


/**
   GC traverse a KOS_DYN_COLLECT: no payload beyond the base fields (the
   node-owned namespace is visited generically by kos_dynnode_traverse).
*/
static int dynnode_traverse_collect(KosDynNode *node, visitproc visit, void *arg) {
  (void) node;
  (void) visit;
  (void) arg;
  return 0;
}


// The end.
