/* Included by dynchain.c — dyn_ns */
/* === KosDynNs (namespace scope) implementation === */


/**
   NS node holding prior frame namespace for restore on pop. Callers:
   kos_dyn_push_ns.
*/
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


/**
   Clone saved_ns INCREF for continuation segments.
*/
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


/**
   DECREF saved_ns; return node to freelist.
*/
static void kos_dynns_free(KosDynNs *ns_node) {
  if (! ns_node)
    return;

  Py_DECREF(KosDynNs_SAVED_NS(ns_node));
  kos_dynnode_return_to_freelist(ns_node);
}


/**
   Restore frame ns from saved_ns during uninstall (PUSH_NS / POP_NS).
*/
void kos_dyn_ns_unwind(KosEvalState *es, KosDynNs *node) {
  if (! node)
    return;
  if (KosEvalState_FRAME(es)) {
    PyObject *prior = KosDynNs_SAVED_NS(node);
    Py_INCREF(prior);
    Py_DECREF(KosDynFrame_NS(KosEvalState_FRAME(es)));
    KosDynFrame_NS(KosEvalState_FRAME(es)) = prior;
  }
}


/**
   Push NS as DYN_TOP (caller swapped frame ns before push). Callers: PUSH_NS.
*/
KosDynNs *kos_dyn_push_ns(KosEvalState *es, PyObject *saved_ns) {

  KosContext *ctx = KosEvalState_CONTEXT(es);
  KosDynNs *ns_node = kos_dynns_new(KosContext_DYN_TOP(ctx), saved_ns);
  if (! ns_node)
    return NULL;

  if (kos_dyn_push_noinstall(es, (KosDynNode *) ns_node) < 0) {
    kos_dynns_free(ns_node);
    return NULL;
  }
  return ns_node;
}


/**
   Unlink and free NS when top. Callers: POP_NS.
*/
void kos_dyn_pop_ns(KosEvalState *es, KosDynNs *node) {
  KosContext *ctx = KosEvalState_CONTEXT(es);
  if (! KosContext_DYN_TOP(ctx) || KosContext_DYN_TOP(ctx) != (KosDynNode *) node) {
    KosEvalState_SyncFromContext(es);
    return;
  }
  {
    KosDynNode *parent = KosDynNode_PARENT((KosDynNode *) node);

    kos_dyn_commit_dyn_top(es, parent);
    KosDynNode_PARENT((KosDynNode *) node) = NULL;
    kos_dynns_free(node);
  }
}


/* === GC traversal === */


/**
   GC traverse a KOS_DYN_NS: visit the saved namespace reference.
*/
static int dynnode_traverse_ns(KosDynNode *node, visitproc visit, void *arg) {
  KosDynNs *ns_node = (KosDynNs *) node;
  Py_VISIT(KosDynNs_SAVED_NS(ns_node));
  return 0;
}
