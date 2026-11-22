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
  Included by dynchain.c. A chain moved between contexts: a suspended
  coroutine's or stepper's whole chain taken off its own context and
  put under an awaiter's top. This is a context switch, not a
  transfer: no extent leaves or enters and no user code runs. Only
  the nodes that hold an effect outside themselves act, through
  kos_dyn_effect_out and kos_dyn_effect_in, and nothing here knows a
  generator's state beyond the GEN at a chain's bottom.

  :author: Christopher O'Brien <obriencj@gmail.com>
  :license: GNU General Public License v3
*/

/* === spans: a chain moved between contexts === */


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
  KosDynNode *root = NULL;

  if (! span->head) {
    PyErr_SetString(PyExc_SystemError, "kos_dyn_span_attach: empty span");
    return -1;
  }

  /* The chain's lowest frame is numbered 1 on its own context, so its
     top frame's depth is its frame count. Nothing renumbers before a
     refusal, so the count reads the same at a second attach. */
  depth = span->top_frame ? KosDynFrame_DEPTH(span->top_frame) : 0u;
  if (kos_eval_check_frame_room(ctx, live, depth) < 0)
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

  kos_dyn_effects_clear(&span->effects);
  span->head = NULL;
  span->top_frame = NULL;
  span->gen = NULL;
  return 0;
}


void kos_dyn_span_drop(KosDynSpan *span) {

  kos_dyn_effects_clear(&span->effects);
}


// The end.
