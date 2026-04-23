/* Included by dynchain.c — dyn_collect */
/* === KosDynCollect implementation === */


static void kos_dyncollect_free(KosDynCollect *col_node);


/**
   COLLECT region: saved ns for restore when collector scope ends.
*/
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


/**
   Clone saved_ns for continuation capture.
*/
static KosDynCollect *kos_dyncollect_clone(KosDynCollect *col_node) {
  if (! col_node)
    return NULL;

  PyObject *saved = KosDynCollect_SAVED_NS(col_node);
  Py_INCREF(saved);
  KosDynCollect *clone = kos_dyncollect_new(NULL, saved);
  if (! clone)
    return NULL;
  KosDynNode_NODE_ID(clone) = KosDynNode_NODE_ID((KosDynNode *) col_node);
  if (KosDynNode_STACK((KosDynNode *) col_node)) {
    KosDynNode_STACK((KosDynNode *) clone) =
        KosStackBlock_DeepCopy(KosDynNode_STACK((KosDynNode *) col_node));
    if (! KosDynNode_STACK((KosDynNode *) clone)) {
      kos_dyncollect_free(clone);
      return NULL;
    }
  }
  return clone;
}


/**
   DECREF saved_ns; return node to freelist.
*/
static void kos_dyncollect_free(KosDynCollect *col_node) {
  if (! col_node)
    return;

  kos_dynnode_release_stack((KosDynNode *) col_node);
  Py_DECREF(KosDynCollect_SAVED_NS(col_node));
  kos_dynnode_return_to_freelist((KosDynNode *) col_node);
}


/**
   Restore frame ns from collector saved_ns on pop (matches kos_dyn_ns_unwind).
*/
void kos_dyn_collect_unwind(KosEvalState *es, KosDynCollect *node) {
  KosContext *ctx = KosEvalState_CONTEXT(es);
  if (! node)
    return;
  if (KosContext_FRAME_TOP(ctx)) {
    PyObject *prior = KosDynCollect_SAVED_NS(node);
    Py_INCREF(prior);
    Py_DECREF(KosDynFrame_NS(KosContext_FRAME_TOP(ctx)));
    KosDynFrame_NS(KosContext_FRAME_TOP(ctx)) = prior;
  }
  KosEvalState_SyncFromContext(es);
}


/**
   Push COLLECT as DYN_TOP. Callers: PUSH_COLLECT.
*/
KosDynCollect *kos_dyn_push_collect(KosEvalState *es, PyObject *saved_ns) {
  KosContext *ctx = KosEvalState_CONTEXT(es);
  KosDynCollect *col_node = kos_dyncollect_new(KosContext_DYN_TOP(ctx), saved_ns);
  if (! col_node)
    return NULL;

  KosContext_DYN_TOP(ctx) = (KosDynNode *) col_node;
  KosContext_CHAIN_VERSION(ctx)++;

  KosEvalState_SyncFromContext(es);
  return col_node;
}


/**
   Pop COLLECT when top. Callers: POP_COLLECT.
*/
void kos_dyn_pop_collect(KosEvalState *es, KosDynCollect *node) {
  KosContext *ctx = KosEvalState_CONTEXT(es);
  if (! KosContext_DYN_TOP(ctx) ||
      KosContext_DYN_TOP(ctx) != (KosDynNode *) node) {
    KosEvalState_SyncFromContext(es);
    return;
  }

  KosContext_DYN_TOP(ctx) = KosDynNode_PARENT(node);
  KosContext_CHAIN_VERSION(ctx)++;
  kos_dyncollect_free(node);
  kos_ctx_refresh_dyn_caches(ctx);
  KosEvalState_SyncFromContext(es);
}


/* === GC traversal === */


/**
   GC traverse a KOS_DYN_COLLECT: visit saved namespace for the collector.
*/
static int dynnode_traverse_collect(KosDynNode *node, visitproc visit, void *arg) {
  int tr = kos_stackblock_traverse(KosDynNode_STACK(node), visit, arg);
  if (tr != 0)
    return tr;
  Py_VISIT(KosDynCollect_SAVED_NS((KosDynCollect *) node));
  return 0;
}
