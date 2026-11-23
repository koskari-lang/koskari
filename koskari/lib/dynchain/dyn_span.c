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
  Included by dynchain.c. A suspended thread's state made an awaiter's:
  a Koskari coroutine, or a stepper's step, suspended at an await on
  its own context has its whole chain and the transfer goals waiting
  on it taken off that context and put under an awaiter's top, as if
  the awaiter had called it from the start. This is a context switch,
  not a transfer: no extent leaves or enters and no user code runs.
  Only the nodes that hold an effect outside themselves act, through
  kos_dyn_effect_out and kos_dyn_effect_in, and nothing here knows a
  generator's state beyond the GEN at a chain's bottom.

  :author: Christopher O'Brien <obriencj@gmail.com>
  :license: GNU General Public License v3
*/

/* === spans: a suspended thread's state === */


/**
   The depth of the highest frame a goal's target still holds off the
   chain, over goal and its outers, or 0. A target is numbered as it
   will stand once attached, on the context it was made on, so the
   first frame from its top is its highest.
*/
static KOS_NOINLINE unsigned kos_dyn_span_goals_reach(KosXferGoal *goal) {

  unsigned reach = 0;

  for (; goal; goal = KosXferGoal_OUTER(goal)) {
    KosDynNode *n = goal->target;

    while (n && ! KosDynNode_IS_FRAME(n))
      n = KosDynNode_PARENT(n);
    if (n && KosDynFrame_DEPTH(n) > reach)
      reach = KosDynFrame_DEPTH(n);
  }
  return reach;
}


/**
   Shift the frames each goal's target holds off the chain by base, the
   shift the attach gives the chain's own frames: both were numbered
   from 1 on the context the chain left. A target's off-chain part runs
   from its top down to a node with no parent.
*/
static KOS_NOINLINE void kos_dyn_span_goals_renumber(
    KosXferGoal *goal,
    unsigned base) {

  if (! base)
    return;

  for (; goal; goal = KosXferGoal_OUTER(goal)) {
    for (KosDynNode *n = goal->target; n; n = KosDynNode_PARENT(n)) {
      if (KosDynNode_IS_FRAME(n))
        KosDynFrame_DEPTH(n) = (uint16_t) (KosDynFrame_DEPTH(n) + base);
    }
  }
}


/**
   Put the span's goals above ctx's own. The span's chain sits above
   ctx's chain, so every node one of its goals waits on is above every
   node one of ctx's goals waits on, and ctx's active goal becomes the
   outer of the outermost of them. A goal whose seam was the root of
   the context it left, NULL, takes under: that root is now the bottom
   of the chain the awaiter called, on under. A voided-outer note on
   ctx's active goal passes up as kos_xfer_goal_push passes it.
*/
static KOS_NOINLINE void kos_dyn_span_goals_link(
    KosContext *ctx,
    KosXferGoal *goals,
    KosDynNode *under) {

  KosXferGoal *below = KosContext_GOAL(ctx);
  int voided = below && below->voided_outer;
  KosXferGoal *goal = goals;

  for (;;) {
    if (! goal->seam && under) {
      goal->seam = under;
      goal->seam_id = kos_dynnode_id(under);
    }
    if (voided)
      goal->voided_outer = 1;
    if (! KosXferGoal_OUTER(goal))
      break;
    goal = KosXferGoal_OUTER(goal);
  }

  KosXferGoal_OUTER(goal) = below;
  KosContext_GOAL(ctx) = goals;
}


int kos_dyn_span_detach(KosContext *ctx, KosDynSpan *span) {

  KosDynNode *head = KosContext_DYN_TOP(ctx);
  KosDynNode *last = NULL;

  /* the list is sized before anything moves */
  span->effects = (KosDynEffects) { 0 };
  if (kos_dyn_effects_collect(&span->effects, head, NULL, &last) < 0)
    return -1;

  /* the suspended frame's pc fields come from the registers, as at any
     chain edit */
  KosContext_FlushLiveRegsToFrame(ctx);

  /* every goal on the context waits on a node of this chain, or was
     voided by one that does: they are the suspended thread's as much
     as the chain is */
  span->goals = KosContext_GOAL(ctx);
  KosContext_GOAL(ctx) = NULL;

  /* A region's effect exchanges its handled exception with the
     thread's, which belongs to whoever is running: the awaiter, not
     this context. The context's own slot, where its handler state
     went when its last eval left, stands in for the thread while the
     effects go out, and takes back what they leave. */
  if (span->effects.count) {
    KosExcState thread_exc;

    KosExc_Init(&thread_exc);
    KosExc_Fetch(&thread_exc);
    KosExc_Install(KosContext_EXC_INFO(ctx));

    kos_dyn_effects_out(ctx, &span->effects);

    KosExc_Fetch(KosContext_EXC_INFO(ctx));
    KosExc_Install(&thread_exc);
  }

  /* the outermost node the walk met is the chain's bottom when it is
     a GEN with no parent: a GEN carries a skip bit */
  span->head = head;
  span->top_frame = (KosDynNode *) KosContext_FRAME(ctx);
  span->gen = (last && KosDynNode_IS_GEN(last) && ! KosDynNode_PARENT(last))
      ? last : NULL;
  KosContext_ClearLiveRegs(ctx);
  return 0;
}


