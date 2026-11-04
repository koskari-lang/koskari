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
  that intercepts yields and denies shifts, on the chain only while
  the generator's span is installed, and the installs of a resting
  span.

  :author: Christopher O'Brien <obriencj@gmail.com>
  :license: GNU General Public License v3
*/

/* === KosGenState and the GEN node === */


void kos_gen_state_init(KosGenState *state, KosDynFrame *frame) {

  *state = (KosGenState) { 0 };
  state->active = (KosDynNode *) frame;
  state->bottom = (KosDynNode *) frame;
  state->base_depth = UINT16_MAX;
}


/**
   The namespace slot holds the empty namespace for the node's life:
   no walk reads it, since every span has the generator's frame
   between its head and the GEN, and the fill keeps the node free of
   any consumer reference.
*/
KosDynGen *kos_dyngen_new(PyObject *owner, KosGenState *state) {

  KosDynGen *gen = (KosDynGen *) state->node;

  if (gen) {
    state->node = NULL;
  } else {
    gen = (KosDynGen *) kos_dynnode_alloc();
    if (! gen)
      return NULL;
    kos_dynnode_init((KosDynNode *) gen, KOS_DYN_GEN, NULL,
                     KOS_DYN_CAP_INTERCEPTS_RAISE);
    KosDynNode_NS(gen) = Py_NewRef(KosNamespace_Empty());
    KosDynGen_STATE(gen) = state;
  }
  KosDynGen_OWNER(gen) = owner;  /* borrowed */
  return gen;
}


void kos_dyngen_rest(KosDynGen *gen) {

  KosDynGen_OWNER(gen) = NULL;
  KosDynNode_PARENT(gen) = NULL;
  KosDynGen_STATE(gen)->node = (KosDynNode *) gen;
}


void kos_dyngen_free(KosDynGen *gen) {

  KosGenState *state = KosDynGen_STATE(gen);

  if (state->node == (KosDynNode *) gen)
    state->node = NULL;
  kos_dynnode_return_to_freelist((KosDynNode *) gen);
}


void kos_gen_state_clear(KosGenState *state) {

  KosDynNode *active = state->active;

  state->active = NULL;
  if (active)
    kos_dynchain_free(active, NULL);
  if (state->node)
    kos_dyngen_free((KosDynGen *) state->node);
}


/**
   The owner is held by the record that steps the generator for the
   installed span: the for-each request, the next request's resume
   state, the native frame's record, the yield-from delegate slot, or
   a Python adapter. The reference taken here keeps it through the
   free and the finish, whose side effects may release the record.
*/
void kos_dyn_finish_flattened_gen(KosContext *ctx, KosDynGen *gen) {

  PyObject *typ = NULL, *val = NULL, *tb = NULL;
  KosDynNode *consumer = KosDynNode_PARENT(gen);
  PyObject *owner = Py_NewRef(KosDynGen_OWNER(gen));
  KosIterSlots *islots;

  PyErr_Fetch(&typ, &val, &tb);
  kos_dyn_commit_dyn_top(ctx, consumer);
  kos_dynnode_free((KosDynNode *) gen);
  PyErr_Restore(typ, val, tb);
  if (KosIter_CheckSlots(owner, &islots))
    islots->ki_finish(owner, 1);
  Py_DECREF(owner);
}


