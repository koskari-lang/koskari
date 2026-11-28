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
   koskari/lib/xfer_goal.c

   The transfer goal driver. See xfer_goal.h.

   :author: Christopher O'Brien <obriencj@gmail.com>
   :license: GNU General Public License v3
*/


#include "xfer_goal.h"
#include "debug.h"
#include "tracing.h"


/* === Lifecycle === */


void kos_xfer_goal_init(
    KosXferGoal *goal,
    KosDynNode *target,
    KosDynNode *seam,
    PyObject *payload,
    KosOp *resume_pc,
    PyObject *continuation_ref,
    int is_cc_unwind) {

  KOS_TRACE_EVENTS_INC(KOS_TE_TRANSFER_GOAL_NEW);
  goal->outer = NULL;
  goal->target = target;
  goal->seam = seam;
  goal->seam_id = seam ? kos_dynnode_id(seam) : 0;
  goal->frontier = NULL;
  goal->frontier_id = 0;
  /* with a target the shape is the caller's to set; without one the
     free reads pieces, and a winding goal the top its caller sets */
  if (! target)
    goal->shape.pieces = NULL;
  goal->piece_next = 0;
  goal->piece_entered = 0;
  goal->keep = NULL;
  goal->keep_tail = NULL;
  goal->payload = Py_XNewRef(payload);
  goal->continuation_ref = Py_XNewRef(continuation_ref);
  goal->resume_pc = resume_pc;
  goal->restore = NULL;
  goal->restore_count = 0;
  goal->phase = KOS_GOAL_LEAVING;
  goal->is_cc_unwind = (uint8_t) (is_cc_unwind != 0);
  goal->voided_outer = 0;
  goal->in_place = 0;
}


KosXferGoal *kos_xfer_goal_new(
    KosDynNode *target,
    KosDynNode *seam,
    PyObject *payload,
    KosOp *resume_pc,
    PyObject *continuation_ref,
    int is_cc_unwind) {

  KosXferGoal *goal = PyMem_Malloc(sizeof(KosXferGoal));

  if (! goal) {
    if (target)
      kos_dynchain_free(target, NULL);
    PyErr_NoMemory();
    return NULL;
  }

  kos_xfer_goal_init(
      goal, target, seam, payload, resume_pc, continuation_ref,
      is_cc_unwind);
  return goal;
}


void kos_xfer_goal_free(KosXferGoal *goal) {

  if (! goal)
    return;

  /* Detach first, free second: freeing a node or a stack runs Python,
     and the goal's traverse reads target and every restore entry, so a
     collection from inside a release must find nothing it has freed.
     Callers unlink the goal before calling here; the order holds on
     its own too. */
  if (goal->target) {
    KosDynNode *target = goal->target;

    goal->target = NULL;
    kos_dynchain_free(target, NULL);
  }
  Py_CLEAR(goal->payload);
  if (goal->restore) {
    for (size_t i = 0; i < goal->restore_count; i++) {
      KosXferRestore *r = &goal->restore[i];
      KosSegment *stack = r->stack;
      const KosResumeKind *kind = r->resume_kind;
      void *state = r->resume_state;

      r->stack = NULL;
      r->resume_kind = NULL;
      r->resume_state = NULL;
      if (stack)
        KosStack_Release(stack);
      /* a cloned resume state never applied gets its kind's clear */
      if (kind && kind->clear)
        kind->clear(state);
    }
    PyMem_Free(goal->restore);
    goal->restore = NULL;
    goal->restore_count = 0;
  }
  /* the split points borrow nodes of the target or the chain */
  PyMem_Free(goal->shape.pieces);
  goal->shape.pieces = NULL;
  /* The reference goes last: a winding goal is storage of the
     generator it names, which this may free. A goal run in place is
     its caller's storage. */
  {
    PyObject *ref = goal->continuation_ref;

    goal->continuation_ref = NULL;
    if (! goal->in_place)
      PyMem_Free(goal);
    Py_XDECREF(ref);
  }
}


