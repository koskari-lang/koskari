/* Included by dynchain.c — dyn_shift */
/* === KosDynShift implementation === */


static KosDynShift *kos_dynshift_new(KosDynNode *parent) {
  KosDynShift *shift = (KosDynShift *) kos_dynnode_alloc();
  if (! shift)
    return NULL;

  kos_dynnode_init((KosDynNode *) shift, KOS_DYN_SHIFT, parent, 0);
  return shift;
}


static KosDynShift *kos_dynshift_clone(KosDynShift *shift) {
  if (! shift)
    return NULL;

  KosDynShift *clone = (KosDynShift *) kos_dynnode_alloc();
  if (! clone)
    return NULL;

  kos_dynnode_init((KosDynNode *) clone, KOS_DYN_SHIFT, NULL,
                   KosDynNode_CAPS((KosDynNode *) shift));
  KosDynNode_FLAGS(clone) = KosDynNode_FLAGS(shift) | KOS_DYNF_CLONED;
  KosDynNode_NODE_ID(clone) = KosDynNode_NODE_ID(shift);
  return clone;
}


static void kos_dynshift_free(KosDynShift *shift) {
  if (! shift)
    return;

  kos_dynnode_return_to_freelist(shift);
}


int kos_dyn_push_shift(KosContext *ctx) {
  KosDynShift *shift = kos_dynshift_new(KosContext_DYN_TOP(ctx));
  if (! shift)
    return -1;

  return kos_dyn_push(ctx, (KosDynNode *) shift);
}

