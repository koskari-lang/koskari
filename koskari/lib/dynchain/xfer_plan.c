/* Included by dynchain.c -- continuation invocation */
/* === Continuation invocation === */


/**
   Walk segment top to bottom; set stop to parent of bottom (clone bound).
*/
KOS_OP_INLINE void kos_chain_segment_bounds(
    KosDynNode *top,
    KosDynNode **bottom_out,
    KosDynNode **stop_out) {

  KosDynNode *bottom = top;

  while (bottom && KosDynNode_PARENT(bottom))
    bottom = KosDynNode_PARENT(bottom);
  if (bottom_out)
    *bottom_out = bottom;
  if (stop_out)
    *stop_out = bottom ? KosDynNode_PARENT(bottom) : NULL;
}


/**
   In-place (Path A) invoke readiness: the capture-time live prompt and
   shift nodes are both still on the current chain (pointer + node_id
   match), the shift node lies above the prompt, no barrier node (foreign
   DELIM, GEN, NEXT, PROMPT) sits between dyn_top and the shift node,
   and no transfer goal is armed. Returns 1 when in-place invoke
   applies, 0 otherwise (fall back to the PUSH path).

   On success, *out_nested_frame is the first FRAME between the SHIFT and
   its prompt (the nested handler's frame), or NULL when the handler
   shares the prompt's frame -- recorded here so the invoke does not
   re-walk the same stretch.
*/
static int kos_delimited_inplace_ready(
    KosEvalState *es,
    KosContinuation *k,
    KosDynFrame **out_nested_frame) {

  KosDynNode *prompt = KosContinuation_LIVE_PROMPT_NODE(k);
  KosDynNode *shift = KosContinuation_LIVE_SHIFT_NODE(k);
  KosDynNode *node;

  if (! prompt || ! shift)
    return 0;
  if (KosEvalState_GOAL(es))
    return 0;

  node = KosEvalState_DYN_TOP(es);
  while (node && node != shift) {
    switch (KosDynNode_KIND(node)) {
      case KOS_DYN_DELIM:
      case KOS_DYN_GEN:
      case KOS_DYN_NEXT:
      case KOS_DYN_PROMPT:
        return 0;
      default:
        break;
    }
    node = KosDynNode_PARENT(node);
  }
  if (! node || ! KosDynNode_IS_SHIFT(node)
      || KosDynNode_NODE_ID(node) != KosContinuation_LIVE_SHIFT_NODE_ID(k))
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
      || KosDynNode_NODE_ID(node) != KosContinuation_LIVE_PROMPT_NODE_ID(k))
    return 0;

  return 1;
}


/**
   In-place (Path A) delimited invoke: detach the invoke span (dyn_top down
   through the live SHIFT node) onto an INPLACE DELIM inserted directly
   above the live prompt, displace the resume frame's stack/pc with the
   captured resume state, and continue on the live chain. No transfer plan,
   no clone, no wind replay; live extents unwind exactly once as the slice
   runs to POP_PROMPT, which restitches via
   kos_dyn_finish_inplace_pop_return. Borrows value.
*/
static int kos_delimited_invoke_inplace(
    KosEvalState *es,
    KosContinuation *k,
    PyObject *value,
    KosDynFrame *nested_frame) {

  KosDynNode *shift = KosContinuation_LIVE_SHIFT_NODE(k);
  KosDynNode *prompt = KosContinuation_LIVE_PROMPT_NODE(k);
  KosDynNode *span_top = KosEvalState_DYN_TOP(es);
  KosDynNode *slice_top = KosDynNode_PARENT(shift);
  KosDynFrame *resume_frame;
  KosSegment *resume_stack;
  KosDynDelim *delim;
  KosDynFrame *return_frame;
  uint32_t return_pc;
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

  resume_stack = KosContinuation_RESUME_STACK(k)
      ? KosStack_DeepCopy(KosContinuation_RESUME_STACK(k)) : NULL;
  if (! resume_stack && KosContinuation_RESUME_STACK(k)) {
    if (! PyErr_Occurred())
      PyErr_NoMemory();
    return -1;
  }

  KosEvalState_FlushLiveRegsToFrame(es);
  return_frame = KosEvalState_FRAME(es);
  return_pc = return_frame ? KosEvalState_PC(es) : (uint32_t) -1;

  /* Nested handler: the slice returns out of the handler's own frame and
     frees it, so restitch has nothing live to resume into and its final
     "load the invoke-site pc" step -- guarded on the live frame being
     that frame -- silently never fires. Snapshot the handler's
     environment (code, ns, operand block, pc) and resume in the copy. */
  if (handler_nested && return_frame) {
    KosDynFrame *tmpl = kos_dynframe_clone(return_frame);
    if (! tmpl) {
      if (resume_stack)
        KosStack_Release(resume_stack);
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
    if (resume_stack)
      KosStack_Release(resume_stack);
    if (! PyErr_Occurred())
      PyErr_NoMemory();
    return -1;
  }
  KosDynDelim_RETURN_NODE(delim) = (KosDynNode *) delim;
  if (handler_nested)
    KosDynNode_FLAGS((KosDynNode *) delim) |= KOS_DYNF_HOLDS_RETURN_FRAME;

  /* The invocation re-delimits: a shift firing inside the resumed slice
     must capture up to THIS invocation's iteration node, not the original
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
    if (resume_stack)
      KosStack_Release(resume_stack);
    if (! PyErr_Occurred())
      PyErr_NoMemory();
    return -1;
  }

  #if KOS_XFER_TRACE
    SAYF("[xfer] invoke inplace: span_top=%p shift=%p prompt=%p "
         "resume_frame=%p return_pc=%u",
         (void *) span_top, (void *) shift, (void *) prompt,
         (void *) resume_frame, (unsigned) return_pc);
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
    /* The re-spliced slice span's back links likewise predate the new
     * shape. */
    kos_dyn_clear_unwind_caches(slice_top, (KosDynNode *) invocation_prompt);
  }

  /* Displace the resume frame's live state with the captured resume
   * state; the delim owns the displaced stack until restitch. The
   * invoke-site acting pc is saved for extent lookups after restitch
   * (slice execution overwrites the frame's acting pc). */
  /* The displaced segment is stored away from its node until restitch;
     an inline segment cannot leave its node, so detach spills it to a
     heap segment first. */
  {
    KosSegment *displaced = KosStack_Detach(KosDynFrame_STACK(resume_frame));
    if (! displaced && KosDynFrame_STACK(resume_frame)) {
      if (resume_stack)
        KosStack_Release(resume_stack);
      return -1;
    }
    KosDynDelim_SAVED_STACK(delim) = displaced;
    if (displaced)
      KosSegment_BACK(displaced) = NULL;
  }
  KosDynDelim_SAVED_PC(delim) = KosDynFrame_PC(resume_frame);
  KosDynDelim_SAVED_ACTING_PC(delim) = KosEvalState_ACTING_PC(es);
  KosDynFrame_STACK(resume_frame) = resume_stack;
  KosDynFrame_PC(resume_frame) = KosContinuation_RESUME_PC(k);

  KosContinuation_LIVE_SLICE_CONSUMED(k) = 1;

  kos_dyn_commit_dyn_top(es, slice_top);
  kos_dyn_resync_chain(es);
  /* rebuild leaves live PCs alone when the frame is unchanged (invoke
   * frame == resume frame); load them from the displaced frame state. */
  kos_dyn_resync_load_regs(es, resume_frame);

  Py_INCREF(value);
  /* StackPUSH consumes the value reference, on failure as well. */
  return KosEvalState_StackPUSH(es, value);
}


