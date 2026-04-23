/* Included by dynchain.c — dyn_cc */
/* === KosDynCC implementation === */


/**
   Allocate call/cc marker with resume PC after CAPTURE_CC. Callers:
   opcode PUSH_CC.
*/
static KosDynCC *kos_dyncc_new(KosDynNode *parent, uint32_t resume_pc) {

  KosDynCC *cc = kos_dynnode_alloc();
  if (! cc)
    return NULL;

  kos_dynnode_init(cc, KOS_DYN_CC, parent, 0);
  KosDynCC_RESUME_PC(cc) = resume_pc;
  return cc;
}


/**
   Clone resume_pc for continuation templates.
*/
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


/**
   No refs on CC payload; return to freelist.
*/
static void kos_dyncc_free(KosDynCC *cc) {
  if (! cc)
    return;

  kos_dynnode_return_to_freelist(cc);
}


/**
   Link CC as DYN_TOP without kos_dyn_install (minimal push). Callers: PUSH_CC.
*/
KosDynCC *kos_dyn_push_cc(KosEvalState *es, uint32_t resume_pc) {

  KosContext *ctx = KosEvalState_CONTEXT(es);
  KosDynCC *cc = kos_dyncc_new(KosContext_DYN_TOP(ctx), resume_pc);
  if (! cc)
    return NULL;

  KosContext_DYN_TOP(ctx) = (KosDynNode *) cc;
  KosContext_CHAIN_VERSION(ctx)++;

  KosEvalState_SyncFromContext(es);
  return cc;
}


/**
   Unlink CC from top when it matches; free node. Callers: POP_CC fast path.
*/
void kos_dyn_pop_cc(KosEvalState *es, KosDynCC *cc) {
  KosContext *ctx = KosEvalState_CONTEXT(es);
  if (! KosContext_DYN_TOP(ctx) || KosContext_DYN_TOP(ctx) != (KosDynNode *) cc) {
    return;
  }

  KosContext_DYN_TOP(ctx) = KosDynNode_PARENT(cc);
  KosContext_CHAIN_VERSION(ctx)++;
  kos_dyncc_free(cc);
  KosEvalState_SyncFromContext(es);
}


/* === GC traversal === */


/**
   GC traverse a KOS_DYN_CC: no PyObject refs on the node.
*/
static int dynnode_traverse_cc(KosDynNode *node, visitproc visit, void *arg) {
  (void) node;
  (void) visit;
  (void) arg;
  return 0;
}