int kos_dyn_span_attach(KosContext *ctx, KosDynSpan *span) {

  KosDynNode *under = KosContext_DYN_TOP(ctx);
  KosDynFrame *live = KosContext_FRAME(ctx);
  uint32_t demand = live ? (KosDynFrame_FLAGS(live) & KOS_FRAME_DEMAND) : 0;
  unsigned base = live ? KosDynFrame_DEPTH(live) : 0;
  unsigned depth;
  unsigned room;
  KosDynNode *root = NULL;

  if (! span->head) {
    PyErr_SetString(PyExc_SystemError, "kos_dyn_span_attach: empty span");
    return -1;
  }

  /* The chain's lowest frame is numbered 1 on its own context, so its
     top frame's depth is its frame count; a goal's target still off the
     chain was numbered there too, and may reach higher. Nothing
     renumbers before a refusal, so both read the same at a second
     attach. */
  depth = span->top_frame ? KosDynFrame_DEPTH(span->top_frame) : 0u;
  room = depth;
  if (unlikely(span->goals)) {
    unsigned reach = kos_dyn_span_goals_reach(span->goals);

    if (reach > room)
      room = reach;
  }
  if (kos_eval_check_frame_room(ctx, live, room) < 0)
    return -1;

  /* what the detach took out comes back before the link: a refusal
     here leaves the span resting, nothing linked */
  if (span->effects.count && kos_dyn_effects_in(ctx, &span->effects) < 0)
    return -1;

  /* One walk over the span: the frames are numbered from the live
     frame's depth, top down, and take its demand bit, and the bottom
     is found. The demand bit is drive-state, assigned to every frame
     of the span, not only the surfaced one: a frame lower in the span
     awaits after the one above it returns, and a child frame inherits
     the bit only at its creation. */
  depth += base;
  for (KosDynNode *n = span->head; n; n = KosDynNode_PARENT(n)) {
    root = n;
    if (! KosDynNode_IS_FRAME(n))
      continue;
    KosDynFrame_DEPTH(n) = (uint16_t) depth--;
    if (demand)
      KosDynFrame_FLAGS(n) |= KOS_FRAME_DEMAND;
    else
      KosDynFrame_FLAGS(n) &= ~KOS_FRAME_DEMAND;
  }

  /* the frames the goals' targets still hold take the chain's shift */
  if (unlikely(span->goals))
    kos_dyn_span_goals_renumber(span->goals, base);

  /* a generator's frames were numbered for the base its state records;
     numbered here for another, the record is void, so the next install
     at any base renumbers rather than trusting a match */
  if (span->gen)
    KosDynGen_STATE((KosDynGen *) span->gen)->base_depth = UINT16_MAX;

  /* the bottom's unwind caches name the frame and block beneath the
     splice; stamps never overwrite, so they are set before the commit
     rebuilds */
  if (KosDynNode_IS_FRAME(root))
    KosDynFrame_BACK_FRAME(root) = (KosDynNode *) live;
  if (KosDynNode_STACK(root))
    KosSegment_BACK(KosDynNode_STACK(root)) = KosContext_BLOCK(ctx);
  /* the skip links that reached the span's old bottom reach under now */
  kos_dynnode_link(root, under);
  kos_dyn_relink_skips(span->head, under);

  kos_dyn_commit_dyn_top(ctx, span->head);

  /* the goals go above ctx's own, as the chain goes above its chain */
  if (unlikely(span->goals)) {
    kos_dyn_span_goals_link(ctx, span->goals, under);
    span->goals = NULL;
  }

  kos_dyn_effects_clear(&span->effects);
  span->head = NULL;
  span->top_frame = NULL;
  span->gen = NULL;
  return 0;
}


void kos_dyn_span_drop(KosDynSpan *span) {

  KosXferGoal *goal = span->goals;

  kos_dyn_effects_clear(&span->effects);

  /* each goal is unlinked before it is freed, as a context's clear
     frees its goals */
  span->goals = NULL;
  while (goal) {
    KosXferGoal *outer = KosXferGoal_OUTER(goal);

    KosXferGoal_OUTER(goal) = NULL;
    kos_xfer_goal_discard(goal);
    goal = outer;
  }
}


// The end.
