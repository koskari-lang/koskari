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
  Included by dynchain.c. Continuation invocation: the transfer that
  attaches a copy of a captured chain at the call site.

  :author: Christopher O'Brien <obriencj@gmail.com>
  :license: GNU General Public License v3
*/

/* === Continuation invocation === */


/**
   Invoke k with value. Pushes a transfer goal: for a delimited k the
   caller stays on the chain and a fresh clone of the template is
   attached on its dyn_top, every time; for an undelimited k the chain
   is replaced. Callers: callable invoke, apply paths.
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

  /* Delimited k(value): the caller stays on the chain and the copy is
     attached above it, returning to it as a call does. Undelimited: the
     whole chain is replaced. */
  int is_delimited = KosContinuation_DELIMITED(k);

  /* Undelimited: the seam is the lowest common ancestor of the live
     chain and the template, matched by node id and by phase, so
     nothing at or below it leaves or re-enters, and only the template
     above its counterpart is attached; a live node that has moved on since
     the capture sits above the seam and is left and re-entered. */
  KosDynNode *seam = NULL;
  KosDynNode *tmpl_stop = NULL;
  unsigned frames = KosContinuation_FRAMES(k);
  unsigned top_depth;

  if (! is_delimited)
    kos_chain_common_ancestor(
        ctx, KosContinuation_CAPTURED_CHAIN(k), &seam, &tmpl_stop, &frames);

  /* The attached frames sit above the invoker's frame (delimited) or
     above the seam's frame (undelimited); refuse here, before anything
     moves, so RecursionError raises at the invoke site. */
  {
    KosDynNode *base = is_delimited
        ? (KosDynNode *) KosContext_FRAME(ctx) : seam;

    while (base && ! KosDynNode_IS_FRAME(base))
      base = KosDynNode_PARENT(base);
    if (kos_eval_check_frame_room(ctx, (KosDynFrame *) base, frames) < 0)
      return -1;
    top_depth = (base ? KosDynFrame_DEPTH(base) : 0u) + frames;
  }

  /* The attached segment is a fresh clone of the captured template, so
     each invoke instantiates the template anew (multi-shot, e.g.
     (sum (k 0) (k 1))); the template itself is never attached. A
     delimited copy's nodes are new instances; an undelimited one's
     keep the ids the seam and the restore records match by. The
     clone's one pass also numbers the copy's frames as they stand once
     attached, links their back links, and records the split points
     ATTACHING needs with the registers live at each. */
  KosCloneShape shape;
  KosDynNode *target = kos_dyn_clone_segment(
      KosContinuation_CAPTURED_CHAIN(k), tmpl_stop, is_delimited, 1,
      top_depth, &shape);
  if (! target && KosContinuation_CAPTURED_CHAIN(k) != tmpl_stop) {
    if (! PyErr_Occurred())
      PyErr_SetString(PyExc_RuntimeError,
                      "continuation template clone failed without PyErr");
    return -1;
  }
  /* the copy's bottom sits on tmpl_stop, a template node: unlink it, so
     the copy holds nothing of the template, and ATTACHING links it to
     the seam */
  if (target)
    KosDynNode_PARENT(shape.bottom) = NULL;

  /* A delimited copy is whole frames ending in its prompt, attached on
     the caller's dyn_top: the body frame's copy returns to the caller
     as any callee does, so there is nothing to leave. */
  if (is_delimited)
    seam = KosContext_DYN_TOP(ctx);

  {
    KosXferGoal *goal = kos_xfer_goal_new(
        target, seam, value, KosContinuation_RESUME_PC(k),
        continuation_ref, 0);
    if (! goal) {
      /* target is freed by kos_xfer_goal_new on failure */
      PyMem_Free(shape.pieces);
      return -1;
    }
    goal->shape = shape;
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
   Clones keep their source's id, so an attached clone of this template
   counts as the same extent. The two tops are brought to one depth
   and walked to the roots together: a mismatch clears the pair in
   hand, a match records one when none is in hand, so what remains is
   the top of the root-anchored run. The template's frames are counted
   on the way down, and a match keeps the count of those above it.
*/
void kos_chain_common_ancestor(
    KosContext *ctx,
    KosDynNode *tmpl,
    KosDynNode **out_live,
    KosDynNode **out_tmpl,
    unsigned *out_frames) {

  size_t n_live = 0;
  size_t n_tmpl = 0;
  unsigned frames = 0;
  unsigned above = 0;
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
  for (; n_tmpl > n_live; n_tmpl--) {
    if (KosDynNode_IS_FRAME(tmpl))
      frames++;
    tmpl = KosDynNode_PARENT(tmpl);
  }

  for (; live; live = KosDynNode_PARENT(live), tmpl = KosDynNode_PARENT(tmpl)) {
    if (kos_dynnode_id(live) != kos_dynnode_id(tmpl)
        || ! kos_dyn_node_same_phase(live, tmpl)) {
      *out_live = NULL;
      *out_tmpl = NULL;
    } else if (! *out_live) {
      *out_live = live;
      *out_tmpl = tmpl;
      above = frames;
    }
    if (KosDynNode_IS_FRAME(tmpl))
      frames++;
  }
  *out_frames = *out_tmpl ? above : frames;
}


// The end.