/**
   Invoke k with value or throw. Delimited with the capture-time reset live
   in ancestry: in-place resume (no goal). Otherwise arms a transfer goal:
   for a delimited k the caller stays on the chain and a fresh clone of the
   template lands on a DELIM bookmark; for an undelimited k the chain is
   replaced. Callers: callable invoke, apply paths.
*/
int kos_invoke_continuation(
    KosEvalState *es,
    KosContinuation *k,
    PyObject *value,
    int is_throw,
    PyObject *continuation_ref) {

  if (! k) {
    PyErr_SetString(PyExc_RuntimeError, "NULL continuation");
    return -1;
  }

  /* In-place (Path A) invoke when the capture-time reset is still live in
     dyn_top ancestry: resume on the live chain, no graft, no wind replay.
     Only the first invoke can run in place (it consumes the live slice);
     re-invokes fall through to the PUSH graft, which instantiates the
     captured template per invoke (multi-shot, e.g. (sum (k 0) (k 1))).
     Throw injection also stays on the PUSH path. */
  if (KosContinuation_PROMPT_ID(k) != KOS_PROMPT_ID_UNDELIMITED
      && ! is_throw
      && ! KosContinuation_LIVE_SLICE_CONSUMED(k)) {
    KosDynFrame *nested_frame = NULL;

    if (kos_delimited_inplace_ready(es, k, &nested_frame))
      return kos_delimited_invoke_inplace(es, k, value, nested_frame);
  }

  /* The landed segment is a fresh clone of the captured template, so
     each invoke instantiates the template anew (multi-shot, e.g.
     (sum (k 0) (k 1))); the template itself is never landed. */
  KosDynNode *target = kos_dyn_clone_segment(
      KosContinuation_CAPTURED_CHAIN(k), NULL);
  if (! target && KosContinuation_CAPTURED_CHAIN(k)) {
    if (! PyErr_Occurred())
      PyErr_SetString(PyExc_RuntimeError,
                      "continuation template clone failed without PyErr");
    return -1;
  }

  /* Delimited k(value): the caller stays on the chain and the segment
     lands on a DELIM bookmark that routes the slice's completion back
     to this invoke site. Undelimited: the whole chain is replaced. */
  int is_delimited = (KosContinuation_PROMPT_ID(k) != KOS_PROMPT_ID_UNDELIMITED);
  KosDynNode *seam = NULL;
  if (is_delimited) {
    KosDynDelim *ret_delim = kos_dyn_push_delim(
        es,
        KOS_DYN_DELIM_MODE_POP_PROMPT,
        NULL,
        KosEvalState_FRAME(es)
            ? KosEvalState_PC(es) : (uint32_t) -1,
        KosEvalState_FRAME(es),
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
           (void *) KosEvalState_DYN_TOP(es),
           (void *) KosEvalState_FRAME(es));
  }

  {
    KosXferGoal *goal = kos_xfer_goal_new(
        target, seam, value, is_throw, KosContinuation_RESUME_PC(k),
        continuation_ref, 0);
    if (! goal) {
      if (seam && kos_dyn_pop_delim(es, (KosDynDelim *) seam) < 0)
        return -1;
      return -1;
    }
    kos_xfer_goal_arm(es, goal);
  }
  return 0;
}