int kos_xfer_goal_capture_restore(
    KosXferGoal *goal,
    KosDynNode *live_seam,
    KosDynNode *tmpl_seam) {

  KosDynNode *live;
  KosDynNode *tmpl;
  size_t count = 0;
  KosXferRestore *recs;

  /* The run is the seam down to and including its frame. */
  for (live = live_seam; live; live = KosDynNode_PARENT(live)) {
    count++;
    if (KosDynNode_IS_FRAME(live))
      break;
  }
  if (! live) {
    PyErr_SetString(PyExc_SystemError,
        "transfer: no frame at or below the seam");
    return -1;
  }

  recs = PyMem_Calloc(count, sizeof(KosXferRestore));
  if (! recs) {
    PyErr_NoMemory();
    return -1;
  }

  live = live_seam;
  tmpl = tmpl_seam;
  for (size_t i = 0; i < count; i++) {
    KosXferRestore *r = &recs[i];

    if (! tmpl || kos_dynnode_id(tmpl) != kos_dynnode_id(live)) {
      PyErr_SetString(PyExc_SystemError,
          "transfer: the template does not mirror the chain"
          " below the seam");
      goto fail;
    }
    r->node = live;
    r->node_id = kos_dynnode_id(live);
    if (KosDynNode_STACK(tmpl)) {
      r->stack = KosStack_DeepCopy(KosDynNode_STACK(tmpl));
      if (! r->stack)
        goto fail;
    }
    if (KosDynNode_IS_FRAME(live)) {
      const KosResumeKind *kind = KosDynFrame_RESUME_KIND(tmpl);

      r->pc = KosDynFrame_PC(tmpl);
      r->acting_pc = KosDynFrame_ACTING_PC(tmpl);
      /* The template keeps its state for the next application: the record
         takes the kind's clone. The capture that built the template
         refused any kind without one; a template reaching here with
         one is refused the same way rather than called through. */
      if (kind) {
        if (unlikely(! kind->clone)) {
          PyErr_Format(PyExc_SystemError,
              "transfer: a template resume '%s' that cannot be cloned",
              kind->name ? kind->name : "?");
          goto fail;
        }
        r->resume_kind = kind;
        r->resume_state = kind->clone(KosDynFrame_RESUME_STATE(tmpl));
      }
    }
    live = KosDynNode_PARENT(live);
    tmpl = KosDynNode_PARENT(tmpl);
  }

  goal->restore = recs;
  goal->restore_count = count;
  return 0;

fail:
  for (size_t i = 0; i < count; i++) {
    if (recs[i].stack)
      KosStack_Release(recs[i].stack);
    if (recs[i].resume_kind && recs[i].resume_kind->clear)
      recs[i].resume_kind->clear(recs[i].resume_state);
  }
  PyMem_Free(recs);
  return -1;
}


/**
   Put the captured operands back on the nodes below the seam. The
   node's segment slot keeps its address, so every unwind cache aimed at
   it stays valid; the copy inherits the displaced segment's back link.
   An inline segment cannot leave its node: it is emptied and left
   dormant, as a spill leaves it.
*/
static int goal_apply_restore(KosContext *ctx, KosXferGoal *goal) {

  for (size_t i = 0; i < goal->restore_count; i++) {
    KosXferRestore *r = &goal->restore[i];
    KosDynNode *node = r->node;

#if KOS_EVAL_SAFETYNET
    if (kos_dynnode_id(node) != r->node_id) {
      PyErr_SetString(PyExc_SystemError,
          "transfer: a restored node left the chain");
      return -1;
    }
#endif
    if (r->stack) {
      KosSegment *old = KosDynNode_STACK(node);

      KosSegment_BACK(r->stack) = old ? KosSegment_BACK(old) : NULL;
      if (old) {
        KosStack_Clear(&KosDynNode_STACK(node));
        if (! KosSegment_IS_INLINE(old))
          KosStack_Release(old);
      }
      KosDynNode_STACK(node) = r->stack;
      r->stack = NULL;
    }
    if (KosDynNode_IS_FRAME(node)) {
      KosDynFrame_PC(node) = r->pc;
      KosDynFrame_ACTING_PC(node) = r->acting_pc;
      /* the resume the live frame held gets no delivery; the record's
         clone is handed over, so the goal no longer owns it */
      kos_dynframe_clear_resume((KosDynFrame *) node);
      KosDynFrame_RESUME_KIND(node) = r->resume_kind;
      KosDynFrame_RESUME_STATE(node) = r->resume_state;
      r->resume_kind = NULL;
      r->resume_state = NULL;
      /* The live registers are the authority for the current frame
         and are flushed back into it whenever the frame changes; a
         restore of that frame must go into them too. */
      if ((KosDynFrame *) node == KosContext_FRAME(ctx)) {
        KosContext_SET_PC(ctx, r->pc);
        ctx->acting_pc = r->acting_pc;
      }
    }
  }
  return 0;
}


void kos_xfer_goal_push(KosContext *ctx, KosXferGoal *goal) {

  goal->outer = KosContext_GOAL(ctx);
  KosContext_GOAL(ctx) = goal;
  /* an outer whose LEAVING voided outers behind it and then waited has
     not reached ATTACHING to retire them: this goal's ATTACHING retires
     them too */
  if (goal->outer && goal->outer->voided_outer)
    goal->voided_outer = 1;
}


/**
   Retire the active goal, making its outer (if any) active.
*/
static void kos_xfer_goal_pop(KosContext *ctx) {

  KosXferGoal *goal = KosContext_GOAL(ctx);

  KosContext_GOAL(ctx) = goal->outer;
  goal->outer = NULL;
  kos_xfer_goal_free(goal);
}


/**
   kos_xfer_goal_pop for the callers off a transfer's own path, so the
   one on it, the delivery, keeps the pop folded in.
*/
static KOS_NOINLINE void kos_xfer_goal_pop_cold(KosContext *ctx) {
  kos_xfer_goal_pop(ctx);
}


/**
   A winding goal that is given up: the nodes its generator's lists
   named went with the target, or are on the chain for the raise to
   unwind, so the lists are emptied. The generator is alive:
   continuation_ref holds it.
*/
static void goal_winding_died(KosXferGoal *goal) {

  if (goal->phase >= KOS_GOAL_WINDING_OUT)
    kos_dyn_gen_wind_reset(goal->gen_state);
}


