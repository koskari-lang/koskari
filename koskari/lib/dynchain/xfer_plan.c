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
    /* No delim took the holds flag, so the handler snapshot is still
       ours to free. */
    if (handler_nested)
      kos_dynframe_free(return_frame);
    if (resume_stack)
      KosStack_Release(resume_stack);
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

  /* The one step that can fail comes first, before the chain is
     touched: the displaced segment is stored away from its node until
     restitch, and an inline segment cannot leave its node, so detach
     spills it to a heap segment. A failure here leaves the chain as
     it was. */
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
   * state; the delim owns the displaced stack, detached above, until
   * restitch. The invoke-site acting pc is saved for extent lookups
   * after restitch (slice execution overwrites the frame's acting
   * pc). */
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
   Invoke k with value. Delimited with the capture-time reset live
   in ancestry: in-place resume (no goal). Otherwise arms a transfer goal:
   for a delimited k the caller stays on the chain and a fresh clone of the
   template lands on a DELIM bookmark; for an undelimited k the chain is
   replaced. Callers: callable invoke, apply paths.
*/
int kos_invoke_continuation(
    KosEvalState *es,
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

    if (kos_delimited_inplace_ready(es, k, &nested_frame))
      return kos_delimited_invoke_inplace(es, k, value, nested_frame);
  }

  /* Delimited k(value): the caller stays on the chain and the segment
     lands on a DELIM bookmark that routes the slice's completion back
     to this invoke site. Undelimited: the whole chain is replaced. */
  int is_delimited =
      (KosContinuation_PROMPT_ID(k) != KOS_PROMPT_ID_UNDELIMITED);

  /* Undelimited: the seam is the lowest common ancestor of the live
     chain and the template, so nothing at or below it leaves or
     re-enters, and only the template above its counterpart lands. */
  KosDynNode *seam = NULL;
  KosDynNode *tmpl_stop = NULL;
  if (! is_delimited
      && kos_chain_common_ancestor(
          es, KosContinuation_CAPTURED_CHAIN(k), &seam, &tmpl_stop) < 0)
    return -1;

  /* The landed frames sit above the invoker's frame (delimited) or
     above the seam's frame (undelimited); refuse here, before anything
     moves, so RecursionError raises at the invoke site. */
  {
    KosDynNode *base = is_delimited
        ? (KosDynNode *) KosEvalState_FRAME(es) : seam;

    while (base && ! KosDynNode_IS_FRAME(base))
      base = KosDynNode_PARENT(base);
    if (kos_eval_check_frame_room(
            es, (KosDynFrame *) base,
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
        target, seam, value, KosContinuation_RESUME_PC(k),
        continuation_ref, 0);
    if (! goal) {
      /* target is freed by kos_xfer_goal_new on failure. Undo only the
         DELIM this call pushed; an undelimited seam is a live node. */
      if (is_delimited && seam)
        (void) kos_dyn_pop_delim(es, (KosDynDelim *) seam);
      return -1;
    }
    if (tmpl_stop
        && kos_xfer_goal_capture_restore(goal, seam, tmpl_stop) < 0) {
      kos_xfer_goal_free(goal);
      return -1;
    }
    kos_xfer_goal_arm(es, goal);
  }
  return 0;
}


/**
   The lowest common ancestor of the live chain and a template: walking
   both from their roots, the last pair of nodes with the same id. NULL
   on both sides when the roots differ. Clones keep their source's id,
   so a landed clone of this template counts as the same extent.

   :return: 0, or -1 with PyErr set
*/
int kos_chain_common_ancestor(
    KosEvalState *es,
    KosDynNode *tmpl,
    KosDynNode **out_live,
    KosDynNode **out_tmpl) {

  size_t n_live = 0;
  size_t n_tmpl = 0;
  KosDynNode *node;
  KosDynNode **live_arr;
  KosDynNode **tmpl_arr;
  size_t i;

  *out_live = NULL;
  *out_tmpl = NULL;
  for (node = KosEvalState_DYN_TOP(es); node; node = KosDynNode_PARENT(node))
    n_live++;
  for (node = tmpl; node; node = KosDynNode_PARENT(node))
    n_tmpl++;
  if (! n_live || ! n_tmpl)
    return 0;

  live_arr = PyMem_Malloc((n_live + n_tmpl) * sizeof(KosDynNode *));
  if (! live_arr) {
    PyErr_NoMemory();
    return -1;
  }
  tmpl_arr = live_arr + n_live;

  /* root first */
  i = n_live;
  for (node = KosEvalState_DYN_TOP(es); node; node = KosDynNode_PARENT(node))
    live_arr[--i] = node;
  i = n_tmpl;
  for (node = tmpl; node; node = KosDynNode_PARENT(node))
    tmpl_arr[--i] = node;

  for (i = 0; i < n_live && i < n_tmpl; i++) {
    if (kos_dynnode_id(live_arr[i]) != kos_dynnode_id(tmpl_arr[i]))
      break;
    *out_live = live_arr[i];
    *out_tmpl = tmpl_arr[i];
  }

  PyMem_Free(live_arr);
  return 0;
}


// The end.
