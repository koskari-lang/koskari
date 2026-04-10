/* Included by dynchain.c — dyn_try */
KosDynTry *kos_dyntry_new(KosDynNode *parent, uint32_t handler_pc) {
  KosDynTry *node = kos_dynnode_alloc();
  if (! node)
    return NULL;
  kos_dynnode_init(node, KOS_DYN_TRY, parent, KOS_DYN_CAP_HANDLES_EXCEPTIONS);
  KosDynTry_HANDLER_PC(node) = handler_pc;
  KosDynTry_ENABLED(node) = 1;
  return node;
}

KosDynTry *kos_dyntry_clone(KosDynTry *node) {
  KosDynTry *clone = kos_dynnode_alloc();
  if (! clone)
    return NULL;
  kos_dynnode_init(clone, KOS_DYN_TRY, NULL, KosDynNode_CAPS(node));
  KosDynNode_FLAGS(clone) = KosDynNode_FLAGS(node) | KOS_DYNF_CLONED;
  KosDynNode_NODE_ID(clone) = KosDynNode_NODE_ID(node);
  KosDynTry_HANDLER_PC(clone) = KosDynTry_HANDLER_PC(node);
  KosDynTry_ENABLED(clone) = KosDynTry_ENABLED(node);
  return clone;
}

void kos_dyntry_free(KosDynTry *node) {
  kos_dynnode_return_to_freelist(node);
}

KosDynTry *kos_dyn_push_try(KosContext *ctx, uint32_t handler_pc) {
  KosDynTry *node = kos_dyntry_new(KosContext_DYN_TOP(ctx), handler_pc);
  if (! node) return NULL;
  KosContext_DYN_TOP(ctx) = (KosDynNode *) node;
  KosContext_CHAIN_VERSION(ctx)++;
  return node;
}

void kos_dyn_pop_try(KosContext *ctx, KosDynTry *node) {
  if (KosContext_DYN_TOP(ctx)==(KosDynNode*)node) {
    KosContext_DYN_TOP(ctx)=KosDynNode_PARENT(node);
    KosContext_CHAIN_VERSION(ctx)++;
    kos_dyntry_free(node);
  }
}
