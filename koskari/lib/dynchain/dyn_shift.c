/* Included by dynchain.c — dyn_shift */
/* === KosDynShift implementation === */


/**
   Marker for shift handler extent (no frame). Callers: kos_dyn_push_shift.
*/
static KosDynShift *kos_dynshift_new(KosDynNode *parent) {
  KosDynShift *shift = (KosDynShift *) kos_dynnode_alloc();
  if (! shift)
    return NULL;

  kos_dynnode_init((KosDynNode *) shift, KOS_DYN_SHIFT, parent, 0);
  return shift;
}


/**
   Clone for captured chain segments.
*/
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


/**
   Return shift node to freelist.
*/
static void kos_dynshift_free(KosDynShift *shift) {
  if (! shift)
    return;

  kos_dynnode_return_to_freelist(shift);
}


/**
   Push SHIFT node via kos_dyn_push. Callers: opcode PUSH_SHIFT.
*/
int kos_dyn_push_shift(KosEvalState *es) {
  KosContext *ctx = KosEvalState_CONTEXT(es);
  // CONTEXT: dirty, touches context directly
  KosDynShift *shift = kos_dynshift_new(KosContext_DYN_TOP(ctx));
  if (! shift)
    return -1;

  return kos_dyn_push(es, (KosDynNode *) shift);
}


/* === GC traversal === */


/**
   GC traverse a KOS_DYN_SHIFT: no PyObject refs on the node.
*/
static int dynnode_traverse_shift(KosDynNode *node, visitproc visit, void *arg) {
  (void) node;
  (void) visit;
  (void) arg;
  return 0;
}

