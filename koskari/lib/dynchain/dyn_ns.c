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
  Included by dynchain.c. The KOS_DYN_NS node: the let namespace scope.

  :author: Christopher O'Brien <obriencj@gmail.com>
  :license: GNU General Public License v3
*/

/* === KosDynNs (namespace scope) implementation === */


/**
   NS node holding active scope namespace.
*/
static KosDynNs *kos_dynns_new(
    KosDynNode *parent,
    PyObject *ns) {

  KosDynNs *ns_node = kos_dynnode_alloc();
  if (! ns_node) {
    Py_DECREF(ns);
    return NULL;
  }

  kos_dynnode_init(ns_node, KOS_DYN_NS, parent, KOS_DYN_CAP_OWNS_NS);
  KosDynNode_NS(ns_node) = ns;  /* caller transferred ownership */
  return ns_node;
}


/**
   Clone for a continuation segment: the clone shares the node's
   namespace. Two holders of a never-revealed namespace would each
   report its contents to the collector, so the share reveals it (the
   frame clone does the same), and the collector tracks it itself.
*/
static KosDynNs *kos_dynns_clone(KosDynNs *ns_node) {
  if (! ns_node)
    return NULL;

  PyObject *ns = KosNamespace_Reveal(KosDynNode_NS(ns_node));
  KosDynNs *clone = kos_dynnode_clone_base(ns_node);
  if (! clone) {
    Py_DECREF(ns);
    return NULL;
  }
  KosDynNode_NS(clone) = ns;  /* owned */
  return clone;
}


/**
   Return node to freelist (which releases the owned namespace).
*/
static void kos_dynns_free(KosDynNs *ns_node) {
  if (! ns_node)
    return;

  kos_dynnode_return_to_freelist(ns_node);
}


static int dynnode_to_dict_ns(KosDynNode *node, PyObject *data) {

  (void) node;
  (void) data;
  return 0;
}


/**
   Push NS as DYN_TOP. Callers: PUSH_NS.
*/
KosDynNs *kos_dyn_push_ns(
    KosEvalState *es,
    PyObject *ns) {

  // CONTEXT: dirty, touches context directly
  KosDynNs *ns_node = kos_dynns_new(KosEvalState_DYN_TOP(es), ns);
  if (! ns_node)
    return NULL;

  if (kos_dyn_push(es, (KosDynNode *) ns_node) < 0) {
    kos_dynns_free(ns_node);
    return NULL;
  }
  return ns_node;
}


/**
   Unlink and free NS when top. Callers: POP_NS.
*/
int kos_dyn_pop_ns(KosEvalState *es, KosDynNs *node) {
  if (! KosEvalState_DYN_TOP(es) || KosEvalState_DYN_TOP(es) != (KosDynNode *) node) {
    PyErr_SetString(PyExc_RuntimeError, "NS node is not at top of chain");
    return -1;
  }

  KOS_DYN_POP_REQUIRE_KIND(node, KOS_DYN_NS, "POP_NS", "NS");
  return kos_dyn_pop_transparent_deliver(es, (KosDynNode *) node);
}


/* === GC traversal === */


/**
   GC traverse a KOS_DYN_NS: no payload beyond the base fields (the
   namespace is visited generically by kos_dynnode_traverse).
*/
static int dynnode_traverse_ns(KosDynNode *node, visitproc visit, void *arg) {
  (void) node;
  (void) visit;
  (void) arg;
  return 0;
}


// The end.