void kos_xfer_goal_discard(KosXferGoal *goal) {

  if (goal)
    goal_winding_died(goal);
  kos_xfer_goal_free(goal);
}


/**
   Give up the active goal: kos_xfer_goal_pop for one that did not run
   to its end. A winding in pushed with no frame live leaves the
   registers to be read from the chain.
*/
static void kos_xfer_goal_pop_dead(KosContext *ctx) {

  KosXferGoal *goal = KosContext_GOAL(ctx);

  goal_winding_died(goal);
  if (goal->phase == KOS_GOAL_WINDING_IN) {
    if (! KosContext_FRAME(ctx) && KosContext_DYN_TOP(ctx))
      kos_dyn_resync_chain(ctx);
    if (KosContext_SuspensionEvent(ctx) == KOS_SUSP_WIND)
      KosContext_ClearSuspensionEvent(ctx);
  }
  kos_xfer_goal_pop_cold(ctx);
}


void kos_xfer_goal_abandon_all(KosContext *ctx) {

  while (KosContext_GOAL(ctx))
    kos_xfer_goal_pop_dead(ctx);
}


/**
   A winding in given up by the error pending: when its step carried a
   throw, the error is raised from the thrown exception, as `raise
   error from thrown` would, so the throw is not lost with the goal.
   No link when the error is the thrown exception itself, or already
   names a cause or suppresses its context: an explicit `from`,
   `from None` included, stands.
*/
static KOS_NOINLINE void goal_wind_in_failed(KosXferGoal *goal) {

  PyObject *err;
  PyObject *cause;

  if (goal->phase != KOS_GOAL_WINDING_IN || ! goal->is_throw
      || ! goal->payload || ! PyErr_Occurred())
    return;

  err = PyErr_GetRaisedException();
  cause = PyException_GetCause(err);
  if (err != goal->payload && ! cause
      && ! ((PyBaseExceptionObject *) err)->suppress_context)
    PyException_SetCause(err, Py_NewRef(goal->payload));
  Py_XDECREF(cause);
  PyErr_SetRaisedException(err);
}


void kos_xfer_goal_abandon_stepping(KosContext *ctx, int failure) {

  KosXferGoal *goal;

  /* A waiter whose node is dyn_top has no thunk above it any more: the
     call returned or failed, or what it waits on is an awaitable, so
     the raise is its thunk's. Every outer of a waiting goal waits on a
     node further down. */
  while ((goal = KosContext_GOAL(ctx))
         && (! goal->frontier || goal->frontier == KosContext_DYN_TOP(ctx))) {
    if (failure)
      goal_wind_in_failed(goal);
    kos_xfer_goal_pop_dead(ctx);
  }
}


void kos_xfer_goal_thunk_died(
    KosContext *ctx,
    const KosDynNode *node,
    uint32_t node_id) {

  KosXferGoal *goal;

  for (goal = KosContext_GOAL(ctx); goal; goal = goal->outer) {
    if (goal->frontier == node && goal->frontier_id == node_id)
      break;
  }
  if (! goal)
    return;

  /* the goals started inside the thunk, then its waiter */
  while (KosContext_GOAL(ctx) != goal)
    kos_xfer_goal_pop_dead(ctx);
  goal_wind_in_failed(goal);
  kos_xfer_goal_pop_dead(ctx);

  /* the outers its LEAVING voided: their nodes left the chain, and the
     ATTACHING that would have retired them will not run */
  while (KosContext_GOAL(ctx) && ! KosContext_GOAL(ctx)->frontier)
    kos_xfer_goal_pop_dead(ctx);
}


/* === Waits === */


/**
   Make the goal wait on node: its thunk or awaitable is in flight. The
   thunk's frame installs directly above node -- LEAVING fires it with
   node at dyn_top, and ATTACHING puts the node ENTERING runs at dyn_top
   -- so the driver does nothing until dyn_top is node again.

   The node carries KOS_DYNF_GOAL_WAIT for as long as the wait lasts.
   The one event that voids a waiting goal is a later goal's LEAVING
   unlinking this node; the mark lets that LEAVING see it as it happens
   (goal_void_outers_on), so ATTACHING never walks the chain to find
   out which outers are still live.

   No two goals wait on one node. A LEAVING waits on the node at
   dyn_top whose leave fired its thunk; a later goal's LEAVING that
   reaches the same node has already unlinked the thunk's frame above
   it, so the wind or with is aborted without firing again and that
   goal unlinks the node rather than waiting on it. An ENTERING waits
   on a node of its own attached target, a fresh clone no other goal's
   ENTERING runs, since each ENTERING runs only the nodes its own
   ATTACHING put on the chain. The clear when the wait ends therefore
   drops no other goal's mark. A mark left behind by a goal freed while
   it waited (a failed continuation invoke abandons every goal) only
   makes a later LEAVING, or the raise walk, look through the goals for
   a waiter that is not there.
*/
static void goal_wait(KosXferGoal *goal, KosDynNode *node) {

  goal->frontier = node;
  goal->frontier_id = kos_dynnode_id(node);
  KosDynNode_FLAGS(node) |= KOS_DYNF_GOAL_WAIT;
}


