/* Included by dynchain.c — dyn_cc */
/* === KosDynCC implementation === */


static KosDynCC *kos_dyncc_new(KosDynNode *parent, uint32_t resume_pc) {

  KosDynCC *cc = kos_dynnode_alloc();
  if (! cc)
    return NULL;

  kos_dynnode_init(cc, KOS_DYN_CC, parent, 0);
  KosDynCC_RESUME_PC(cc) = resume_pc;
  return cc;
}


static KosDynCC *kos_dyncc_clone(KosDynCC *cc) {
  if (! cc)
    return NULL;

  KosDynCC *clone = kos_dynnode_alloc();
  if (! clone)
    return NULL;

  kos_dynnode_init(clone, KOS_DYN_CC, NULL, KosDynNode_CAPS(cc));
  KosDynNode_FLAGS(clone) = KosDynNode_FLAGS(cc) | KOS_DYNF_CLONED;
  KosDynNode_NODE_ID(clone) = KosDynNode_NODE_ID(cc);
  KosDynCC_RESUME_PC(clone) = KosDynCC_RESUME_PC(cc);
  return clone;
}


static void kos_dyncc_free(KosDynCC *cc) {
  if (! cc)
    return;

  kos_dynnode_return_to_freelist(cc);
}


KosDynCC *kos_dyn_push_cc(KosContext *ctx, uint32_t resume_pc) {

  KosDynCC *cc = kos_dyncc_new(KosContext_DYN_TOP(ctx), resume_pc);
  if (! cc)
    return NULL;

  KosContext_DYN_TOP(ctx) = (KosDynNode *) cc;
  KosContext_CHAIN_VERSION(ctx)++;

  return cc;
}


void kos_dyn_pop_cc(KosContext *ctx, KosDynCC *cc) {
  if (! KosContext_DYN_TOP(ctx) || KosContext_DYN_TOP(ctx) != (KosDynNode *) cc) {
    return;
  }

  KosContext_DYN_TOP(ctx) = KosDynNode_PARENT(cc);
  KosContext_CHAIN_VERSION(ctx)++;
  kos_dyncc_free(cc);
}

