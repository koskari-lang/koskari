/* Included by dynchain.c — dyn_delim */
/* === KosDynDelim implementation === */


/**
   Allocate DELIM marker for delimited return bookkeeping.
*/
KosDynDelim *kos_dyndelim_new(
    KosDynNode *parent,
    uint8_t mode,
    KosDynNode *chain,
    uint32_t pc,
    KosDynFrame *frame,
    KosDynNode *node) {

  KosDynDelim *delim = kos_dynnode_alloc();
  if (! delim)
    return NULL;

  kos_dynnode_init(delim, KOS_DYN_DELIM, parent, 0);
  KosDynDelim_MODE(delim) = mode;
  KosDynDelim_RETURN_CHAIN(delim) = chain;
  KosDynDelim_RETURN_PC(delim) = pc;
  KosDynDelim_RETURN_FRAME_NODE(delim) = (KosDynNode *) frame;
  KosDynDelim_RETURN_NODE(delim) = node;
  return delim;
}


/**
   Clone DELIM payload.
*/
static KosDynDelim *kos_dyndelim_clone(KosDynDelim *delim) {
  if (! delim)
    return NULL;

  KosDynDelim *clone = kos_dynnode_alloc();
  if (! clone)
    return NULL;

  kos_dynnode_init(clone, KOS_DYN_DELIM, NULL, KosDynNode_CAPS(delim));
  KosDynNode_FLAGS(clone) = KosDynNode_FLAGS(delim) | KOS_DYNF_CLONED;
  KosDynNode_NODE_ID(clone) = KosDynNode_NODE_ID(delim);
  KosDynDelim_MODE(clone) = KosDynDelim_MODE(delim);
  KosDynDelim_RETURN_CHAIN(clone) = KosDynDelim_RETURN_CHAIN(delim);
  KosDynDelim_RETURN_PC(clone) = KosDynDelim_RETURN_PC(delim);
  KosDynDelim_RETURN_FRAME_NODE(clone) = KosDynDelim_RETURN_FRAME_NODE(delim);
  KosDynDelim_RETURN_NODE(clone) = KosDynDelim_RETURN_NODE(delim);
  return clone;
}


/**
   Free DELIM payload and node.
*/
void kos_dyndelim_free(KosDynDelim *delim) {
  if (! delim)
    return;

  if (KosDynDelim_RETURN_CHAIN(delim)) {
    kos_dynchain_free(KosDynDelim_RETURN_CHAIN(delim), NULL);
    KosDynDelim_RETURN_CHAIN(delim) = NULL;
  }

  kos_dynnode_return_to_freelist(delim);
}


/**
   Push DELIM marker via kos_dyn_push.
*/
KosDynDelim *kos_dyn_push_delim(
    KosEvalState *es,
    uint8_t mode,
    KosDynNode *chain,
    uint32_t pc,
    KosDynFrame *frame,
    KosDynNode *node) {

  KosContext *ctx = KosEvalState_CONTEXT(es);
  KosDynDelim *delim = kos_dyndelim_new(KosContext_DYN_TOP(ctx),
                                        mode,
                                        chain,
                                        pc,
                                        frame,
                                        node);
  if (! delim)
    return NULL;

  if (kos_dyn_push(es, (KosDynNode *) delim) < 0) {
    kos_dyndelim_free(delim);
    return NULL;
  }

  KosEvalState_SyncFromContext(es);
  return delim;
}


/**
   Pop DELIM marker from top of chain.
*/
void kos_dyn_pop_delim(KosEvalState *es, KosDynDelim *delim) {
  KosContext *ctx = KosEvalState_CONTEXT(es);
  if (! KosContext_DYN_TOP(ctx) || KosContext_DYN_TOP(ctx) != (KosDynNode *) delim) {
    KosEvalState_SyncFromContext(es);
    return;
  }

  kos_dyn_uninstall_handler(es, (KosDynNode *) delim, KOS_XFER_EVENT_NONE, NULL);

  KosContext_DYN_TOP(ctx) = KosDynNode_PARENT(delim);
  KosContext_CHAIN_VERSION(ctx)++;
  kos_dyndelim_free(delim);
  KosEvalState_RefreshDynCaches(es);
  KosEvalState_SyncFromContext(es);
}


/**
   Find nearest DELIM from dyn_top toward root; optional mode filter.
*/
KosDynDelim *kos_find_nearest_delim(KosEvalState *es, uint8_t mode) {
  KosContext *ctx = KosEvalState_CONTEXT(es);
  KosDynNode *node;
  for (node = KosContext_DYN_TOP(ctx); node; node = KosDynNode_PARENT(node)) {
    if (! KosDynNode_IS_DELIM(node))
      continue;
    if (! mode || KosDynDelim_MODE((KosDynDelim *) node) == mode)
      return (KosDynDelim *) node;
  }
  return NULL;
}


/* === GC traversal === */


/**
   GC traverse a KOS_DYN_DELIM: visit owned return chain payload if present.
*/
static int dynnode_traverse_delim(KosDynNode *node, visitproc visit, void *arg) {
  KosDynNode *chain = KosDynDelim_RETURN_CHAIN((KosDynDelim *) node);
  if (! chain)
    return 0;
  return kos_dynchain_traverse(chain, NULL, visit, arg);
}
