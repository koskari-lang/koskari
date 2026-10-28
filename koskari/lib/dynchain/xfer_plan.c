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
  Included by dynchain.c. Continuation invocation: the in-place resume
  and the transfer plan that grafts a captured chain onto the call
  site.

  :author: Christopher O'Brien <obriencj@gmail.com>
  :license: GNU General Public License v3
*/

/* === Continuation invocation === */


/**
   In-place (Path A) invoke readiness: the capture-time live prompt and
   shift nodes are both still on the current chain (pointer + node_id
   match), the shift node lies above the prompt, no barrier node (foreign
   DELIM, GEN, PROMPT) sits between dyn_top and the shift node,
   and no transfer goal is active. Returns 1 when in-place invoke
   applies, 0 otherwise (fall back to the PUSH path).

   On success, *out_nested_frame is the first FRAME between the SHIFT and
   its prompt (the nested handler's frame), or NULL when the handler
   shares the prompt's frame -- recorded here so the invoke does not
   re-walk the same stretch.
*/
static int kos_delimited_inplace_ready(
    KosContext *ctx,
    KosContinuation *k,
    KosDynFrame **out_nested_frame) {

  KosDynNode *prompt = KosContinuation_LIVE_PROMPT_NODE(k);
  KosDynNode *shift = KosContinuation_LIVE_SHIFT_NODE(k);
  KosDynNode *node;

  if (! prompt || ! shift)
    return 0;
  if (KosContext_GOAL(ctx))
    return 0;

  node = KosContext_DYN_TOP(ctx);
  while (node && node != shift) {
    switch (KosDynNode_KIND(node)) {
      case KOS_DYN_DELIM:
      case KOS_DYN_GEN:
      case KOS_DYN_PROMPT:
        return 0;
      default:
        break;
    }
    node = KosDynNode_PARENT(node);
  }
  if (! node || ! KosDynNode_IS_SHIFT(node)
      || kos_dynnode_id(node) != KosContinuation_LIVE_SHIFT_NODE_ID(k))
    return 0;

  *out_nested_frame = NULL;
  node = KosDynNode_PARENT(node);
  while (node && node != prompt) {
    /* An INPLACE delim between this SHIFT and the prompt means another
       invoke against the same prompt is already in flight: its span is
       detached and its restitch still expects to find itself directly
       above that prompt. Inserting a second delim there re-parents the
       first, which then fails with "inconsistent INPLACE delim state".
       This is the re-entrant case -- a shift firing inside a continuation
       that is being resumed -- and it belongs on the graft, which
       instantiates the template per invoke rather than mutating the live
       chain. The dyn_top-to-SHIFT walk above already rejects barriers;
       this stretch was unchecked. */
    if (KosDynNode_IS_DELIM(node))
      return 0;
    if (! *out_nested_frame && KosDynNode_IS_FRAME(node))
      *out_nested_frame = (KosDynFrame *) node;
    node = KosDynNode_PARENT(node);
  }
  if (! node || ! KosDynNode_IS_PROMPT(node)
      || kos_dynnode_id(node) != KosContinuation_LIVE_PROMPT_NODE_ID(k))
    return 0;

  return 1;
}


