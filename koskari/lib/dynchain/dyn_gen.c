/* Included by dynchain.c — dyn_gen */
/* === KosDynGen implementation === */


/**
   Allocate GEN boundary node; owner borrowed, refcount for sharing. Callers:
   generator object setup.
*/
KosDynGen *kos_dyngen_new(PyObject *owner) {
  KosDynGen *gen = (KosDynGen *) kos_dynnode_alloc();
  if (! gen)
    return NULL;

  kos_dynnode_init((KosDynNode *) gen, KOS_DYN_GEN, NULL, 0);
  KosDynNode_FLAGS(gen) = KOS_DYNF_BOUNDARY;
  KosDynGen_OWNER(gen) = owner;  /* borrowed */
  KosDynGen_ACTIVE(gen) = NULL;
  KosDynGen_REFCOUNT(gen) = 1;
  return gen;
}


/**
   Clear owner/active; return node to freelist.
*/
void kos_dyngen_free(KosDynGen *gen) {
  if (! gen)
    return;

  KosDynGen_OWNER(gen) = NULL;
  KosDynGen_ACTIVE(gen) = NULL;
  kos_dynnode_return_to_freelist((KosDynNode *) gen);
}


/**
   Increment GEN refcount when chain holds an extra reference.
*/
void kos_dyngen_acquire(KosDynGen *gen) {
  if (gen)
    KosDynGen_REFCOUNT(gen)++;
}


/**
   Decrement refcount; free at zero. Callers: freeze path, unwind exhausted.
*/
void kos_dyngen_release(KosDynGen *gen) {
  if (! gen)
    return;
  if (KosDynGen_REFCOUNT(gen) == 0) {
    PyErr_SetString(PyExc_RuntimeError,
                    "kos_dyngen_release: refcount underflow");
    return;
  }
  KosDynGen_REFCOUNT(gen)--;
  if (KosDynGen_REFCOUNT(gen) == 0)
    kos_dyngen_free(gen);
}


/**
   Resume generator: graft GEN under ctx, swap in frozen active chain, push
   None. Callers: eval when entering generator body from consumer.
*/
int kos_dyn_gen_install(KosContext *ctx, KosDynGen *gen) {
  KosDynNode *active;
  KosDynNode *n;

  SAYF("[gen] install ENTER ctx=%p gen=%p dyn_top=%p ctx_dyn_top_kind=%s "
       "active=%p",
       (void *) ctx, (void *) gen,
       (void *) KosContext_DYN_TOP(ctx),
       KosContext_DYN_TOP(ctx)
           ? kos_dynnode_kind_name(KosDynNode_KIND(KosContext_DYN_TOP(ctx)))
           : "NULL",
       (void *) KosDynGen_ACTIVE(gen));
  if (KosDynGen_ACTIVE(gen)) {
    SAY("[gen] install chain from active: ");
    for (n = (KosDynNode *) KosDynGen_ACTIVE(gen); n; n = KosDynNode_PARENT(n))
      SAYF("  %p %s -> ", (void *) n,
           kos_dynnode_kind_name(KosDynNode_KIND(n)));
    SAY("(end)");
  }
  if (! gen || ! ctx) {
    PyErr_SetString(PyExc_RuntimeError, "NULL ctx or gen for gen_install");
    return -1;
  }
  active = (KosDynNode *) KosDynGen_ACTIVE(gen);
  if (! active) {
    PyErr_SetString(PyExc_RuntimeError,
                    "kos_dyn_gen_install: GEN has no active frame");
    return -1;
  }

  /* 1. GEN's parent = current ctx's active node */
  KosDynNode_PARENT(gen) = KosContext_DYN_TOP(ctx);
  /* 2. Incref GEN */
  kos_dyngen_acquire(gen);
  /* 3. Put GEN's active onto ctx's active; 4. NULL GEN's active.
   * Do NOT overwrite active->parent: the stored chain active -> ... -> gen
   * is already intact (e.g. YIELD_FROM -> FRAME -> GEN). Overwriting
   * would sever links when active is not a direct child of gen. */
  KosContext_DYN_TOP(ctx) = active;
  KosDynGen_ACTIVE(gen) = NULL;
  KosContext_CHAIN_VERSION(ctx)++;
  kos_update_frame_top(ctx);

  if (KosStackBlock_PushRef(&KosDynFrame_STACK(KosContext_FRAME_TOP(ctx)),
                            Py_None) < 0)
    return -1;

  SAYF("[gen] install DONE dyn_top=%p (%s) frame_top=%p chain: ",
       (void *) KosContext_DYN_TOP(ctx),
       KosContext_DYN_TOP(ctx)
           ? kos_dynnode_kind_name(KosDynNode_KIND(KosContext_DYN_TOP(ctx)))
           : "NULL",
       (void *) KosContext_FRAME_TOP(ctx));
  for (n = KosContext_DYN_TOP(ctx); n; n = KosDynNode_PARENT(n))
    SAYF("%p %s -> ", (void *) n, kos_dynnode_kind_name(KosDynNode_KIND(n)));
  SAY("(end)");
  return 0;
}


/**
   Unwind an exhausted gen from the consumer chain. Call when the gen's
   frame does RETURN and parent is GEN (top-level return of generator).
   Assumes ctx->dyn_top is the gen's frame, frame->parent is gen.
   Frees the chain from frame down to gen, restores consumer, releases gen.
*/
int kos_dyn_gen_unwind_exhausted(KosContext *ctx) {
  KosDynNode *frame = KosContext_DYN_TOP(ctx);
  if (! frame || ! KosDynNode_IS_FRAME(frame))
    return -1;
  KosDynGen *gen = (KosDynGen *) KosDynNode_PARENT(frame);
  if (! gen || ! KosDynNode_IS_GEN((KosDynNode *) gen))
    return -1;
  KosDynNode *consumer = KosDynNode_PARENT(gen);
  kos_dynchain_free(frame, (KosDynNode *) gen);
  KosContext_DYN_TOP(ctx) = consumer;
  KosContext_CHAIN_VERSION(ctx)++;
  kos_update_frame_top(ctx);
  kos_dyngen_release(gen);
  return 0;
}


/* === GC traversal === */


/**
   GC traverse a KOS_DYN_GEN: visit owner and, when frozen, the active chain.
*/
static int dynnode_traverse_gen(KosDynNode *node, visitproc visit, void *arg) {
  Py_VISIT(KosDynGen_OWNER((KosDynGen *) node));
  if (KosDynGen_ACTIVE((KosDynGen *) node))
    return dynnode_traverse_frame(
        KosDynGen_ACTIVE((KosDynGen *) node), visit, arg);
  return 0;
}