/**
   The wait is over and the frontier node is still on the chain: take
   the mark off it and forget it.
*/
static void goal_end_wait(KosXferGoal *goal) {

  KosDynNode_FLAGS(goal->frontier) &= ~KOS_DYNF_GOAL_WAIT;
  goal->frontier = NULL;
  goal->frontier_id = 0;
}


/**
   LEAVING unlinked node, which carried KOS_DYNF_GOAL_WAIT: every outer
   that waited on it (by address and id, read before the unlink freed
   it; the node is never dereferenced here) is void. Its frontier is
   dropped, which is how ATTACHING recognizes it, and the goal notes
   that its ATTACHING has outers to retire. Proportional to the outers,
   and only for a marked node.
*/
static void goal_void_outers_on(
    KosXferGoal *goal,
    const KosDynNode *node,
    uint32_t node_id) {

  for (KosXferGoal *outer = goal->outer; outer; outer = outer->outer) {
    if (outer->frontier != node || outer->frontier_id != node_id)
      continue;
    outer->frontier = NULL;
    outer->frontier_id = 0;
    goal->voided_outer = 1;
  }
}


int kos_xfer_goal_will_drive(KosContext *ctx) {

  KosXferGoal *goal = KosContext_GOAL(ctx);

  if (! goal)
    return 0;
  return ! goal->frontier || KosContext_DYN_TOP(ctx) == goal->frontier;
}


int kos_xfer_goal_waits_on(KosContext *ctx, const KosDynNode *node) {

  KosXferGoal *goal = KosContext_GOAL(ctx);

  return goal && goal->frontier == node;
}


int kos_xfer_goal_traverse(KosXferGoal *goal, visitproc visit, void *arg) {

  Py_VISIT(goal->payload);
  Py_VISIT(goal->continuation_ref);
  for (size_t i = 0; i < goal->restore_count; i++) {
    int r = KosStack_Traverse(goal->restore[i].stack, visit, arg);
    if (r)
      return r;
    r = kos_resume_traverse(goal->restore[i].resume_kind,
                            goal->restore[i].resume_state, visit, arg);
    if (r)
      return r;
  }
  if (goal->target)
    return kos_dynchain_traverse(goal->target, NULL, visit, arg);
  return 0;
}


/* === Phases === */


/**
   Link a node LEAVING kept into the goal's continuation: under the
   node kept before it, or as the template's top, and onto the
   template's bottom.
*/
KOS_OP_INLINE void goal_keep_node(KosXferGoal *goal, KosDynNode *node) {

  KosContinuation *k = goal->keep;
  KosDynNode *tail = goal->keep_tail;

  /* The kept nodes keep the skip links among them. One that named the
     prompt the abort stops at names the template's copy of it, the
     node every kept node goes onto. */
  if (KosDynNode_SKIP(node) == goal->seam)
    KosDynNode_SKIP(node) = kos_dynnode_skip_under(
        tail ? KosDynNode_PARENT(tail) : KosContinuation_CAPTURED_CHAIN(k));

  if (tail) {
    KosDynNode_PARENT(node) = KosDynNode_PARENT(tail);
    KosDynNode_PARENT(tail) = node;
    kos_dynnode_note_parent(tail);
  } else {
    KosDynNode_PARENT(node) = KosContinuation_CAPTURED_CHAIN(k);
    KosContinuation_CAPTURED_CHAIN(k) = node;
  }
  kos_dynnode_note_parent(node);
  goal->keep_tail = node;
  if (KosDynNode_IS_FRAME(node))
    KosContinuation_FRAMES(k)++;
}


/**
   LEAVING: unlink from dyn_top down to the seam, every node through the
   same unlink. What leaving a node does is the node's own: a flattened
   generator's span boundary finishes its generator as it goes, and the
   root of a generator's own context refuses, being the driver
   boundary, which only a cc-unwind reaches, as its seam.

   A goal with a continuation to fill, a shift's abort, keeps each node
   instead of freeing it (kos_dyn_unlink_top_keep). The nodes come off
   top first, so each goes under the one kept before it and onto the
   template's bottom, and the template is a whole chain after every
   step. The capture checked every node down to the seam before the
   transfer began.
*/
static KosOpResult goal_leave(KosContext *ctx, KosXferGoal *goal) {

  while (KosContext_DYN_TOP(ctx) != goal->seam) {
    KosDynNode *top = KosContext_DYN_TOP(ctx);
    KosDynNode *kept = NULL;
    uint32_t waited_id;
    KosOpResult r;

    if (! top) {
      PyErr_SetString(PyExc_SystemError,
          "transfer: the seam is not on the chain");
      return EVAL_RAISE();
    }

    /* A node with no skip bit runs nothing and no goal waits on it:
       it is freed or kept in one step. */
    if (! (KosDynNode_CAPS(top) & KOS_DYN_CAP_SKIP)) {
      if (goal->keep) {
        kos_dyn_keep_top(ctx, top);
        goal_keep_node(goal, top);
      } else {
        kos_dyn_release_top(ctx, top);
      }
      continue;
    }

    /* an outer waits on this node: its id is read now, as the unlink
       frees the node, or takes the mark off one it keeps */
    waited_id = (KosDynNode_FLAGS(top) & KOS_DYNF_GOAL_WAIT)
        ? kos_dynnode_id(top) : 0;
    r = goal->keep
        ? kos_dyn_unlink_top_keep(ctx, &kept)
        : kos_dyn_unlink_top(ctx, KOS_XFER_EVENT_TRANSFER, 1);
    if (r.kind == KOS_RES_NORMAL) {
      if (kept)
        goal_keep_node(goal, kept);
      if (waited_id)
        goal_void_outers_on(goal, top, waited_id);
      continue;
    }
    if (r.kind == KOS_RES_REPROLOGUE || r.kind == KOS_RES_AWAIT)
      goal_wait(goal, top);
    return r;
  }

#if KOS_EVAL_SAFETYNET
  /* The seam is at dyn_top, so it is read as a live node: one that
     took the address of the node the goal recorded carries another
     id. A seam that is not on the chain at all is the loop's own
     error above, with or without the safety net. */
  if (goal->seam && kos_dynnode_id(goal->seam) != goal->seam_id) {
    PyErr_SetString(PyExc_SystemError,
        "transfer: the seam is not the node the goal recorded");
    return EVAL_RAISE();
  }
#endif
  return EVAL_NORMAL();
}


