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
  span. A chain moved between contexts is dyn_span.c's.

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
    gen = (KosDynGen *) kos_dynnode_alloc(KOS_DYN_GEN);
    if (! gen)
      return NULL;
    kos_dynnode_init((KosDynNode *) gen, KOS_DYN_GEN, NULL,
                     0);
    KosDynNode_NS(gen) = Py_NewRef(KosNamespace_Empty());
    KosDynGen_STATE(gen) = state;
  }
  KosDynGen_OWNER(gen) = owner;  /* borrowed */
  return gen;
}


void kos_dyngen_rest(KosDynGen *gen) {

  /* kos_dynnode_link onto nothing, for a node known not to be a frame */
  KosDynGen_OWNER(gen) = NULL;
  KosDynNode_PARENT(gen) = NULL;
  KosDynNode_SKIP(gen) = NULL;
  KosDynGen_STATE(gen)->node = (KosDynNode *) gen;
}


void kos_dyngen_free(KosDynGen *gen) {

  kos_dynnode_return_minor((KosDynNode *) gen);
}


/**
   The one free of a GEN node that knows the owner is alive: the state
   is the owner's, and it takes its resting node out before freeing
   it. An installed node is not in the state (kos_dyngen_new took it
   out), and goes with its chain.
*/
void kos_gen_state_clear(KosGenState *state) {

  KosDynNode *active = state->active;
  KosDynNode *node;

  state->active = NULL;
  if (active)
    kos_dynchain_free(active, NULL);

  node = state->node;
  state->node = NULL;
  if (node)
    kos_dyngen_free((KosDynGen *) node);

  /* the list named nodes of the span just freed */
  kos_dyn_effects_clear(&state->effects);
  PyMem_Free(state->pieces);
  state->pieces = NULL;
  state->piece_count = 0;
  state->piece_cap = 0;
  state->wound_count = 0;
}


/**
   Mark a flattened generator's owner closed, once the unlink that left
   its span has freed the GEN: the work a GEN's uninstall hands back to
   the unlink (kos_dyn_uninstall_handler). The owner is held by the
   record that steps the generator for the installed span: the for-each
   request, the next request's resume state, the native frame's record,
   the yield-from delegate slot, or a Python adapter. The unlink holds
   its own reference through the free and this finish, whose side
   effects may release the record.
*/
static void kos_dyngen_finish_owner(PyObject *owner) {

  KosIterSlots *islots;

  if (KosIter_CheckSlots(owner, &islots))
    islots->ki_finish(owner, 1);
}


static int dynnode_to_dict_gen(KosDynNode *node, PyObject *data) {

  return snapshot_set_obj(data, "owner", KosDynGen_OWNER(node));
}


KosDynGen *kos_dyn_find_gen(KosContext *ctx) {

  /* a GEN carries a skip bit, so the walk follows the skip links and
     meets only the nodes that carry one */
  KosDynNode *n = kos_dyn_skip_from((KosDynNode *) KosContext_FRAME(ctx));

  for (; n; n = KosDynNode_SKIP(n)) {
    if (KosDynNode_IS_GEN(n))
      return (KosDynGen *) n;
  }
  return NULL;
}


