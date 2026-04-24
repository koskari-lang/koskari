/* Included by dynchain.c — dyn_finally */
/* === KosDynFinally (finally region) === */


/**
   Allocate FINALLY with handler_pc; handles RAISE and RETURN when enabled.
*/
KosDynFinally *kos_dynfinally_new(KosDynNode *parent, uint32_t handler_pc) {
  KosDynFinally *node = kos_dynnode_alloc();

  if (! node)
    return NULL;

  KosStackBlock *stack = KosStackBlock_NewEmpty();
  if (! stack) {
    kos_dynnode_return_to_freelist((KosDynNode *) node);
    return NULL;
  }

  kos_dynnode_init(node, KOS_DYN_FINALLY, parent, KOS_DYN_CAP_HANDLES_EXCEPTIONS);
  KosDynFinally_HANDLER_PC(node) = handler_pc;
  KosDynFinally_ENABLED(node) = 1;
  KosDynFinally_HANDLING_RAISE(node) = 0;
  KosDynFinally_HANDLING_RETURN(node) = 0;
  KosDynFinally_HANDLING_RESULT(node) = NULL;
  KosDynNode_STACK((KosDynNode *) node) = stack;
  return node;
}


/**
   Clone finally state for continuation templates.
*/
KosDynFinally *kos_dynfinally_clone(KosDynFinally *node) {
  KosDynFinally *clone = kos_dynnode_alloc();
  if (! clone)
    return NULL;
  kos_dynnode_init(clone, KOS_DYN_FINALLY, NULL, KosDynNode_CAPS(node));
  KosDynNode_FLAGS(clone) = KosDynNode_FLAGS(node) | KOS_DYNF_CLONED;
  KosDynNode_NODE_ID(clone) = KosDynNode_NODE_ID(node);
  KosDynFinally_HANDLER_PC(clone) = KosDynFinally_HANDLER_PC(node);
  KosDynFinally_ENABLED(clone) = KosDynFinally_ENABLED(node);
  KosDynFinally_HANDLING_RAISE(clone) = KosDynFinally_HANDLING_RAISE(node);
  KosDynFinally_HANDLING_RETURN(clone) = KosDynFinally_HANDLING_RETURN(node);
  KosDynFinally_HANDLING_RESULT(clone) = KosDynFinally_HANDLING_RESULT(node);
  Py_XINCREF(KosDynFinally_HANDLING_RESULT(clone));
  if (KosDynNode_STACK((KosDynNode *) node)) {
    KosDynNode_STACK((KosDynNode *) clone) =
        KosStackBlock_DeepCopy(KosDynNode_STACK((KosDynNode *) node));
    if (! KosDynNode_STACK((KosDynNode *) clone)) {
      kos_dynfinally_free(clone);
      return NULL;
    }
  }
  return clone;
}


/**
   DECREF handling result; return node to freelist.
*/
void kos_dynfinally_free(KosDynFinally *node) {
  Py_XDECREF(KosDynFinally_HANDLING_RESULT(node));
  kos_dynnode_return_to_freelist((KosDynNode *) node);
}


/**
   Push FINALLY as DYN_TOP. Callers: opcode PUSH_FINALLY.
*/
KosDynFinally *kos_dyn_push_finally(KosEvalState *es, uint32_t handler_pc) {
  KosContext *ctx = KosEvalState_CONTEXT(es);
  KosDynFinally *node = kos_dynfinally_new(KosContext_DYN_TOP(ctx), handler_pc);
  if (! node) return NULL;
  KosContext_DYN_TOP(ctx) = (KosDynNode *) node;
  KosContext_CHAIN_VERSION(ctx)++;
  KosEvalState_SyncFromContext(es);
  return node;
}


/**
   Pop FINALLY when top. Callers: abort_push_region, POP_FINALLY fast path.
*/
void kos_dyn_pop_finally(KosEvalState *es, KosDynFinally *node) {
  KosContext *ctx = KosEvalState_CONTEXT(es);
  if (KosContext_DYN_TOP(ctx)==(KosDynNode*)node) {
    KosContext_DYN_TOP(ctx)=KosDynNode_PARENT(node);
    KosContext_CHAIN_VERSION(ctx)++;
    kos_dynfinally_free(node);
    KosEvalState_RefreshDynCaches(es);
  }
  KosEvalState_SyncFromContext(es);
}


/* === GC traversal === */


/**
   GC traverse a KOS_DYN_FINALLY: visit handling result when present.
*/
static int dynnode_traverse_finally(KosDynNode *node, visitproc visit, void *arg) {
  int tr = kos_stackblock_traverse(KosDynNode_STACK(node), visit, arg);
  if (tr != 0)
    return tr;
  Py_VISIT(KosDynFinally_HANDLING_RESULT((KosDynFinally *) node));
  return 0;
}