/**
   In-place (Path A) delimited invoke: detach the invoke span (dyn_top down
   through the live SHIFT node) onto an INPLACE DELIM inserted directly
   above the live prompt, set the resume frame's pc to the captured
   resume pc, and continue on the live chain. The region's operands are
   where capture left them: on the live prompt and the region nodes
   between it and the shift, untouched by the handler, whose own
   operands sit on the SHIFT node in the stashed span. The invocation
   prompt that stands in for the live prompt under the slice takes a
   copy of the prompt's operands, so the slice resumes over them and
   the live prompt keeps its own for the handler's return. No transfer
   plan, no clone, no wind replay; live extents unwind exactly once as
   the slice runs to POP_PROMPT, which restitches via
   kos_dyn_finish_inplace_pop_return. Borrows value.
*/
static int kos_delimited_invoke_inplace(
    KosContext *ctx,
    KosContinuation *k,
    PyObject *value,
    KosDynFrame *nested_frame) {

  KosDynNode *shift = KosContinuation_LIVE_SHIFT_NODE(k);
  KosDynNode *prompt = KosContinuation_LIVE_PROMPT_NODE(k);
  KosDynNode *span_top = KosContext_DYN_TOP(ctx);
  KosDynNode *slice_top = KosDynNode_PARENT(shift);
  KosDynFrame *resume_frame;
  KosDynDelim *delim;
  KosDynFrame *return_frame;
  KosOp *return_pc;
  int handler_nested = (nested_frame != NULL);

  /* Resume frame: the FRAME owning the SHIFT, which is what capture
     recorded RESUME_PC and RESUME_STACK against (kos_shift_capture takes
     the first frame walking down from the captured chain's top). Chain
     order root..top is caller FRAME, PROMPT, callee FRAME, SHIFT, so when
     the shift sits in a callee that frame is ABOVE the prompt and a walk
     at-or-below the prompt can never reach it -- installing the callee's
     pc and stack into the caller's frame. The readiness walk records
     that frame (nested_frame); when the shift is lexically inside the
     reset no frame intervenes, so fall back to the frame below the
     prompt. */
  resume_frame = nested_frame;
  if (! resume_frame) {
    KosDynNode *node = prompt;

    while (node && ! KosDynNode_IS_FRAME(node))
      node = KosDynNode_PARENT(node);
    if (! node) {
      PyErr_SetString(PyExc_RuntimeError,
                      "in-place invoke: no frame below prompt");
      return -1;
    }
    resume_frame = (KosDynFrame *) node;
  }

  KosContext_FlushLiveRegsToFrame(ctx);
  return_frame = KosContext_FRAME(ctx);
  return_pc = return_frame ? KosContext_PC(ctx) : NULL;

  /* Nested handler: the slice returns out of the handler's own frame and
     frees it, so restitch has nothing live to resume into and its final
     "load the invoke-site pc" step -- guarded on the live frame being
     that frame -- silently never fires. Snapshot the handler's
     environment (code, ns, operand block, pc) and resume in the copy. */
  if (handler_nested && return_frame) {
    KosDynFrame *tmpl = kos_dynframe_clone(return_frame);
    if (! tmpl) {
      if (! PyErr_Occurred())
        PyErr_NoMemory();
      return -1;
    }
    return_frame = tmpl;
  }

  delim = kos_dyndelim_new(prompt,
                           KOS_DYN_DELIM_MODE_INPLACE,
                           NULL,
                           return_pc,
                           return_frame,
                           NULL);
  if (! delim) {
    /* No delim took the holds flag, so the handler snapshot is still
       ours to free. */
    if (handler_nested)
      kos_dynframe_free(return_frame);
    if (! PyErr_Occurred())
      PyErr_NoMemory();
    return -1;
  }
  KosDynDelim_RETURN_NODE(delim) = (KosDynNode *) delim;
  /* The snapshot has no owner but this delim until the restitch hands
     it to the chain. The flag is only ever set here, on an INPLACE
     delim, which kos_dyn_clone_segment never clones, so no template
     can hold a borrowed pointer to a frame this delim will free. */
  if (handler_nested)
    KosDynNode_FLAGS((KosDynNode *) delim) |= KOS_DYNF_HOLDS_RETURN_FRAME;

  /* The invocation re-delimits: a shift firing inside the resumed slice
     must capture up to THIS invocation's prompt, not the original
     prompt, or its POP_SHIFT discards the remainder of the suspended
     handler (control semantics where shift was promised). The fresh
     instance sits between the delim and the slice; captures inside the
     slice bound at it and its completion routes through the delim. */
  KosDynPrompt *invocation_prompt = kos_dynprompt_new_invocation(
      (KosDynNode *) delim,
      KosDynPrompt_ID(prompt));
  if (! invocation_prompt) {
    KosDynNode_PARENT((KosDynNode *) delim) = NULL;
    kos_dyndelim_free(delim);
    if (! PyErr_Occurred())
      PyErr_NoMemory();
    return -1;
  }

  /* The one step that can fail comes first, before the chain is
     touched: the invocation prompt takes a copy of the region's pending
     operands from the live prompt. A failure here leaves the chain as
     it was. */
  if (kos_dyn_clone_copy_segment((KosDynNode *) invocation_prompt,
                                 prompt) < 0) {
    kos_dynprompt_free(invocation_prompt);
    KosDynNode_PARENT((KosDynNode *) delim) = NULL;
    kos_dyndelim_free(delim);
    if (! PyErr_Occurred())
      PyErr_NoMemory();
    return -1;
  }

  #if KOS_XFER_TRACE
    SAYF("[xfer] invoke inplace: span_top=%p shift=%p prompt=%p "
         "resume_frame=%p return_pc=%u",
         (void *) span_top, (void *) shift, (void *) prompt,
         (void *) resume_frame, (unsigned) kos_insn_pc(return_pc));
  #endif

  /* Detach the invoke span and stash it on the delim. Its segments'
   * unwind-cache back links die with the detach. */
  KosDynNode_PARENT(shift) = NULL;
  KosDynDelim_RETURN_CHAIN(delim) = span_top;
  kos_dyn_clear_unwind_caches(span_top, NULL);

  /* Wire prompt <- delim <- invocation prompt <- slice so the slice's
   * natural unwind leaves the invocation prompt (then the delim) at
   * dyn_top when POP_PROMPT runs. */
  if (slice_top == prompt) {
    slice_top = (KosDynNode *) invocation_prompt;
  } else {
    KosDynNode *child = slice_top;
    while (KosDynNode_PARENT(child) != prompt)
      child = KosDynNode_PARENT(child);
    KosDynNode_PARENT(child) = (KosDynNode *) invocation_prompt;
    kos_dynnode_note_parent(child);
    /* The re-spliced slice span's back links likewise predate the new
     * shape. */
    kos_dyn_clear_unwind_caches(slice_top, (KosDynNode *) invocation_prompt);
  }

  /* Point the resume frame at the captured resume pc; the invoke-site
   * pc and acting pc are saved for the restitch (slice execution
   * overwrites both). A resume the frame carries, waiting on a
   * delivery the invoke site will receive later, is displaced with
   * them: the frame runs the slice now, and the prologue would offer
   * the resume cold. */
  KosDynDelim_SAVED_PC(delim) = KosDynFrame_PC(resume_frame);
  KosDynDelim_SAVED_ACTING_PC(delim) = KosContext_ACTING_PC(ctx);
  KosDynDelim_SAVED_RESUME_KIND(delim) = KosDynFrame_RESUME_KIND(resume_frame);
  KosDynDelim_SAVED_RESUME_STATE(delim) = KosDynFrame_RESUME_STATE(resume_frame);
  KosDynFrame_RESUME_KIND(resume_frame) = NULL;
  KosDynFrame_RESUME_STATE(resume_frame) = NULL;
  KosDynFrame_PC(resume_frame) = KosContinuation_RESUME_PC(k);

  KosContinuation_LIVE_SLICE_CONSUMED(k) = 1;

  kos_dyn_commit_dyn_top(ctx, slice_top);
  kos_dyn_resync_chain(ctx);
  /* rebuild leaves live PCs alone when the frame is unchanged (invoke
   * frame == resume frame); load them from the displaced frame state. */
  kos_dyn_resync_load_regs(ctx, resume_frame);

  Py_INCREF(value);
  /* StackPUSH consumes the value reference, on failure as well. */
  return KosContext_StackPUSH(ctx, value);
}