static int dynnode_to_dict_gen(KosDynNode *node, PyObject *data) {

  return snapshot_set_obj(data, "owner", KosDynGen_OWNER(node));
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


/* === the installs === */


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


/**
   The demand bit is drive-state: forcibly assigned to the generator's
   frame at every entry, inherited from the frame below the splice.
   Adapter-driven installs have no frame below (invoker 0), so an async
   drive self-cleans any bit a prior demand drive left behind; the
   demand boundary ORs its forced bit in after this install.
*/
static void kos_dyn_gen_inherit_demand(
    KosContext *ctx,
    uint32_t invoker_demand) {

  KosDynFrame *surfaced = KosContext_FRAME(ctx);

  if (! surfaced)
    return;
  if (invoker_demand)
    KosDynFrame_FLAGS(surfaced) |= KOS_FRAME_DEMAND;
  else
    KosDynFrame_FLAGS(surfaced) &= ~KOS_FRAME_DEMAND;
}


/**
   The install both entries share. placeholder: push the Py_None a
   frame resumed at its yield expects; a span parked at an await takes
   the awaited value as the await's own revive pushes it.
*/
static int kos_dyn_gen_install_span_at(
    KosContext *ctx,
    PyObject *owner,
    KosGenState *state,
    int placeholder) {

  KosDynNode *active = state->active;
  KosDynNode *bottom = state->bottom;
  KosDynFrame *invoker_frame = KosContext_FRAME(ctx);
  uint32_t invoker_demand = invoker_frame
      ? (KosDynFrame_FLAGS(invoker_frame) & KOS_FRAME_DEMAND)
      : 0;
  KosDynGen *gen;

  SAYF("[gen] install ENTER ctx=%p state=%p dyn_top=%p ctx_dyn_top_kind=%s "
       "active=%p",
       (void *) ctx, (void *) state,
       (void *) KosContext_DYN_TOP(ctx),
       KosContext_DYN_TOP(ctx)
           ? kos_dynnode_kind_name(KosDynNode_KIND(KosContext_DYN_TOP(ctx)))
           : "NULL",
       (void *) active);

  if (! active || ! bottom || ! state->started) {
    PyErr_SetString(PyExc_RuntimeError,
                    "kos_dyn_gen_install: no resting started span");
    return -1;
  }

  gen = kos_dyngen_new(owner, state);
  if (! gen)
    return -1;

  /* The resumed frames sit above the consumer's frame; a resume that
     would not fit raises RecursionError at the request, as Python's
     next() does, with the generator left suspended. The freeze
     counted the frames; the invoker frame in hand is the base depth,
     and the numbering stands while it is the one of the last install. */
  {
    unsigned count = state->nframes;
    unsigned base = invoker_frame ? KosDynFrame_DEPTH(invoker_frame) : 0;

    if (kos_eval_check_frame_room(ctx, invoker_frame, count) < 0) {
      kos_dyngen_rest(gen);
      return -1;
    }
    if (base != state->base_depth) {
      kos_dyn_renumber_frames_counted(active, NULL, base, count);
      state->base_depth = (uint16_t) base;
    }
  }

  /* What the freeze took out comes back before the splice: a refusal
     here leaves the span resting, nothing spliced. */
  if (state->installs) {
    if (kos_dyn_gen_install_span(ctx, active) < 0) {
      kos_dyngen_rest(gen);
      return -1;
    }
    state->installs = 0;
  }

  /* The consumer's registers, restored at the next freeze. */
  kos_live_regs_capture(ctx, &state->host);

  /* The span kept its interior unwind caches across the freeze; the
     two on its bottom node name the consumer, and are rewritten from
     the registers just recorded. Done before either splice path
     below: stamps never overwrite, so a stale link here would survive
     the rebuild. */
  if (KosDynNode_IS_FRAME(bottom))
    KosDynFrame_BACK_FRAME(bottom) = state->host.frame;
  if (KosDynNode_STACK(bottom))
    KosSegment_BACK(KosDynNode_STACK(bottom)) = state->host.block;

  /* The GEN goes under the chain's top and the span above it; the
     span's interior links are intact, so only its bottom is relinked.
     The bottom's parent-is-frame flag is clear for a GEN as for the
     NULL it rested under, so no note is needed. */
  KosDynNode_PARENT(gen) = KosContext_DYN_TOP(ctx);
  KosDynNode_PARENT(bottom) = (KosDynNode *) gen;
  state->active = NULL;

  /* frozen.top always equals the active head here: kos_dyn_gen_freeze
     writes active and the frozen snapshot together and nothing else
     sets active non-NULL. The compare is kept so a future freeze path
     that records one without the other lands on the rebuild instead of
     stale registers; the same holds for host.top at the freeze. */
  if (state->frozen.top == active && state->frozen.frame) {
    /* The registers the freeze recorded for this very head: apply
       them. Same outcome as the rebuild without its find walk. */
    kos_live_regs_apply(ctx, &state->frozen);
  } else {
    kos_dyn_commit_dyn_top(ctx, active);
  }
  state->frozen.top = NULL;

  kos_dyn_gen_inherit_demand(ctx, invoker_demand);

  /* The suspended yield expression's placeholder, replaced by the sent
     value at the drive. A frame waiting on a resume (a delegation in
     flight) takes its delivery through the pending slot instead. */
  if (placeholder) {
    KosDynFrame *surfaced = KosContext_FRAME(ctx);
    if (! (surfaced && KosDynFrame_RESUME_KIND(surfaced))
        && KosContext_StackPushRef(ctx, Py_None) < 0) {
      /* the span is spliced and nothing else has run: put it back to
         rest, so the generator stays suspended where it was */
      kos_dyn_gen_unsplice(ctx, state);
      return -1;
    }
  }

  SAYF("[gen] install DONE dyn_top=%p (%s) frame_top=%p",
       (void *) KosContext_DYN_TOP(ctx),
       KosContext_DYN_TOP(ctx)
           ? kos_dynnode_kind_name(KosDynNode_KIND(KosContext_DYN_TOP(ctx)))
           : "NULL",
       (void *) KosContext_FRAME(ctx));
  return 0;
}


int kos_dyn_gen_install(KosContext *ctx, PyObject *owner, KosGenState *state) {

  return kos_dyn_gen_install_span_at(ctx, owner, state, 1);
}


int kos_dyn_gen_install_awaiting(
    KosContext *ctx,
    PyObject *owner,
    KosGenState *state) {

  return kos_dyn_gen_install_span_at(ctx, owner, state, 0);
}


void kos_dyn_gen_unsplice(KosContext *ctx, KosGenState *state) {

  KosDynNode *bottom = state->bottom;
  KosDynGen *gen;
  PyObject *typ, *val, *tb;

  if (! bottom)
    return;
  gen = (KosDynGen *) KosDynNode_PARENT(bottom);
  if (! gen || KosDynNode_KIND(gen) != KOS_DYN_GEN)
    return;

  /* the failure's error is the one the caller reports; the freeze's
     own, should it have one, is not */
  PyErr_Fetch(&typ, &val, &tb);
  if (kos_dyn_gen_freeze(ctx, gen) < 0)
    PyErr_Clear();
  PyErr_Restore(typ, val, tb);
}


int kos_dyn_gen_install_bootstrap(
    KosContext *ctx,
    PyObject *owner,
    KosGenState *state,
    KosDynNode *chain_head) {

  KosDynNode *n;
  KosDynNode *root = NULL;
  KosDynGen *gen = NULL;
  unsigned count = 0;
  KosDynFrame *invoker_frame;
  uint32_t invoker_demand;

  if (! ctx || ! chain_head) {
    PyErr_SetString(PyExc_RuntimeError,
                    "kos_dyn_gen_install_bootstrap: NULL ctx or empty chain");
    return -1;
  }

  invoker_frame = KosContext_FRAME(ctx);
  invoker_demand = invoker_frame
      ? (KosDynFrame_FLAGS(invoker_frame) & KOS_FRAME_DEMAND)
      : 0;

  /* The walk to the root counts the frames the room check and the
     renumber need, and finds the GEN when the chain carries one. */
  for (n = chain_head; n; n = KosDynNode_PARENT(n)) {
    if (KosDynNode_IS_GEN(n)) {
      gen = (KosDynGen *) n;
      break;
    }
    if (KosDynNode_IS_FRAME(n))
      count++;
    root = n;
  }

  if (gen) {
    /* a chain in flight, moved from a context's dyn top: its GEN is
       the one made for this span, relinked under the consumer */
    if (KosDynNode_PARENT(gen)) {
      PyErr_SetString(PyExc_RuntimeError,
                      "kos_dyn_gen_install_bootstrap: the GEN is not the"
                      " chain's root");
      return -1;
    }
    if (kos_eval_check_frame_room(ctx, invoker_frame, count) < 0)
      return -1;
  } else {
    /* the unstarted generator's frame: its first install gets the GEN */
    if (kos_eval_check_frame_room(ctx, invoker_frame, count) < 0)
      return -1;
    gen = kos_dyngen_new(owner, state);
    if (! gen)
      return -1;
    KosDynNode_PARENT(root) = (KosDynNode *) gen;
    if (KosDynNode_IS_FRAME(root))
      kos_dynframe_note_parent((KosDynFrame *) root);
    state->started = 1;
  }

  kos_dyn_renumber_frames_counted(
      chain_head, (KosDynNode *) gen,
      invoker_frame ? KosDynFrame_DEPTH(invoker_frame) : 0, count);

  /* The consumer's registers, restored at the first freeze. */
  kos_live_regs_capture(ctx, &state->host);

  KosDynNode_PARENT(gen) = KosContext_DYN_TOP(ctx);
  kos_dyn_commit_dyn_top(ctx, chain_head);

  kos_dyn_gen_inherit_demand(ctx, invoker_demand);

  /* No resume placeholder: a bootstrap install is the generator's first
     drive, entering fresh code rather than reviving a suspended yield.
     Only the frozen install (kos_dyn_gen_install) pushes the Py_None the
     revive machinery pops. */
  return 0;
}


/**
   Unwind an exhausted gen from the consumer chain. Call when the gen's
   frame does RETURN and parent is GEN (top-level return of generator).
   Assumes ctx->dyn_top is the gen's frame, frame->parent is gen.
   Frees the chain from the frame down through the GEN, and restores
   the consumer.
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
  kos_dynchain_free(frame, consumer);
  return 0;
}


/* === GC traversal === */


/**
   GC traverse a KOS_DYN_GEN: nothing. The owner is borrowed, and the
   resting span is the owner's to traverse.
*/
static int dynnode_traverse_gen(KosDynNode *node, visitproc visit, void *arg) {
  (void) node;
  (void) visit;
  (void) arg;
  return 0;
}


// The end.
