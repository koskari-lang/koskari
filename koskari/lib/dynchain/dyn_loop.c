/* Included by dynchain.c — dyn_loop */
/* === KosDynLoop === */


/**
   LOOP region: break/continue PCs and optional continue result.
*/
KosDynLoop *kos_dynloop_new(KosDynNode *parent, uint32_t break_pc, uint32_t continue_pc) {
  KosDynLoop *node = kos_dynnode_alloc();
  if (! node)
    return NULL;
  kos_dynnode_init(node, KOS_DYN_LOOP, parent, 0);
  KosDynLoop_BREAK_PC(node) = break_pc;
  KosDynLoop_CONTINUE_PC(node) = continue_pc;
  KosDynLoop_ENABLED(node) = 1;
  KosDynLoop_RESULT(node) = NULL;
  return node;
}


/**
   Clone PCs, enabled, and RESULT ref for capture.
*/
KosDynLoop *kos_dynloop_clone(KosDynLoop *node) {
  KosDynLoop *clone = kos_dynnode_alloc();
  if (! clone)
    return NULL;
  kos_dynnode_init(clone, KOS_DYN_LOOP, NULL, KosDynNode_CAPS(node));
  KosDynNode_FLAGS(clone) = KosDynNode_FLAGS(node) | KOS_DYNF_CLONED;
  KosDynNode_NODE_ID(clone) = KosDynNode_NODE_ID(node);
  KosDynLoop_BREAK_PC(clone) = KosDynLoop_BREAK_PC(node);
  KosDynLoop_CONTINUE_PC(clone) = KosDynLoop_CONTINUE_PC(node);
  KosDynLoop_ENABLED(clone) = KosDynLoop_ENABLED(node);
  KosDynLoop_RESULT(clone) = KosDynLoop_RESULT(node);
  Py_XINCREF(KosDynLoop_RESULT(clone));
  return clone;
}


/**
   DECREF RESULT; return node to freelist.
*/
void kos_dynloop_free(KosDynLoop *node) {
  Py_XDECREF(KosDynLoop_RESULT(node));
  kos_dynnode_return_to_freelist(node);
}


/**
   Push LOOP as DYN_TOP. Callers: opcode PUSH_LOOP.
*/
KosDynLoop *kos_dyn_push_loop(KosContext *ctx, uint32_t break_pc, uint32_t continue_pc) {
  KosDynLoop *node = kos_dynloop_new(KosContext_DYN_TOP(ctx), break_pc, continue_pc);
  if (! node) return NULL;
  KosContext_DYN_TOP(ctx) = (KosDynNode *) node;
  KosContext_CHAIN_VERSION(ctx)++;
  return node;
}


/**
   Pop LOOP when top. Callers: abort_push_region, kos_dyn_transfer_to_loop, POP.
*/
void kos_dyn_pop_loop(KosContext *ctx, KosDynLoop *node) {
  if (KosContext_DYN_TOP(ctx)==(KosDynNode*)node) {
    KosContext_DYN_TOP(ctx)=KosDynNode_PARENT(node);
    KosContext_CHAIN_VERSION(ctx)++;
    kos_dynloop_free(node);
  }
}


/* === GC traversal === */


/**
   GC traverse a KOS_DYN_LOOP: visit optional continue result.
*/
static int dynnode_traverse_loop(KosDynNode *node, visitproc visit, void *arg) {
  Py_VISIT(KosDynLoop_RESULT((KosDynLoop *) node));
  return 0;
}