/**
   ATTACHING: splice the next piece of the target onto the chain. A
   transfer re-enters the target outermost first, and each node it runs
   (KOS_DYN_CAP_WINDS) runs its enter with only the nodes outside it on
   the chain, as dynamic-wind runs a before thunk in the extent of the
   dynamic-wind itself. So a piece is the rest of the target from its
   bottom up to and including the next split point, or the whole rest
   when no split point is left; what rests on the split point stays in
   goal->target, and ENTERING runs the split point before the next
   piece goes on. The first piece goes onto the seam, each later one
   onto the split point the previous piece ended at, which is dyn_top.
   The piece that uses the target up installs the resume pc.

   The clone that made the target numbered its frames, linked its back
   links and recorded each split point with the registers live there
   (KosCloneShape). The first ATTACHING completes those from the
   registers live at the seam; after that a piece is a relink and a
   register load, and nothing walks the chain.

   Infallible but for the restore. An outer goal whose frontier LEAVING
   unlinked is void: the node it waited on and everything above it are
   gone, and the extent it was moving through no longer exists. With a
   NULL seam that is every outer.
*/
static KosOpResult goal_attach(KosContext *ctx, KosXferGoal *goal) {

  /* Only the first ATTACHING has split at nothing yet: a later one
     follows a split, since without one the first used the target up. */
  int first = goal->piece_next == 0;
  KosDynNode *base = first ? goal->seam : KosContext_DYN_TOP(ctx);
  int had_target = goal->target != NULL;
  KosCloneShape *shape = &goal->shape;

  if (goal->target) {
    /* the rest of the target starts on the previous split point */
    KosDynNode *bottom = first
        ? shape->bottom : shape->pieces[goal->piece_next - 1].rest;
    const KosLiveRegs *regs = &shape->top;

    /* the registers live now are the ones below the copy */
    if (first)
      kos_dyn_settle_clone(ctx, shape);

    if (goal->piece_next < shape->piece_count) {
      KosXferPiece *piece = &shape->pieces[goal->piece_next++];

      regs = &piece->regs;
      if (piece->rest)
        KosDynNode_PARENT(piece->rest) = NULL;
      else
        goal->target = NULL;
    } else {
      goal->target = NULL;
    }

    /* A later piece goes back onto the split point it was cut from,
       which its skip links name already. The first goes onto the seam,
       and the links that reached below the copy are set from there. */
    kos_dynnode_link(bottom, base);
    if (first)
      kos_dyn_repair_tail(shape->tail, bottom);
    kos_live_regs_apply(ctx, regs);

    /* a region captured while handling an exception handles it again */
    if (shape->frozen)
      kos_dyn_install_regions(ctx, regs->top, base);
  }

  /* Below the seam the extents are the ones the capture ran in; only
     the operands they held come back. This runs after the splice and
     its register load: retargeting the live frame flushes the live pc
     into the frame it leaves, which would overwrite a restore done
     first. The records cover the seam's run, below the first piece. */
  if (first && goal->restore) {
    if (goal_apply_restore(ctx, goal) < 0)
      return EVAL_RAISE();
    if (! had_target)
      kos_dyn_resync_chain(ctx);
  }
  /* the resume pc is the copy's top frame's: set once the target is
     used up */
  if (! goal->target && goal->resume_pc && KosContext_FRAME(ctx))
    KosContext_SET_PC(ctx, goal->resume_pc);

  /* Retire every waiting outer whose frontier LEAVING unlinked. An
     outer waits on a node whose thunk this transfer started from;
     if that node left the chain, the extent the outer was moving
     through no longer exists, and resuming it later would act on
     whatever node came to occupy the same address. LEAVING dropped
     the frontier of each such outer as it unlinked the node
     (goal_void_outers_on), so the outers to retire are the ones with
     no frontier, and a LEAVING that voided none leaves nothing to do.
     With a NULL seam the whole chain went, so every outer goes. */
  if (! goal->seam || goal->voided_outer) {
    KosXferGoal **link = &goal->outer;

    while (*link) {
      KosXferGoal *outer = *link;

      if (! goal->seam || ! outer->frontier) {
        *link = outer->outer;
        outer->outer = NULL;
        kos_xfer_goal_discard(outer);
      } else {
        link = &outer->outer;
      }
    }
    goal->voided_outer = 0;
  }

  return EVAL_NORMAL();
}


