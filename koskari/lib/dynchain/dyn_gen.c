/* Included by dynchain.c — dyn_gen */
/* === KosDynGen implementation === */


/**
   Allocate GEN iteration node; owner borrowed, refcount for sharing. Callers:
   generator object setup.
*/
KosDynGen *kos_dyngen_new(PyObject *owner) {
  KosDynGen *gen = (KosDynGen *) kos_dynnode_alloc();
  if (! gen)
    return NULL;

  kos_dynnode_init((KosDynNode *) gen, KOS_DYN_GEN, NULL,
                   KOS_DYN_CAP_INTERCEPTS_RAISE);
  KosDynGen_OWNER(gen) = owner;  /* borrowed */
  KosDynGen_ACTIVE(gen) = NULL;
  KosDynGen_REFCOUNT(gen) = 1;
  *KosDynGen_HOST(gen) = (KosLiveRegs) { 0 };
  *KosDynGen_FROZEN(gen) = (KosLiveRegs) { 0 };
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


void kos_dyn_finish_flattened_gen(KosEvalState *es, KosDynGen *gen) {

  PyObject *typ = NULL, *val = NULL, *tb = NULL;
  KosDynNode *consumer = KosDynNode_PARENT(gen);
  PyObject *owner = KosDynGen_OWNER(gen);
  KosIterSlots *islots;

  if (owner && KosIter_CheckSlots(owner, &islots))
    islots->ki_finish(owner, 1);
  PyErr_Fetch(&typ, &val, &tb);
  kos_dyn_commit_dyn_top(es, consumer);
  KosDynNode_PARENT(gen) = NULL;
  kos_dyngen_keepalive_release(gen);
  kos_dyngen_release(gen);
  PyErr_Restore(typ, val, tb);
}


static int dynnode_to_dict_gen(KosDynNode *node, PyObject *data) {

  if (snapshot_set_obj(data, "owner", KosDynGen_OWNER(node)) < 0)
    return -1;
  if (snapshot_set_uint(data, "active_node_id",
      KosDynGen_ACTIVE(node) ? kos_dynnode_id(KosDynGen_ACTIVE(node)) : 0) < 0)
    return -1;
  if (snapshot_set_item(data, "refcount",
                        PyLong_FromSize_t((size_t) KosDynGen_REFCOUNT(node))) < 0)
    return -1;
  return 0;
}


KosDynGen *kos_dyn_find_gen(KosEvalState *es) {
  KosDynNode *n = (KosDynNode *) KosEvalState_FRAME(es);

  for (; n; n = KosDynNode_PARENT(n)) {
    if (KosDynNode_IS_GEN(n))
      return (KosDynGen *) n;
  }
  return NULL;
}


/**
   Capture the live registers at the current dyn_top into regs.
*/
static void kos_live_regs_capture(KosEvalState *es, KosLiveRegs *regs) {
  regs->top = KosEvalState_DYN_TOP(es);
  regs->frame = (KosDynNode *) KosEvalState_FRAME(es);
  regs->block = KosEvalState_BLOCK(es);
  regs->ns = KosEvalState_NAMESPACE(es);
}


/**
   Make regs the live registers: dyn_top and the retarget a resync from
   that top would produce, with the outgoing frame's pc flushed.
*/
static void kos_live_regs_apply(KosEvalState *es, const KosLiveRegs *regs) {
  KosEvalState_DYN_TOP(es) = regs->top;
  kos_dyn_resync_apply(es, (KosDynFrame *) regs->frame, regs->block,
                       regs->ns, 1);
}


/**
   Resume generator: graft GEN under ctx, swap in frozen active chain, push
   None. Callers: eval when entering generator body from consumer.
*/
int kos_dyn_gen_install(KosEvalState *es, KosDynGen *gen) {
  KosContext *const ctx = KosEvalState_CONTEXT(es);
  KosDynNode *active;
  KosDynNode *n;
  KosDynFrame *invoker_frame;
  uint32_t invoker_demand;

  if (! es || ! gen) {
    PyErr_SetString(PyExc_RuntimeError, "NULL es or gen for gen_install");
    return -1;
  }

  /* The demand bit is drive-state: forcibly assigned to the generator's
     frame at every entry, inherited from the frame below the splice.
     Adapter-driven installs have no frame below (invoker 0), so an async
     drive self-cleans any bit a prior demand drive left behind; the
     demand boundary ORs its forced bit in after this install. */
  invoker_frame = KosEvalState_FRAME(es);
  invoker_demand = invoker_frame
      ? (KosDynFrame_FLAGS(invoker_frame) & KOS_FRAME_DEMAND)
      : 0;

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

  /* The resumed frames sit above the consumer's frame; a resume that
     would not fit raises RecursionError at the request, as Python's
     next() does, with the generator left suspended. One count serves
     the room check and the renumber, and the invoker frame in hand is
     the base depth. */
  {
    unsigned count = kos_dyn_frame_count(active, (KosDynNode *) gen);

    if (kos_eval_check_frame_room(es, invoker_frame, count) < 0)
      return -1;
    kos_dyn_renumber_frames_counted(
        active, (KosDynNode *) gen,
        invoker_frame ? KosDynFrame_DEPTH(invoker_frame) : 0, count);
  }

  /* The consumer's registers, restored at the next freeze. */
  kos_live_regs_capture(es, KosDynGen_HOST(gen));

  /* 1. GEN's parent = current ctx's active node */
  KosDynNode_PARENT(gen) = KosEvalState_DYN_TOP(es);
  /* 2. Incref GEN */
  kos_dyngen_acquire(gen);
  /* 3. Put GEN's active onto ctx's active; 4. NULL GEN's active.
   * Do NOT overwrite active->parent: the stored chain active -> ... -> gen
   * is already intact (e.g. YIELD_FROM -> FRAME -> GEN). Overwriting
   * would sever links when active is not a direct child of gen. */
  KosDynGen_ACTIVE(gen) = NULL;
  /* frozen.top always equals the active head here: kos_dyn_gen_freeze
     writes ACTIVE and the frozen snapshot together and nothing else
     sets ACTIVE non-NULL. The compare is kept so a future freeze path
     that records one without the other lands on the rebuild instead of
     stale registers; the same holds for host.top at the freeze. */
  if (KosDynGen_FROZEN(gen)->top == active && KosDynGen_FROZEN(gen)->frame) {
    /* The registers the freeze recorded for this very head: apply
       them, then restamp the spliced span's unwind caches, which the
       freeze cleared, from the consumer's registers just recorded:
       they are the facts live at the GEN's parent. Same outcome as
       the rebuild without its find walk or the restamp's scan. */
    kos_live_regs_apply(es, KosDynGen_FROZEN(gen));
    kos_dyn_restamp_span(active, (KosDynNode *) gen, KosDynGen_HOST(gen));
  } else {
    kos_dyn_commit_dyn_top(es, active);
  }
  KosDynGen_FROZEN(gen)->top = NULL;
  /* GEN is now live on the chain: keep its owner (KPAG) alive while running. */
  kos_dyngen_keepalive_acquire(gen);

  {
    KosDynFrame *surfaced = KosEvalState_FRAME(es);
    if (surfaced) {
      if (invoker_demand)
        KosDynFrame_FLAGS(surfaced) |= KOS_FRAME_DEMAND;
      else
        KosDynFrame_FLAGS(surfaced) &= ~KOS_FRAME_DEMAND;
    }
  }

  /* The suspended yield expression's placeholder, replaced by the sent
     value at the drive. A frame waiting on a resume (a delegation in
     flight) takes its delivery through the pending slot instead. */
  {
    KosDynFrame *surfaced = KosEvalState_FRAME(es);
    if (! (surfaced && KosDynFrame_RESUME_KIND(surfaced))
        && KosEvalState_StackPushRef(es, Py_None) < 0)
      return -1;
  }

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
  KosDynFrame *invoker_frame;
  uint32_t invoker_demand;

  if (! es || ! src || ! gen) {
    PyErr_SetString(PyExc_RuntimeError,
                    "NULL es, src, or gen for gen_install_bootstrap");
    return -1;
  }

  /* Drive-state demand bit: see kos_dyn_gen_install. */
  invoker_frame = KosEvalState_FRAME(es);
  invoker_demand = invoker_frame
      ? (KosDynFrame_FLAGS(invoker_frame) & KOS_FRAME_DEMAND)
      : 0;

  chain_head = KosContext_DYN_TOP(src);
  if (! chain_head) {
    PyErr_SetString(PyExc_RuntimeError,
                    "kos_dyn_gen_install_bootstrap: empty source chain");
    return -1;
  }

  /* The membership check walks the span once; the frames it passes are
     the count the room check and the renumber need. */
  {
    unsigned count = 0;

    for (n = chain_head; n; n = KosDynNode_PARENT(n)) {
      if (n == (KosDynNode *) gen)
        break;
      if (KosDynNode_IS_FRAME(n))
        count++;
    }
    if (! n) {
      PyErr_SetString(PyExc_RuntimeError,
                      "kos_dyn_gen_install_bootstrap: GEN not on source"
                      " chain");
      return -1;
    }
    if (kos_eval_check_frame_room(es, invoker_frame, count) < 0)
      return -1;
    kos_dyn_renumber_frames_counted(
        chain_head, (KosDynNode *) gen,
        invoker_frame ? KosDynFrame_DEPTH(invoker_frame) : 0, count);
  }

  /* The consumer's registers, restored at the first freeze. */
  kos_live_regs_capture(es, KosDynGen_HOST(gen));

  KosDynNode_PARENT(gen) = KosEvalState_DYN_TOP(es);
  /* The GEN's chain-membership reference transfers from src to es along with
     the chain itself (src->dyn_top is cleared below); do NOT acquire a fresh
     one, or src's membership ref is orphaned and the GEN node leaks. */
  kos_dyn_commit_dyn_top(es, chain_head);
  /* GEN is now live on the chain: keep its owner (KPAG) alive while running. */
  kos_dyngen_keepalive_acquire(gen);

  KosContext_DYN_TOP(src) = NULL;
  KosContext_FRAME_TOP(src) = NULL;

  {
    KosDynFrame *surfaced = KosEvalState_FRAME(es);
    if (surfaced) {
      if (invoker_demand)
        KosDynFrame_FLAGS(surfaced) |= KOS_FRAME_DEMAND;
      else
        KosDynFrame_FLAGS(surfaced) &= ~KOS_FRAME_DEMAND;
    }
  }

  /* No resume placeholder: a bootstrap install is the generator's first
     drive, entering fresh code rather than reviving a suspended yield.
     Only the frozen install (kos_dyn_gen_install) pushes the Py_None the
     revive machinery pops. */
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
int kos_dyn_teardown_gen_exhausted(KosEvalState *es) {
  KosDynNode *frame = KosEvalState_DYN_TOP(es);
  if (! frame || ! KosDynNode_IS_FRAME(frame)) {
    PyErr_SetString(PyExc_RuntimeError,
                    "kos_dyn_teardown_gen_exhausted: dyn_top is not a frame");
    return -1;
  }
  KosDynGen *gen = (KosDynGen *) KosDynNode_PARENT(frame);
  if (! gen || ! KosDynNode_IS_GEN((KosDynNode *) gen)) {
    PyErr_SetString(PyExc_RuntimeError,
                    "kos_dyn_teardown_gen_exhausted: frame parent is not GEN");
    return -1;
  }
  KosDynNode *consumer = KosDynNode_PARENT(gen);

  KosEvalState_FlushLiveRegsToFrame(es);
  /* The commit resyncs the live registers from the node being removed
     (its parent, stack, and namespace), so it runs while the frame and
     GEN are still allocated; the chain is freed only afterwards. */
  kos_dyn_commit_dyn_top(es, consumer);
  kos_dynchain_free(frame, (KosDynNode *) gen);
  /* The dead GEN may outlive this drive on its owner; it holds no
     consumer namespace past the chain. */
  Py_CLEAR(KosDynNode_NS((KosDynNode *) gen));
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
