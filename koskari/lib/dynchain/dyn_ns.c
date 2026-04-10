/* Included by dynchain.c — dyn_ns */
/* === KosDynNs (namespace scope) implementation === */


static KosDynNs *kos_dynns_new(KosDynNode *parent, PyObject *saved_ns) {

  KosDynNs *ns_node = kos_dynnode_alloc();
  if (! ns_node) {
    Py_DECREF(saved_ns);
    return NULL;
  }

  kos_dynnode_init(ns_node, KOS_DYN_NS, parent, 0);
  KosDynNs_SAVED_NS(ns_node) = saved_ns;  /* caller transferred ownership */
  return ns_node;
}


static KosDynNs *kos_dynns_clone(KosDynNs *ns_node) {
  if (! ns_node)
    return NULL;

  PyObject *saved = KosDynNs_SAVED_NS(ns_node);
  Py_INCREF(saved);
  KosDynNs *clone = kos_dynns_new(NULL, saved);
  if (clone)
    KosDynNode_NODE_ID(clone) = KosDynNode_NODE_ID(ns_node);
  return clone;
}


static void kos_dynns_free(KosDynNs *ns_node) {
  if (! ns_node)
    return;

  Py_DECREF(KosDynNs_SAVED_NS(ns_node));
  kos_dynnode_return_to_freelist(ns_node);
}


void kos_dyn_ns_unwind(KosContext *ctx, KosDynNs *node) {
  if (! node)
    return;
  if (KosContext_FRAME_TOP(ctx)) {
    PyObject *prior = KosDynNs_SAVED_NS(node);
    Py_INCREF(prior);
    Py_DECREF(KosDynFrame_NS(KosContext_FRAME_TOP(ctx)));
    KosDynFrame_NS(KosContext_FRAME_TOP(ctx)) = prior;
  }
}


KosDynNs *kos_dyn_push_ns(KosContext *ctx, PyObject *saved_ns) {

  KosDynNs *ns_node = kos_dynns_new(KosContext_DYN_TOP(ctx), saved_ns);
  if (! ns_node)
    return NULL;

  KosContext_DYN_TOP(ctx) = (KosDynNode *) ns_node;
  KosContext_CHAIN_VERSION(ctx)++;

  return ns_node;
}


void kos_dyn_pop_ns(KosContext *ctx, KosDynNs *node) {
  if (! KosContext_DYN_TOP(ctx) || KosContext_DYN_TOP(ctx) != (KosDynNode *) node) {
    return;
  }

  KosContext_DYN_TOP(ctx) = KosDynNode_PARENT(node);
  KosContext_CHAIN_VERSION(ctx)++;
  kos_dynns_free(node);
}