/**
   ENTERING: run the enter of the split point the last ATTACHING put at
   dyn_top, if it ended at one: a wind, a dyn-with or a param extent
   (KOS_DYN_CAP_WINDS). Its thunk or __enter__ frame installs directly
   above it, and the goal waits on it. A node already entered answers
   NORMAL, so the pass after the wait re-derives its place. A node
   without the cap, a plain with among them, is not re-entered.
*/
static KosOpResult goal_enter(KosContext *ctx, KosXferGoal *goal) {

  KosDynNode *node;
  KosOpResult r;

  if (goal->piece_entered == goal->piece_next)
    return EVAL_NORMAL();

  node = goal->shape.pieces[goal->piece_entered].regs.top;
  r = kos_dyn_extent_enter(ctx, node);
  if (r.kind == KOS_RES_NORMAL)
    goal->piece_entered++;
  else if (r.kind == KOS_RES_REPROLOGUE || r.kind == KOS_RES_AWAIT)
    goal_wait(goal, node);
  return r;
}


/**
   DELIVERING for a cc-unwind: raise its signal. The signal leaves the
   context from its root, the seam the foreign path in eval.c gives: a
   NULL seam, or the GEN at the bottom of a generator's own context.
   Every goal, waiting outers included, is void with it. The raise is
   prepared as the boundary raise, which sets ctx->terminal: that is
   the record by which the drain and the goal thread return the answer
   as it stands, without the raise ingress.
*/
static KOS_NOINLINE KosOpResult goal_deliver_signal(
    KosContext *ctx,
    KosXferGoal *goal) {

  PyErr_SetObject((PyObject *) Py_TYPE(goal->payload), goal->payload);
  kos_eval_prepare_boundary_raise(ctx);
  kos_xfer_goal_abandon_all(ctx);
  return EVAL_RAISE();
}


/**
   DELIVERING: push the payload, stage it for the receiving frame's
   resume, set it as a pending throw, or raise a cc-unwind's signal at
   the boundary; then retire the goal. An ABORT's abort to a prompt
   has no payload: its target is the handler's frame, which takes
   nothing, and the goal only retires.
   A shift's abort carries the continuation, pushed for the handler's
   first instruction to store.
   A receiving frame that carries a resume the goal
   installed, through a restore record or an attached copy, was
   waiting on a delivery when it was captured: the payload is that
   delivery, staged in the pending slot for the prologue pass that
   follows the drive. A goal that installed no frame state resumes the
   live frame at a pc, and a resume that frame still carries belongs to
   the instruction the transfer left: it is cleared and the payload is
   pushed.
*/
static KosOpResult goal_deliver(KosContext *ctx, KosXferGoal *goal) {

  if (unlikely(goal->is_cc_unwind))
    return goal_deliver_signal(ctx, goal);

  if (! KosContext_FRAME(ctx)) {
    PyErr_SetString(PyExc_SystemError,
        "transfer: no frame to deliver to");
    return EVAL_RAISE();
  }
  if (! goal->target && ! goal->restore)
    kos_dynframe_clear_resume(KosContext_FRAME(ctx));

  if (! goal->payload) {
    /* an ABORT's abort to a prompt: the handler's frame takes nothing */
  } else if (KosDynFrame_RESUME_KIND(KosContext_FRAME(ctx))) {
    Py_XSETREF(KosContext_PENDING_VALUE(ctx), Py_NewRef(goal->payload));
  } else if (KosContext_StackPushRef(ctx, goal->payload) < 0) {
    return EVAL_RAISE();
  }

  kos_xfer_goal_pop(ctx);
  return EVAL_NORMAL();
}


/**
   WINDING_OUT: leave each winder of a yielding generator's span, with
   the span cut above it. When the last is left the goal retires and
   answers the yield itself, EVAL_YIELD with the value, for the yield
   event to freeze the span and present it.
*/
static KOS_NOINLINE KosOpResult goal_wind_out(
    KosContext *ctx,
    KosXferGoal *goal) {

  KosDynNode *node;
  PyObject *val;

  while ((node = kos_dyn_gen_wind_out_next(ctx, goal))) {
    KosOpResult r = kos_dyn_extent_leave(ctx, node, KOS_XFER_EVENT_TRANSFER);

    if (r.kind == KOS_RES_REPROLOGUE || r.kind == KOS_RES_AWAIT) {
      goal_wait(goal, node);
      return r;
    }
    if (r.kind != KOS_RES_NORMAL)
      return r;
    kos_dyn_gen_wind_left(ctx, goal, node);
  }

  /* the lists stand for the freeze and the next install */
  val = goal->payload;
  goal->payload = NULL;
  kos_xfer_goal_pop_cold(ctx);
  return EVAL_YIELD(val);
}


