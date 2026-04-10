/* Included by dynchain.c — dyn_collect */
/* === KosDynCollect implementation === */


static KosDynCollect *kos_dyncollect_new(KosDynNode *parent, PyObject *saved_ns) {
  KosDynCollect *col_node = (KosDynCollect *) kos_dynnode_alloc();
  if (! col_node) {
    Py_DECREF(saved_ns);
    return NULL;
  }

  kos_dynnode_init((KosDynNode *) col_node, KOS_DYN_COLLECT, parent, 0);
  KosDynCollect_SAVED_NS(col_node) = saved_ns;
  return col_node;
}


static KosDynCollect *kos_dyncollect_clone(KosDynCollect *col_node) {
  if (! col_node)
    return NULL;

  PyObject *saved = KosDynCollect_SAVED_NS(col_node);
  Py_INCREF(saved);
  KosDynCollect *clone = kos_dyncollect_new(NULL, saved);
  if (clone)
    KosDynNode_NODE_ID(clone) = KosDynNode_NODE_ID((KosDynNode *) col_node);
  return clone;
}


static void kos_dyncollect_free(KosDynCollect *col_node) {
  if (! col_node)
    return;

  Py_DECREF(KosDynCollect_SAVED_NS(col_node));
  kos_dynnode_return_to_freelist((KosDynNode *) col_node);
}


void kos_dyn_collect_unwind(KosContext *ctx, KosDynCollect *node) {
  if (! node)
    return;
  if (KosContext_FRAME_TOP(ctx)) {
    PyObject *prior = KosDynCollect_SAVED_NS(node);
    Py_INCREF(prior);
    Py_DECREF(KosDynFrame_NS(KosContext_FRAME_TOP(ctx)));
    KosDynFrame_NS(KosContext_FRAME_TOP(ctx)) = prior;
  }
}


KosDynCollect *kos_dyn_push_collect(KosContext *ctx, PyObject *saved_ns) {
  KosDynCollect *col_node = kos_dyncollect_new(KosContext_DYN_TOP(ctx), saved_ns);
  if (! col_node)
    return NULL;

  KosContext_DYN_TOP(ctx) = (KosDynNode *) col_node;
  KosContext_CHAIN_VERSION(ctx)++;

  return col_node;
}


void kos_dyn_pop_collect(KosContext *ctx, KosDynCollect *node) {
  if (! KosContext_DYN_TOP(ctx) ||
      KosContext_DYN_TOP(ctx) != (KosDynNode *) node) {
    return;
  }

  KosContext_DYN_TOP(ctx) = KosDynNode_PARENT(node);
  KosContext_CHAIN_VERSION(ctx)++;
  kos_dyncollect_free(node);
}