/**
   Invoke k with value. Delimited with the capture-time reset live
   in ancestry: in-place resume (no goal). Otherwise pushes a transfer goal:
   for a delimited k the caller stays on the chain and a fresh clone of the
   template lands on a DELIM bookmark; for an undelimited k the chain is
   replaced. Callers: callable invoke, apply paths.
*/
int kos_invoke_continuation(
    KosContext *ctx,
    KosContinuation *k,
    PyObject *value,
    PyObject *continuation_ref) {

  if (! k) {
    PyErr_SetString(PyExc_RuntimeError, "NULL continuation");
    return -1;
  }

  /* In-place (Path A) invoke when the capture-time reset is still live in
     dyn_top ancestry: resume on the live chain, no graft, no wind replay.
     Only the first invoke can run in place (it consumes the live slice);
     re-invokes fall through to the PUSH graft, which instantiates the
     captured template per invoke (multi-shot, e.g. (sum (k 0) (k 1))). */
  if (KosContinuation_PROMPT_ID(k) != KOS_PROMPT_ID_UNDELIMITED
      && ! KosContinuation_LIVE_SLICE_CONSUMED(k)) {
    KosDynFrame *nested_frame = NULL;

    if (kos_delimited_inplace_ready(ctx, k, &nested_frame))
      return kos_delimited_invoke_inplace(ctx, k, value, nested_frame);
  }

  /* Delimited k(value): the caller stays on the chain and the segment
     lands on a DELIM bookmark that routes the slice's completion back
     to this invoke site. Undelimited: the whole chain is replaced. */
  int is_delimited =
      (KosContinuation_PROMPT_ID(k) != KOS_PROMPT_ID_UNDELIMITED);

  /* Undelimited: the seam is the lowest common ancestor of the live
     chain and the template, matched by node id and by phase, so
     nothing at or below it leaves or re-enters, and only the template
     above its counterpart lands; a live node that has moved on since
     the capture sits above the seam and is left and re-entered. */
  KosDynNode *seam = NULL;
  KosDynNode *tmpl_stop = NULL;
  if (! is_delimited)
    kos_chain_common_ancestor(
        ctx, KosContinuation_CAPTURED_CHAIN(k), &seam, &tmpl_stop);

  /* The landed frames sit above the invoker's frame (delimited) or
     above the seam's frame (undelimited); refuse here, before anything
     moves, so RecursionError raises at the invoke site. */
  {
    KosDynNode *base = is_delimited
        ? (KosDynNode *) KosContext_FRAME(ctx) : seam;

    while (base && ! KosDynNode_IS_FRAME(base))
      base = KosDynNode_PARENT(base);
    if (kos_eval_check_frame_room(
            ctx, (KosDynFrame *) base,
            kos_dyn_frame_count(KosContinuation_CAPTURED_CHAIN(k),
                                tmpl_stop)) < 0)
      return -1;
  }

  /* The landed segment is a fresh clone of the captured template, so
     each invoke instantiates the template anew (multi-shot, e.g.
     (sum (k 0) (k 1))); the template itself is never landed. */
  KosDynNode *target = kos_dyn_clone_segment(
      KosContinuation_CAPTURED_CHAIN(k), tmpl_stop);
  if (! target && KosContinuation_CAPTURED_CHAIN(k) != tmpl_stop) {
    if (! PyErr_Occurred())
      PyErr_SetString(PyExc_RuntimeError,
                      "continuation template clone failed without PyErr");
    return -1;
  }
  /* kos_dyn_clone_segment leaves the clone's bottom parented at
     tmpl_stop; sever it so goal_land's walk to the bottom stops at the
     clone and re-parents it to the seam, rather than following into the
     template and moving the template's own root onto the live chain. */
  if (target && tmpl_stop)
    kos_chain_detach_template(target, tmpl_stop);

  /* An INPLACE delim in the landed segment whose handler frame is not
     in the segment resumes into the live frame of that id, shared at
     or below the seam. */
  for (KosDynNode *node = target; node; node = KosDynNode_PARENT(node)) {
    if (! KosDynNode_IS_DELIM(node)
        || KosDynDelim_MODE((KosDynDelim *) node) != KOS_DYN_DELIM_MODE_INPLACE
        || (KosDynNode_FLAGS(node) & KOS_DYNF_HOLDS_RETURN_FRAME))
      continue;
    KosDynNode *want = KosDynDelim_RETURN_FRAME_NODE((KosDynDelim *) node);
    if (! want)
      continue;
    int inside = 0;
    for (KosDynNode *cand = target; cand; cand = KosDynNode_PARENT(cand)) {
      if (cand == want) {
        inside = 1;
        break;
      }
    }
    if (inside)
      continue;
    KosDynNode *live = seam;
    while (live && ! (KosDynNode_IS_FRAME(live)
                      && kos_dynnode_id(live) == kos_dynnode_id(want)))
      live = KosDynNode_PARENT(live);
    if (! live) {
      kos_dynchain_free(target, NULL);
      PyErr_SetString(PyExc_RuntimeError,
                      "continuation: the frame a resumed shift handler"
                      " returns into is no longer live");
      return -1;
    }
    KosDynDelim_RETURN_FRAME_NODE((KosDynDelim *) node) = live;
  }

  if (is_delimited) {
    KosDynDelim *ret_delim = kos_dyn_push_delim(
        ctx,
        KOS_DYN_DELIM_MODE_POP_PROMPT,
        NULL,
        KosContext_FRAME(ctx) ? KosContext_PC(ctx) : NULL,
        KosContext_FRAME(ctx),
        NULL);
    if (! ret_delim) {
      kos_dynchain_free(target, NULL);
      if (! PyErr_Occurred())
        PyErr_SetString(PyExc_RuntimeError,
                        "failed to push delimited return marker");
      return -1;
    }
    KosDynDelim_RETURN_NODE(ret_delim) = (KosDynNode *) ret_delim;
    seam = (KosDynNode *) ret_delim;
    if (KOS_XFER_TRACE)
      SAYF("[xfer] invoke: push DELIM dyn_top=%p frame_top=%p",
           (void *) KosContext_DYN_TOP(ctx),
           (void *) KosContext_FRAME(ctx));
  }

  {
    KosXferGoal *goal = kos_xfer_goal_new(
        target, seam, value, KosContinuation_RESUME_PC(k),
        continuation_ref, 0);
    if (! goal) {
      /* target is freed by kos_xfer_goal_new on failure. Undo only the
         DELIM this call pushed; an undelimited seam is a live node. */
      if (is_delimited && seam)
        (void) kos_dyn_pop_delim(ctx, (KosDynDelim *) seam);
      return -1;
    }
    if (tmpl_stop
        && kos_xfer_goal_capture_restore(goal, seam, tmpl_stop) < 0) {
      kos_xfer_goal_free(goal);
      return -1;
    }
    kos_xfer_goal_push(ctx, goal);
  }
  return 0;
}