/**
   WINDING_IN: enter the winder at dyn_top, then attach what rested on
   it, until the span is whole; then deliver what the step sent, as the
   install of a span with no winder leaves it: a frame waiting on a
   resume takes the value as its delivery, any other takes it on its
   stack, and a throw is delivered at the yield as every throw is
   (KosContext_ThrowAtYield), chained to the generator's own handled
   exception, if it has one, now that every effect is back in.
*/
static KOS_NOINLINE KosOpResult goal_wind_in(
    KosContext *ctx,
    KosXferGoal *goal) {

  KosGenState *state = goal->gen_state;
  PyObject *payload;
  KosDynFrame *frame;
  int is_throw;

  /* pushed on a running context, which got no frame to thread into */
  if (! KosContext_FRAME(ctx))
    kos_dyn_gen_wind_load(ctx, goal);

  while (goal->piece_entered < state->piece_count) {
    KosDynNode *node = state->pieces[goal->piece_entered].regs.top;
    KosOpResult r = kos_dyn_extent_enter(ctx, node);

    if (r.kind == KOS_RES_REPROLOGUE || r.kind == KOS_RES_AWAIT) {
      goal_wait(goal, node);
      return r;
    }
    if (r.kind != KOS_RES_NORMAL)
      return r;
    if (kos_dyn_gen_wind_attach(ctx, goal) < 0)
      return EVAL_RAISE();
  }

  /* every listed node is back on the chain, in and entered */
  kos_dyn_gen_wind_reset(state);
  payload = goal->payload;
  is_throw = goal->is_throw;
  goal->payload = NULL;
  kos_xfer_goal_pop_cold(ctx);
  frame = KosContext_FRAME(ctx);

  if (is_throw)
    return KosContext_ThrowAtYield(ctx, payload)
        ? EVAL_NORMAL() : EVAL_RAISE();
  if (frame && KosDynFrame_RESUME_KIND(frame)) {
    if (payload)
      Py_XSETREF(KosContext_PENDING_VALUE(ctx), payload);
    return EVAL_NORMAL();
  }
  if (KosContext_StackPushRef(ctx, payload ? payload : Py_None) < 0) {
    Py_XDECREF(payload);
    return EVAL_RAISE();
  }
  Py_XDECREF(payload);
  return EVAL_NORMAL();
}


void kos_xfer_goal_hand(
    KosContext *ctx,
    PyObject *send_val,
    PyObject *throw_exc) {

  KosXferGoal *goal = KosContext_GOAL(ctx);

  if (throw_exc) {
    goal->payload = Py_NewRef(throw_exc);
    goal->is_throw = 1;
  } else if (send_val) {
    goal->payload = Py_NewRef(send_val);
  }
}


void kos_xfer_goal_take_entry(
    KosContext *ctx,
    PyObject *send_val,
    PyObject *throw_exc) {

  KosXferGoal *goal = KosContext_GOAL(ctx);

  if (goal && goal->phase == KOS_GOAL_WINDING_IN && ! goal->payload)
    kos_xfer_goal_hand(ctx, send_val, throw_exc);
}


/**
   Drive the active goal as far as the chain allows, and each outer it
   retires into; with only set, stop once only is no longer the active
   goal. A RAISE with ctx->terminal set is a cc-unwind's boundary
   raise, already prepared (goal_deliver_signal).
*/
static KosOpResult goal_drive(KosContext *ctx, const KosXferGoal *only) {

  while (KosContext_GOAL(ctx) && (! only || KosContext_GOAL(ctx) == only)) {
    KosXferGoal *goal = KosContext_GOAL(ctx);
    KosOpResult r;

    if (goal->frontier) {
      /* Waiting on a thunk. Its frame installs directly above the
         frontier, so any boundary event while it runs finds dyn_top
         above the frontier; the first one that finds dyn_top at the
         frontier is its return, or the revive of its awaitable with
         the value on the block. */
      if (KosContext_DYN_TOP(ctx) != goal->frontier)
        return EVAL_NORMAL();
#if KOS_EVAL_SAFETYNET
      if (kos_dynnode_id(goal->frontier) != goal->frontier_id) {
        PyErr_SetString(PyExc_SystemError,
            "transfer: the node it waited on is not the node the goal"
            " recorded");
        return EVAL_RAISE();
      }
#endif
      if (kos_dyn_extent_settle(ctx, goal->frontier) < 0)
        return EVAL_RAISE();
      goal_end_wait(goal);
      /* the settle took a staged result; anything still staged was for
         the path the transfer replaces */
      Py_CLEAR(KosContext_PENDING_VALUE(ctx));
    }

    switch (goal->phase) {
      case KOS_GOAL_WINDING_OUT:
        /* done, it answers the yield, and never NORMAL */
        return goal_wind_out(ctx, goal);
      case KOS_GOAL_WINDING_IN:
        r = goal_wind_in(ctx, goal);
        if (r.kind != KOS_RES_NORMAL)
          return r;
        break;
      case KOS_GOAL_LEAVING:
        r = goal_leave(ctx, goal);
        if (r.kind != KOS_RES_NORMAL)
          return r;
        goal->phase = KOS_GOAL_ATTACHING;
        /* fall through */
      case KOS_GOAL_ATTACHING:
        r = goal_attach(ctx, goal);
        if (r.kind != KOS_RES_NORMAL)
          return r;
        goal->phase = KOS_GOAL_ENTERING;
        /* fall through */
      case KOS_GOAL_ENTERING:
        r = goal_enter(ctx, goal);
        if (r.kind != KOS_RES_NORMAL)
          return r;
        if (goal->target) {
          /* the next piece goes on above the split point just entered */
          goal->phase = KOS_GOAL_ATTACHING;
          continue;
        }
        goal->phase = KOS_GOAL_DELIVERING;
        /* fall through */
      case KOS_GOAL_DELIVERING:
      default:
        r = goal_deliver(ctx, goal);
        if (r.kind != KOS_RES_NORMAL)
          return r;
        break;
    }
  }
  return EVAL_NORMAL();
}


