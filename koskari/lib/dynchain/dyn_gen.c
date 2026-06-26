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
   Free frozen chain held in ACTIVE down to (but not including) gen.
   Only for final destruction via kos_dyngen_free.
*/
static void kos_dyngen_dispose_active(KosDynGen *gen) {

  KosDynNode *active;

  if (! gen)
    return;

  active = (KosDynNode *) KosDynGen_ACTIVE(gen);
  if (! active)
    return;

  KosDynGen_ACTIVE(gen) = NULL;
  kos_dynchain_free(active, (KosDynNode *) gen);
}


/**
   Clear owner/active; return node to freelist.
*/
void kos_dyngen_free(KosDynGen *gen) {
  if (! gen)
    return;

  kos_dyngen_dispose_active(gen);
  KosDynGen_OWNER(gen) = NULL;
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
   GEN->KPAG keepalive: while a GEN's frame is live on a chain, the GEN holds
   a counted reference on its owner (the KosPyAsyncGen) so the owner cannot be
   deallocated mid-run.  Acquired when the GEN is installed/made active;
   released when it freezes (yield) or dies (return/raise).  The KEEPALIVE
   flag makes both idempotent so release fires only when an acquire happened
   (the Python-driven first-run path may go active without an install).
*/
static void kos_dyngen_keepalive_acquire(KosDynGen *gen) {
  PyObject *owner = KosDynGen_OWNER(gen);
  /* Never resurrect an owner that is mid-deallocation (refcount 0): the
     teardown path re-installs the frozen gen to run its GeneratorExit
     cleanup, and a keepalive incref there would resurrect the dying object
     and trigger a re-entrant dealloc when the cleanup unwinds. */
  if (owner && Py_REFCNT(owner) > 0
      && ! (KosDynNode_FLAGS(gen) & KOS_DYNF_GEN_KEEPALIVE)) {
    Py_INCREF(owner);
    KosDynNode_FLAGS(gen) |= KOS_DYNF_GEN_KEEPALIVE;
  }
}


static void kos_dyngen_keepalive_release(KosDynGen *gen) {
  if (KosDynNode_FLAGS(gen) & KOS_DYNF_GEN_KEEPALIVE) {
    PyObject *owner = KosDynGen_OWNER(gen);
    KosDynNode_FLAGS(gen) &= ~KOS_DYNF_GEN_KEEPALIVE;
    Py_XDECREF(owner);
  }
}


static int dynnode_to_dict_gen(KosDynNode *node, PyObject *data) {

  if (snapshot_set_obj(data, "owner", KosDynGen_OWNER(node)) < 0)
    return -1;
  if (snapshot_set_uint(data, "active_node_id",
      KosDynGen_ACTIVE(node) ? KosDynNode_NODE_ID(KosDynGen_ACTIVE(node)) : 0) < 0)
    return -1;
  if (snapshot_set_size(data, "refcount", (size_t) KosDynGen_REFCOUNT(node)) < 0)
    return -1;
  return 0;
}


/**
   Resume generator: graft GEN under ctx, swap in frozen active chain, push
   None. Callers: eval when entering generator body from consumer.
*/
int kos_dyn_gen_install(KosEvalState *es, KosDynGen *gen) {
  KosContext *const ctx = KosEvalState_CONTEXT(es);
  KosDynNode *active;
  KosDynNode *n;

  if (! es || ! gen) {
    PyErr_SetString(PyExc_RuntimeError, "NULL es or gen for gen_install");
    return -1;
  }

  SAYF("[gen] install ENTER ctx=%p gen=%p dyn_top=%p ctx_dyn_top_kind=%s "
       "active=%p",
       (void *) ctx, (void *) gen,
       (void *) KosEvalState_DYN_TOP(es),
       KosEvalState_DYN_TOP(es)
           ? kos_dynnode_kind_name(KosDynNode_KIND(KosEvalState_DYN_TOP(es)))
           : "NULL",
       (void *) KosDynGen_ACTIVE(gen));

  if (KosDynGen_ACTIVE(gen)) {
    SAY("[gen] install chain from active: ");
    // for (n = (KosDynNode *) KosDynGen_ACTIVE(gen); n; n = KosDynNode_PARENT(n))
    //   SAYF("  %p %s -> ", (void *) n,
    //        kos_dynnode_kind_name(KosDynNode_KIND(n)));
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
  KosDynNode_PARENT(gen) = KosEvalState_DYN_TOP(es);
  /* 2. Incref GEN */
  kos_dyngen_acquire(gen);
  /* 3. Put GEN's active onto ctx's active; 4. NULL GEN's active.
   * Do NOT overwrite active->parent: the stored chain active -> ... -> gen
   * is already intact (e.g. YIELD_FROM -> FRAME -> GEN). Overwriting
   * would sever links when active is not a direct child of gen. */
  KosDynGen_ACTIVE(gen) = NULL;
  kos_dyn_commit_dyn_top(es, active);
  /* GEN is now live on the chain: keep its owner (KPAG) alive while running. */
  kos_dyngen_keepalive_acquire(gen);

  if (KosEvalState_StackPushRef(es, Py_None) < 0)
    return -1;

  SAYF("[gen] install DONE dyn_top=%p (%s) frame_top=%p chain: ",
       (void *) KosEvalState_DYN_TOP(es),
       KosEvalState_DYN_TOP(es)
           ? kos_dynnode_kind_name(KosDynNode_KIND(KosEvalState_DYN_TOP(es)))
           : "NULL",
       (void *) KosEvalState_FRAME(es));
  for (n = KosEvalState_DYN_TOP(es); n; n = KosDynNode_PARENT(n))
    SAYF("%p %s -> ", (void *) n, kos_dynnode_kind_name(KosDynNode_KIND(n)));
  SAY("(end)");
  return 0;
}


/**
   First-run bootstrap: graft GEN and its ctx chain head onto consumer es.
   Runnable state lives on src only (dyn_top -> ... -> gen); ACTIVE is NULL.
*/
int kos_dyn_gen_install_bootstrap(
    KosEvalState *es,
    KosContext *src,
    KosDynGen *gen) {

  KosDynNode *chain_head;
  KosDynNode *n;

  if (! es || ! src || ! gen) {
    PyErr_SetString(PyExc_RuntimeError,
                    "NULL es, src, or gen for gen_install_bootstrap");
    return -1;
  }

  chain_head = KosContext_DYN_TOP(src);
  if (! chain_head) {
    PyErr_SetString(PyExc_RuntimeError,
                    "kos_dyn_gen_install_bootstrap: empty source chain");
    return -1;
  }

  for (n = chain_head; n; n = KosDynNode_PARENT(n)) {
    if (n == (KosDynNode *) gen)
      break;
  }
  if (! n) {
    PyErr_SetString(PyExc_RuntimeError,
                    "kos_dyn_gen_install_bootstrap: GEN not on source chain");
    return -1;
  }

  KosDynNode_PARENT(gen) = KosEvalState_DYN_TOP(es);
  /* The GEN's chain-membership reference transfers from src to es along with
     the chain itself (src->dyn_top is cleared below); do NOT acquire a fresh
     one, or src's membership ref is orphaned and the GEN node leaks. */
  kos_dyn_commit_dyn_top(es, chain_head);
  /* GEN is now live on the chain: keep its owner (KPAG) alive while running. */
  kos_dyngen_keepalive_acquire(gen);

  KosContext_DYN_TOP(src) = NULL;
  KosContext_FRAME_TOP(src) = NULL;

  if (KosEvalState_StackPushRef(es, Py_None) < 0)
    return -1;

  return 0;
}


int kos_dyn_gen_frozen_needs_install_es(KosEvalState *es, KosDynGen *g) {

  KosDynNode *n;

  if (! es || ! g)
    return 0;
  if (! KosDynGen_ACTIVE(g))
    return 0;

  for (n = KosEvalState_DYN_TOP(es); n; n = KosDynNode_PARENT(n)) {
    if (n == (KosDynNode *) g)
      return 0;
  }
  return 1;
}


/**
   Unwind an exhausted gen from the consumer chain. Call when the gen's
   frame does RETURN and parent is GEN (top-level return of generator).
   Assumes ctx->dyn_top is the gen's frame, frame->parent is gen.
   Frees the chain from frame down to gen, restores consumer, releases gen.
*/
int kos_dyn_gen_unwind_exhausted(KosEvalState *es) {
  KosDynNode *frame = KosEvalState_DYN_TOP(es);
  if (! frame || ! KosDynNode_IS_FRAME(frame)) {
    PyErr_SetString(PyExc_RuntimeError,
                    "kos_dyn_gen_unwind_exhausted: dyn_top is not a frame");
    return -1;
  }
  KosDynGen *gen = (KosDynGen *) KosDynNode_PARENT(frame);
  if (! gen || ! KosDynNode_IS_GEN((KosDynNode *) gen)) {
    PyErr_SetString(PyExc_RuntimeError,
                    "kos_dyn_gen_unwind_exhausted: frame parent is not GEN");
    return -1;
  }
  KosDynNode *consumer = KosDynNode_PARENT(gen);

  KosEvalState_FlushLiveRegsToFrame(es);
  kos_dynchain_free(frame, (KosDynNode *) gen);
  kos_dyn_commit_dyn_top(es, consumer);
  /* GEN is dead: drop the keepalive on its owner (es is now consistent).
     In the common case the consumer still holds the KPAG, so this is just a
     decrement; if it was the last ref the owner deallocates here. */
  kos_dyngen_keepalive_release(gen);
  kos_dyngen_release(gen);
  return 0;
}


/* === GC traversal === */


/**
   GC traverse a KOS_DYN_GEN: visit owner and, when frozen, the active chain.

   active is the chain head stored by kos_dyn_gen_freeze (dyn_top at yield);
   it may be FOREACH, FRAME, or another kind, not only FRAME. Traverse from
   active down to this GEN node (exclusive) via kos_dynchain_traverse.
*/
static int dynnode_traverse_gen(KosDynNode *node, visitproc visit, void *arg) {
  /* OWNER is a borrowed back-pointer except while the keepalive holds a real
     counted reference on it.  Reporting a borrowed pointer to the cyclic
     collector makes it subtract a phantom reference and collect a live,
     externally-referenced generator (held by a GC-invisible FOREACH/NEXT/
     YIELD_FROM dynnode) as cyclic garbage.  Only visit when it is real. */
  if (KosDynNode_FLAGS(node) & KOS_DYNF_GEN_KEEPALIVE)
    Py_VISIT(KosDynGen_OWNER((KosDynGen *) node));
  if (KosDynGen_ACTIVE((KosDynGen *) node))
    return kos_dynchain_traverse(
        KosDynGen_ACTIVE((KosDynGen *) node),
        node,
        visit,
        arg);
  return 0;
}