/**
   The lowest common ancestor of the live chain and a template: the
   top of the run of node pairs, matched by id and by phase, that
   reaches both roots. NULL on both sides when the roots differ.
   Clones keep their source's id, so a landed clone of this template
   counts as the same extent. The two tops are brought to one depth
   and walked to the roots together: a mismatch clears the pair in
   hand, a match records one when none is in hand, so what remains is
   the top of the root-anchored run.
*/
void kos_chain_common_ancestor(
    KosContext *ctx,
    KosDynNode *tmpl,
    KosDynNode **out_live,
    KosDynNode **out_tmpl) {

  size_t n_live = 0;
  size_t n_tmpl = 0;
  KosDynNode *live = KosContext_DYN_TOP(ctx);
  KosDynNode *node;

  *out_live = NULL;
  *out_tmpl = NULL;
  for (node = live; node; node = KosDynNode_PARENT(node))
    n_live++;
  for (node = tmpl; node; node = KosDynNode_PARENT(node))
    n_tmpl++;

  for (; n_live > n_tmpl; n_live--)
    live = KosDynNode_PARENT(live);
  for (; n_tmpl > n_live; n_tmpl--)
    tmpl = KosDynNode_PARENT(tmpl);

  for (; live; live = KosDynNode_PARENT(live), tmpl = KosDynNode_PARENT(tmpl)) {
    if (kos_dynnode_id(live) != kos_dynnode_id(tmpl)
        || ! kos_dyn_node_same_phase(live, tmpl)) {
      *out_live = NULL;
      *out_tmpl = NULL;
    } else if (! *out_live) {
      *out_live = live;
      *out_tmpl = tmpl;
    }
  }
}


// The end.
