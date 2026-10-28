/*
  This library is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 3 of the License, or
  (at your option) any later version.

  This library is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this library; if not, see <http://www.gnu.org/licenses/>.
*/


/**
  Included by dynchain.c. The KOS_DYN_GEN node: the generator boundary
  that intercepts yields and denies shifts.

  :author: Christopher O'Brien <obriencj@gmail.com>
  :license: GNU General Public License v3
*/

/* === KosDynGen implementation === */


/**
   Allocate a GEN node; owner borrowed, refcount for sharing. Callers:
   generator object setup. The namespace slot holds the empty
   namespace for the node's life: no walk reads it, since every span
   has the generator's frame between its head and the GEN, and the
   fill keeps the node free of any consumer reference.
*/
KosDynGen *kos_dyngen_new(PyObject *owner) {
  KosDynGen *gen = (KosDynGen *) kos_dynnode_alloc();
  if (! gen)
    return NULL;

  kos_dynnode_init((KosDynNode *) gen, KOS_DYN_GEN, NULL,
                   KOS_DYN_CAP_INTERCEPTS_RAISE);
  KosDynNode_NS(gen) = Py_NewRef(KosNamespace_Empty());
  KosDynGen_OWNER(gen) = owner;  /* borrowed */
  KosDynGen_ACTIVE(gen) = NULL;
  KosDynGen_REFCOUNT(gen) = 1;
  KosDynGen_NFRAMES(gen) = 0;
  KosDynGen_BASE_DEPTH(gen) = UINT16_MAX;
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
   The owner is borrowed at every moment of the GEN's life. The record
   that steps the generator holds the owner for the installed span:
   the for-each request, the next request's resume state, the native
   frame's record, the yield-from delegate slot, or a Python adapter;
   the span sits above that record in the chain and a GEN refuses
   continuation capture, so the record cannot go while the span is
   live.
*/
void kos_dyn_finish_flattened_gen(KosContext *ctx, KosDynGen *gen) {

  PyObject *typ = NULL, *val = NULL, *tb = NULL;
  KosDynNode *consumer = KosDynNode_PARENT(gen);
  PyObject *owner = KosDynGen_OWNER(gen);
  KosIterSlots *islots;

  if (owner && KosIter_CheckSlots(owner, &islots))
    islots->ki_finish(owner, 1);
  PyErr_Fetch(&typ, &val, &tb);
  kos_dyn_commit_dyn_top(ctx, consumer);
  KosDynNode_PARENT(gen) = NULL;
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


KosDynGen *kos_dyn_find_gen(KosContext *ctx) {
  KosDynNode *n = (KosDynNode *) KosContext_FRAME(ctx);

  for (; n; n = KosDynNode_PARENT(n)) {
    if (KosDynNode_IS_GEN(n))
      return (KosDynGen *) n;
  }
  return NULL;
}


/**
   Capture the live registers at the current dyn_top into regs.
*/
static void kos_live_regs_capture(KosContext *ctx, KosLiveRegs *regs) {
  regs->top = KosContext_DYN_TOP(ctx);
  regs->frame = (KosDynNode *) KosContext_FRAME(ctx);
  regs->block = KosContext_BLOCK(ctx);
  regs->ns = KosContext_NAMESPACE(ctx);
}


/**
   Make regs the live registers: dyn_top and the retarget a resync from
   that top would produce, with the outgoing frame's pc flushed.
*/
static void kos_live_regs_apply(KosContext *ctx, const KosLiveRegs *regs) {
  KosContext_DYN_TOP(ctx) = regs->top;
  kos_dyn_resync_apply(ctx, (KosDynFrame *) regs->frame, regs->block,
                       regs->ns, 1);
}


/**
   Resume generator: graft GEN under ctx, swap in frozen active chain, push
   None. Callers: eval when entering generator body from consumer.
*/
/**
   Put back what the freeze took out, outermost first, so each node's
   record of the table beneath it is the consumer's table as it is now.
   On a failure the nodes already installed are frozen again and the
   span is as the freeze left it.
*/
static int kos_dyn_gen_install_span(KosContext *ctx, KosDynNode *node) {
  if (! node)
    return 0;
  if (kos_dyn_gen_install_span(ctx, KosDynNode_PARENT(node)) < 0)
    return -1;
  if (kos_dyn_extent_install(ctx, node) < 0) {
    for (KosDynNode *n = KosDynNode_PARENT(node); n; n = KosDynNode_PARENT(n))
      (void) kos_dyn_extent_freeze(ctx, n);
    return -1;
  }
  return 0;
}


int kos_dyn_gen_install(KosContext *ctx, KosDynGen *gen) {
  KosDynNode *active;
  KosDynNode *bottom;
  KosDynFrame *invoker_frame;
  uint32_t invoker_demand;

  /* The demand bit is drive-state: forcibly assigned to the generator's
     frame at every entry, inherited from the frame below the splice.
     Adapter-driven installs have no frame below (invoker 0), so an async
     drive self-cleans any bit a prior demand drive left behind; the
     demand boundary ORs its forced bit in after this install. */
  invoker_frame = KosContext_FRAME(ctx);
  invoker_demand = invoker_frame
      ? (KosDynFrame_FLAGS(invoker_frame) & KOS_FRAME_DEMAND)
      : 0;

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
    // for (n = (KosDynNode *) KosDynGen_ACTIVE(gen); n; n = KosDynNode_PARENT(n))
    //   SAYF("  %p %s -> ", (void *) n,
    //        kos_dynnode_kind_name(KosDynNode_KIND(n)));
    SAY("(end)");
  }

  active = (KosDynNode *) KosDynGen_ACTIVE(gen);
  if (! active) {
    PyErr_SetString(PyExc_RuntimeError,
                    "kos_dyn_gen_install: GEN has no active frame");
    return -1;
  }

  /* The span's bottom node, whose parent is the GEN: found before
     anything is installed, so a frozen frame that is not in the span
     is a refusal that leaves the GEN frozen and nothing spliced. */
  bottom = KosDynGen_FROZEN(gen)->frame
      ? (KosDynNode *) KosDynGen_FROZEN(gen)->frame
      : active;
  while (bottom && KosDynNode_PARENT(bottom) != (KosDynNode *) gen)
    bottom = KosDynNode_PARENT(bottom);
  if (! bottom) {
    PyErr_SetString(PyExc_RuntimeError,
                    "kos_dyn_gen_install: frozen frame is not in the"
                    " GEN's span");
    return -1;
  }

  /* The resumed frames sit above the consumer's frame; a resume that
     would not fit raises RecursionError at the request, as Python's
     next() does, with the generator left suspended. The freeze
     counted the frames; the invoker frame in hand is the base depth,
     and the numbering stands while it is the one of the last install. */
  {
    unsigned count = KosDynGen_NFRAMES(gen);
    unsigned base = invoker_frame ? KosDynFrame_DEPTH(invoker_frame) : 0;

    if (kos_eval_check_frame_room(ctx, invoker_frame, count) < 0)
      return -1;
    if (base != KosDynGen_BASE_DEPTH(gen)) {
      kos_dyn_renumber_frames_counted(active, (KosDynNode *) gen, base,
                                      count);
      KosDynGen_BASE_DEPTH(gen) = (uint16_t) base;
    }
  }

  /* What the freeze took out comes back before the splice: a refusal
     here leaves the GEN frozen, nothing spliced. */
  if (KosDynNode_FLAGS(gen) & KOS_DYNF_GEN_INSTALLS) {
    if (kos_dyn_gen_install_span(ctx, active) < 0)
      return -1;
    KosDynNode_FLAGS(gen) &= ~KOS_DYNF_GEN_INSTALLS;
  }

  /* The consumer's registers, restored at the next freeze. */
  kos_live_regs_capture(ctx, KosDynGen_HOST(gen));

  /* The span kept its interior unwind caches across the freeze; the
     two on its bottom frame name the consumer, and are rewritten from
     the registers just recorded. Done before either splice path
     below: stamps never overwrite, so a stale link here would survive
     the rebuild. */
  if (KosDynNode_IS_FRAME(bottom))
    KosDynFrame_BACK_FRAME(bottom) =
        (KosDynNode *) KosDynGen_HOST(gen)->frame;
  if (KosDynNode_STACK(bottom))
    KosSegment_BACK(KosDynNode_STACK(bottom)) = KosDynGen_HOST(gen)->block;

  /* 1. GEN's parent = current ctx's active node */
  KosDynNode_PARENT(gen) = KosContext_DYN_TOP(ctx);
  /* 2. Incref GEN */
  kos_dyngen_acquire(gen);
  /* 3. Put GEN's active onto ctx's active; 4. NULL GEN's active.
   * Do NOT overwrite active->parent: the stored chain active -> ... -> gen
   * is already intact (e.g. FOREACH -> FRAME -> GEN). Overwriting
   * would sever links when active is not a direct child of gen. */
  KosDynGen_ACTIVE(gen) = NULL;
  /* frozen.top always equals the active head here: kos_dyn_gen_freeze
     writes ACTIVE and the frozen snapshot together and nothing else
     sets ACTIVE non-NULL. The compare is kept so a future freeze path
     that records one without the other lands on the rebuild instead of
     stale registers; the same holds for host.top at the freeze. */
  if (KosDynGen_FROZEN(gen)->top == active && KosDynGen_FROZEN(gen)->frame) {
    /* The registers the freeze recorded for this very head: apply
       them. Same outcome as the rebuild without its find walk. */
    kos_live_regs_apply(ctx, KosDynGen_FROZEN(gen));
  } else {
    kos_dyn_commit_dyn_top(ctx, active);
  }
  KosDynGen_FROZEN(gen)->top = NULL;

  {
    KosDynFrame *surfaced = KosContext_FRAME(ctx);
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
    KosDynFrame *surfaced = KosContext_FRAME(ctx);
    if (! (surfaced && KosDynFrame_RESUME_KIND(surfaced))
        && KosContext_StackPushRef(ctx, Py_None) < 0)
      return -1;
  }

  SAYF("[gen] install DONE dyn_top=%p (%s) frame_top=%p chain: ",
       (void *) KosContext_DYN_TOP(ctx),
       KosContext_DYN_TOP(ctx)
           ? kos_dynnode_kind_name(KosDynNode_KIND(KosContext_DYN_TOP(ctx)))
           : "NULL",
       (void *) KosContext_FRAME(ctx));
#if VERBOSE_DEBUG
  {
    KosDynNode *n;
    for (n = KosContext_DYN_TOP(ctx); n; n = KosDynNode_PARENT(n))
      SAYF("%p %s -> ", (void *) n, kos_dynnode_kind_name(KosDynNode_KIND(n)));
    SAY("(end)");
  }
#endif
  return 0;
}


/**
   First-run bootstrap: graft GEN and the chain that ends at it onto the
   consumer ctx. chain_head is the head of that chain (head -> ... -> gen),
   taken out of wherever it rested by the caller: the GEN's active slot
   for a generator never driven, or a context's dyn top.
*/
int kos_dyn_gen_install_bootstrap(
    KosContext *ctx,
    KosDynGen *gen,
    KosDynNode *chain_head) {

  KosDynNode *n;
  KosDynFrame *invoker_frame;
  uint32_t invoker_demand;

  if (! ctx || ! gen) {
    PyErr_SetString(PyExc_RuntimeError,
                    "NULL ctx or gen for gen_install_bootstrap");
    return -1;
  }

  /* Drive-state demand bit: see kos_dyn_gen_install. */
  invoker_frame = KosContext_FRAME(ctx);
  invoker_demand = invoker_frame
      ? (KosDynFrame_FLAGS(invoker_frame) & KOS_FRAME_DEMAND)
      : 0;

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
    if (kos_eval_check_frame_room(ctx, invoker_frame, count) < 0)
      return -1;
    kos_dyn_renumber_frames_counted(
        chain_head, (KosDynNode *) gen,
        invoker_frame ? KosDynFrame_DEPTH(invoker_frame) : 0, count);
  }

  /* The consumer's registers, restored at the first freeze. */
  kos_live_regs_capture(ctx, KosDynGen_HOST(gen));

  KosDynNode_PARENT(gen) = KosContext_DYN_TOP(ctx);
  /* The GEN's chain-membership reference travels with the chain onto
     ctx; do NOT acquire a fresh one, or the reference the resting place
     held is orphaned and the GEN node leaks. */
  kos_dyn_commit_dyn_top(ctx, chain_head);
  KosDynNode_FLAGS(gen) |= KOS_DYNF_GEN_STARTED;

  {
    KosDynFrame *surfaced = KosContext_FRAME(ctx);
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


int kos_dyn_gen_frozen_needs_install(KosContext *ctx, KosDynGen *g) {

  KosDynNode *n;

  if (! ctx || ! g)
    return 0;
  if (! KosDynGen_ACTIVE(g))
    return 0;

  for (n = KosContext_DYN_TOP(ctx); n; n = KosDynNode_PARENT(n)) {
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
int kos_dyn_teardown_gen_exhausted(KosContext *ctx) {
  KosDynNode *frame = KosContext_DYN_TOP(ctx);
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

  KosContext_FlushLiveRegsToFrame(ctx);
  /* The commit resyncs the live registers from the node being removed
     (its parent, stack, and namespace), so it runs while the frame and
     GEN are still allocated; the chain is freed only afterwards. */
  kos_dyn_commit_dyn_top(ctx, consumer);
  kos_dynchain_free(frame, (KosDynNode *) gen);
  /* The dead GEN may outlive this drive on its owner; it holds no
     consumer link past the chain. */
  KosDynNode_PARENT(gen) = NULL;
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
  /* OWNER is a borrowed back-pointer and is not visited: reporting it
     would make the collector subtract a phantom reference and collect
     a live, externally-referenced generator as cyclic garbage. */
  if (KosDynGen_ACTIVE((KosDynGen *) node))
    return kos_dynchain_traverse(
        KosDynGen_ACTIVE((KosDynGen *) node),
        node,
        visit,
        arg);
  return 0;
}


// The end.