KosOpResult kos_xfer_goal_drive(KosContext *ctx) {
  return goal_drive(ctx, NULL);
}


/**
   goal, run in place, is still linked: a step paused or failed, and the
   goal outlives the storage it was run in. Move it to storage of its
   own, in the same place among the goals. Returns 0, or -1 with
   MemoryError set and the goal unlinked and released.
*/
static int goal_keep(KosContext *ctx, KosXferGoal *goal) {

  KosXferGoal **link = &KosContext_GOAL(ctx);
  KosXferGoal *kept;

  while (*link != goal)
    link = &(*link)->outer;

  kept = PyMem_Malloc(sizeof(KosXferGoal));
  if (! kept) {
    *link = goal->outer;
    goal->outer = NULL;
    if (goal->frontier)
      goal_end_wait(goal);
    kos_xfer_goal_discard(goal);
    PyErr_NoMemory();
    return -1;
  }

  *kept = *goal;
  kept->in_place = 0;
  *link = kept;
  return 0;
}


/**
   What a run answers for r, the drive's own answer. A step that
   installed a frame for a segment, or for an __enter__ or __exit__,
   left the goal waiting on it: the frame is threaded into from here,
   NORMAL, when it can be and the breaker is down, as a call's install
   is. A finished run threads on from the registers the transfer
   loaded, unless a delivery is staged for the frame's resume or an
   outer goal is still to be looked at. Anything else is the drive's
   answer as it stands, for the caller's ingress.
*/
static inline KosOpResult goal_run_answer(KosContext *ctx, KosOpResult r) {

  const KosXferGoal *waiting = KosContext_GOAL(ctx);

  if (r.kind == KOS_RES_REPROLOGUE) {
    if (waiting && waiting->frontier
        && KosContext_DYN_TOP(ctx) != waiting->frontier
        && ! KosContext_PENDING_VALUE(ctx)
        && kos_frame_threads(KosContext_FRAME(ctx))
        && ! KOS_EVAL_BREAKER_UP(ctx))
      return EVAL_NORMAL();
    return r;
  }
  if (r.kind != KOS_RES_NORMAL)
    return r;

  if (! waiting
      && ! KosContext_PENDING_VALUE(ctx)
      && kos_frame_threads(KosContext_FRAME(ctx)))
    return EVAL_NORMAL();
  return EVAL_REPROLOGUE();
}


/**
   The body of kos_xfer_goal_run and kos_xfer_goal_run_kept, kept the
   constant: a goal in storage that outlives it is never moved.
*/
KOS_OP_INLINE KosOpResult goal_run(
    KosContext *ctx,
    KosXferGoal *goal,
    const int kept) {

  KosOpResult r;

  goal->in_place = kept ? 2 : 1;
  kos_xfer_goal_push(ctx, goal);

  /* what the prologue does ahead of a drive: a delivery staged for the
     path the transfer replaces is dropped */
  Py_CLEAR(KosContext_PENDING_VALUE(ctx));

  r = goal_drive(ctx, goal);

  if (! kept) {
    int linked = 0;

    for (KosXferGoal *g = KosContext_GOAL(ctx); g; g = g->outer) {
      if (g == goal) {
        linked = 1;
        break;
      }
    }
    if (linked && goal_keep(ctx, goal) < 0) {
      if (r.kind == KOS_RES_AWAIT)
        Py_XDECREF(r.val);
      return EVAL_RAISE();
    }
  }

  return goal_run_answer(ctx, r);
}


KosOpResult kos_xfer_goal_run(KosContext *ctx, KosXferGoal *goal) {
  return goal_run(ctx, goal, 0);
}


KosOpResult kos_xfer_goal_run_kept(KosContext *ctx, KosXferGoal *goal) {
  return goal_run(ctx, goal, 1);
}


KosOpResult kos_xfer_goal_run_pushed(KosContext *ctx) {

  KosXferGoal *goal = KosContext_GOAL(ctx);

  /* as the prologue does ahead of a drive: a goal whose frame has just
     returned may find the frame's result staged, which its settle
     takes; any other staged delivery is dropped */
  if (! goal->frontier)
    Py_CLEAR(KosContext_PENDING_VALUE(ctx));
  return goal_run_answer(ctx, goal_drive(ctx, goal));
}


// The end.