KosDynGen *kos_dyn_yield_gen(KosContext *ctx) {
  KosDynGen *gen = kos_dyn_find_gen(ctx);

  if (! gen)
    PyErr_SetString(PyExc_RuntimeError, "yield outside generator");
  return gen;
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
   Make regs the live registers. See dynchain.h.
*/
void kos_live_regs_apply(KosContext *ctx, const KosLiveRegs *regs) {
  KosContext_DYN_TOP(ctx) = regs->top;
  kos_dyn_resync_apply(ctx, (KosDynFrame *) regs->frame, regs->block,
                       regs->ns, 1);
}


/* === the installs === */


static inline KosExtent *kos_dyn_node_extent(const KosDynNode *node);

static int kos_dyn_gen_install_winding(
    KosContext *ctx,
    KosGenState *state,
    KosDynGen *gen,
    KosDynNode *active,
    uint32_t invoker_demand);


/**
   Put a resting span's bottom onto gen and gen onto the chain's top.
   The live registers are not changed: the caller loads the ones of
   whatever part of the span it leaves at dyn_top.
*/
static inline void kos_dyn_gen_splice(
    KosContext *ctx,
    KosGenState *state,
    KosDynGen *gen,
    KosDynNode *active,
    KosDynNode *bottom) {

  /* The consumer's registers, restored at the next freeze. */
  kos_live_regs_capture(ctx, &state->host);

  /* The span kept its interior unwind caches across the freeze; the
     two on its bottom node name the consumer, and are rewritten from
     the registers just recorded. Done before the registers change:
     stamps never overwrite, so a stale link here would survive the
     rebuild. */
  if (KosDynNode_IS_FRAME(bottom))
    KosDynFrame_BACK_FRAME(bottom) = state->host.frame;
  if (KosDynNode_STACK(bottom))
    KosSegment_BACK(KosDynNode_STACK(bottom)) = state->host.block;

  /* The GEN goes under the chain's top and the span above it; the
     span's interior links are intact, so only its bottom is relinked.
     The bottom's parent-is-frame flag is clear for a GEN as for the
     NULL it rested under, so no note is needed. Nor do the span's skip
     links change: the ones that reach below it name this GEN, the
     node the state keeps from one install to the next, so only the
     GEN's own is set, as kos_dynnode_link sets it for a node known
     not to be a frame. A span that rested under another GEN node has
     them set over again. */
  KosDynNode_PARENT(gen) = KosContext_DYN_TOP(ctx);
  KosDynNode_SKIP(gen) = kos_dynnode_skip_under(KosContext_DYN_TOP(ctx));
  KosDynNode_PARENT(bottom) = (KosDynNode *) gen;
  if (unlikely(KosDynNode_SKIP(bottom) != (KosDynNode *) gen))
    kos_dyn_relink_skips(active, (KosDynNode *) gen);
  state->active = NULL;
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


int kos_dyn_gen_install(KosContext *ctx, PyObject *owner, KosGenState *state) {

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
     next() does, with the generator left suspended. The span's
     frames are numbered without a gap, so their count is the top
     frame's depth less the bottom's, plus one; the invoker frame in
     hand is the base depth, and the numbering stands while it is the
     one of the last install. */
  {
    unsigned count = state->frozen.frame
        ? (unsigned) KosDynFrame_DEPTH(state->frozen.frame)
            - KosDynFrame_DEPTH(bottom) + 1u
        : 0u;
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
  if (state->effects.count && kos_dyn_effects_in(ctx, &state->effects) < 0) {
    kos_dyngen_rest(gen);
    return -1;
  }

  kos_dyn_gen_splice(ctx, state, gen, active, bottom);

  /* frozen.top equals the active head for a span kos_dyn_gen_freeze
     rested: it writes active and the frozen snapshot together and
     nothing else sets active non-NULL. A span whose yield left
     winders rests with frozen.top NULL (kos_dyn_gen_freeze_wound), so
     the one test tells it from the rest at no cost to them, and it
     goes on only up to its outermost winder. Any other mismatch takes
     the rebuild instead of stale registers; the same holds for
     host.top at the freeze. */
  if (state->frozen.top == active && state->frozen.frame) {
    /* The registers the freeze recorded for this very head: apply
       them. Same outcome as the rebuild without its find walk. */
    kos_live_regs_apply(ctx, &state->frozen);
  } else {
    if (state->piece_count)
      return kos_dyn_gen_install_winding(
          ctx, state, gen, active, invoker_demand);
    kos_dyn_commit_dyn_top(ctx, active);
  }
  state->frozen.top = NULL;

  kos_dyn_gen_inherit_demand(ctx, invoker_demand);

  /* The suspended yield expression's placeholder, replaced by the sent
     value at the drive. A frame waiting on a resume (a delegation in
     flight) takes its delivery through the pending slot instead. */
  {
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


/* === winding across a yield === */


void kos_dyn_gen_wind_reset(KosGenState *state) {

  state->effects.count = 0;
  state->wound_count = 0;
  state->piece_count = 0;
}


/**
   Record the listed winders, outermost first, each with the node
   resting on it: one walk by parent links from top down to the
   outermost winder. Returns 0, or -1 with MemoryError set.
*/
static int kos_dyn_gen_pieces(KosGenState *state, KosDynNode *top) {

  KosDynEffects *fx = &state->effects;
  KosDynNode **items = kos_dyn_effects_items(fx);
  KosDynNode *above = NULL;
  size_t count = 0;
  size_t at;

  for (size_t i = 0; i < fx->count; i++)
    count += (size_t) kos_dyn_node_winder(items[i]);

  if (count > state->piece_cap) {
    KosXferPiece *grown =
        PyMem_Realloc(state->pieces, count * sizeof(KosXferPiece));

    if (! grown) {
      PyErr_NoMemory();
      return -1;
    }
    state->pieces = grown;
    state->piece_cap = (uint16_t) count;
  }

  at = count;
  for (KosDynNode *n = top; at; n = KosDynNode_PARENT(n)) {
    if (kos_dyn_node_winder(n)) {
      KosXferPiece *piece = &state->pieces[--at];

      piece->rest = above;
      piece->regs = (KosLiveRegs) { 0 };
      piece->regs.top = n;
    }
    above = n;
  }
  state->piece_count = (uint16_t) count;
  return 0;
}


int kos_dyn_gen_wind_out_begin(
    KosContext *ctx,
    KosDynGen *gen,
    KosXferGoal *goal,
    PyObject *val) {

  KosGenState *state = KosDynGen_STATE(gen);

  if (kos_dyn_gen_pieces(state, KosContext_DYN_TOP(ctx)) < 0) {
    kos_dyn_gen_wind_reset(state);
    return -1;
  }

  /* the list is the winding's from here: the plain install puts
     nothing of it back */
  state->wound_count = state->effects.count;
  state->effects.count = 0;

  kos_xfer_goal_init(goal, NULL, NULL, val, NULL, KosDynGen_OWNER(gen), 0);
  goal->phase = KOS_GOAL_WINDING_OUT;
  goal->gen_state = state;
  goal->is_throw = 0;
  goal->demand = 0;
  KosContext_FlushLiveRegsToFrame(ctx);
  kos_live_regs_capture(ctx, &goal->shape.top);
  return 0;
}


KosDynNode *kos_dyn_gen_wind_out_next(KosContext *ctx, KosXferGoal *goal) {

  KosGenState *state = goal->gen_state;
  KosDynNode **items = kos_dyn_effects_items(&state->effects);
  /* the winder left last, whose rest is still cut off */
  KosXferPiece *left = goal->piece_entered
      ? &state->pieces[state->piece_count - goal->piece_entered] : NULL;

  while (goal->piece_next < state->wound_count) {
    KosDynNode *node = items[goal->piece_next];
    KosXferPiece *piece;

    if (! kos_dyn_node_winder(node)) {
      kos_dyn_effect_out(ctx, node);
      goal->piece_next++;
      continue;
    }

    piece = &state->pieces[state->piece_count - 1 - goal->piece_entered];
    if (! piece->regs.frame) {
      /* Cut above this winder before the one above it gets its rest
         back, so the nodes the goal holds never reach the chain. */
      if (piece->rest) {
        KosDynNode_PARENT(piece->rest) = NULL;
        goal->target = goal->shape.top.top;
      }
      if (left && left->rest)
        KosDynNode_PARENT(left->rest) = left->regs.top;
      if (KosContext_DYN_TOP(ctx) != node) {
        KosContext_DYN_TOP(ctx) = node;
        kos_dyn_resync_chain(ctx);
      }
      kos_live_regs_capture(ctx, &piece->regs);
      goal->resume_pc = KosContext_PC(ctx);
    }
    return node;
  }

  /* every winder is left: the span is whole at dyn_top again */
  if (left && left->rest)
    KosDynNode_PARENT(left->rest) = left->regs.top;
  goal->target = NULL;
  if (KosContext_DYN_TOP(ctx) != goal->shape.top.top)
    kos_live_regs_apply(ctx, &goal->shape.top);
  return NULL;
}


void kos_dyn_gen_wind_left(
    KosContext *ctx,
    KosXferGoal *goal,
    KosDynNode *node) {

  KosExtent *ext = kos_dyn_node_extent(node);

  KosExtent_PHASE(ext) = KOS_EXTENT_IDLE;
  KosExtent_ENTERED(ext) = 0;
  if (KosContext_FRAME(ctx))
    KosContext_SET_PC(ctx, goal->resume_pc);
  goal->piece_entered++;
  goal->piece_next++;
}


/**
   Put back the effects listed outside the outermost winder and leave
   the goal at that winder. Returns 0, or -1 with PyErr set and those
   effects out again.
*/
static int kos_dyn_gen_wind_in_outer(
    KosContext *ctx,
    KosGenState *state,
    KosXferGoal *goal) {

  KosDynNode **items = kos_dyn_effects_items(&state->effects);
  size_t at = state->wound_count;

  while (! kos_dyn_node_winder(items[at - 1])) {
    if (kos_dyn_effect_in(ctx, items[at - 1]) < 0) {
      for (; at < state->wound_count; at++)
        kos_dyn_effect_out(ctx, items[at]);
      return -1;
    }
    at--;
  }
  goal->piece_next = at - 1;
  return 0;
}


int kos_dyn_gen_wind_attach(KosContext *ctx, KosXferGoal *goal) {

  KosGenState *state = goal->gen_state;
  KosDynNode **items = kos_dyn_effects_items(&state->effects);
  KosXferPiece *piece = &state->pieces[goal->piece_entered];
  KosXferPiece *next = (goal->piece_entered + 1u < state->piece_count)
      ? piece + 1 : NULL;
  KosDynFrame *below = KosContext_FRAME(ctx);
  uint32_t demand = below
      ? (KosDynFrame_FLAGS(below) & KOS_FRAME_DEMAND) : 0;
  size_t at = goal->piece_next;

  /* the effects go back while their nodes are still off the chain, so
     a refusal leaves no node on it with its effect out */
  while (at && ! kos_dyn_node_winder(items[at - 1])) {
    if (kos_dyn_effect_in(ctx, items[at - 1]) < 0) {
      for (; at < goal->piece_next; at++)
        kos_dyn_effect_out(ctx, items[at]);
      return -1;
    }
    at--;
  }
  goal->piece_next = next ? at - 1 : at;

  /* cut above the next winder first, then give this one its rest */
  if (next && next->rest)
    KosDynNode_PARENT(next->rest) = NULL;
  else
    goal->target = NULL;
  if (piece->rest)
    KosDynNode_PARENT(piece->rest) = piece->regs.top;
  kos_live_regs_apply(ctx, next ? &next->regs : &goal->shape.top);

  /* each piece's frame runs under what the one below it runs under */
  kos_dyn_gen_inherit_demand(ctx, demand);
  goal->piece_entered++;
  return 0;
}


/**
   The rest of kos_dyn_gen_install for a span whose yield left
   winders, spliced onto gen with the consumer's registers still live:
   the effects outside the outermost winder go back, the span stays on
   the chain up to that winder, and a goal is pushed that enters each
   winder and attaches what rests on it. What rests on the outermost
   is cut off and is the goal's until then. A refusal takes the span
   off gen again and leaves it resting.

   On a running context the consumer's step goes on at once, and
   threads into whatever frame is live. None is left live here, so the
   step goes to the prologue (kos_iter_installed), whose drain runs
   the goal, which loads the registers the yield recorded with the
   winder at dyn_top (kos_dyn_gen_wind_load). A context that is not
   running enters the evaluator next, and gets those registers now.

   Returns 1, or -1 with PyErr set and the span resting.
*/
static KOS_NOINLINE int kos_dyn_gen_install_winding(
    KosContext *ctx,
    KosGenState *state,
    KosDynGen *gen,
    KosDynNode *active,
    uint32_t invoker_demand) {

  KosXferPiece *first = &state->pieces[0];
  KosXferGoal *goal = kos_xfer_goal_new(
      NULL, NULL, NULL, NULL, KosDynGen_OWNER(gen), 0);

  if (! goal || kos_dyn_gen_wind_in_outer(ctx, state, goal) < 0) {
    kos_xfer_goal_free(goal);
    KosDynNode_PARENT(state->bottom) = NULL;
    kos_dyngen_rest(gen);
    state->active = active;
    return -1;
  }

  goal->phase = KOS_GOAL_WINDING_IN;
  goal->gen_state = state;
  goal->is_throw = 0;
  goal->demand = invoker_demand != 0;
  goal->shape.top = state->frozen;
  goal->shape.top.top = active;
  if (first->rest) {
    KosDynNode_PARENT(first->rest) = NULL;
    goal->target = active;
  }
  kos_xfer_goal_push(ctx, goal);

  if (KosContext_FLAGS(ctx) & KOS_CTX_RUNNING) {
    KosContext_DYN_TOP(ctx) = first->regs.top;
    kos_dyn_resync_apply(ctx, NULL, first->regs.block, first->regs.ns, 1);
  } else {
    kos_dyn_gen_wind_load(ctx, goal);
    KosContext_SetSuspensionEvent(ctx, KOS_SUSP_WIND);
  }
  return 1;
}


void kos_dyn_gen_wind_load(KosContext *ctx, KosXferGoal *goal) {

  kos_live_regs_apply(ctx, &goal->gen_state->pieces[0].regs);
  kos_dyn_gen_inherit_demand(ctx, goal->demand ? KOS_FRAME_DEMAND : 0);
}


void kos_dyn_gen_unsplice(KosContext *ctx, KosGenState *state) {

  KosDynNode *bottom = state->bottom;
  KosDynGen *gen;
  PyObject *typ, *val, *tb;

  if (! bottom)
    return;
  gen = (KosDynGen *) KosDynNode_PARENT(bottom);
  if (! gen || ! KosDynNode_IS_GEN(gen))
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
    /* the GEN goes under nodes linked before it existed: their skip
       links are set over again, once in the generator's life */
    kos_dynnode_link(root, (KosDynNode *) gen);
    kos_dyn_relink_skips(chain_head, (KosDynNode *) gen);
    state->started = 1;
  }

  kos_dyn_renumber_frames_counted(
      chain_head, (KosDynNode *) gen,
      invoker_frame ? KosDynFrame_DEPTH(invoker_frame) : 0, count);

  /* The consumer's registers, restored at the first freeze. */
  kos_live_regs_capture(ctx, &state->host);

  kos_dynnode_link((KosDynNode *) gen, KosContext_DYN_TOP(ctx));
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
