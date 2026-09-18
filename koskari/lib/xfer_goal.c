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


KosXferGoal *kos_xfer_goal_new(
    KosDynNode *target,
    KosDynNode *seam,
    PyObject *payload,
    int is_throw,
    uint32_t resume_pc,
    PyObject *continuation_ref,
    int is_cc_unwind) {

  KosXferGoal *goal = PyMem_Malloc(sizeof(KosXferGoal));

  KOS_TRACE_EVENTS_INC(KOS_TE_TRANSFER_GOAL_NEW);
  if (! goal) {
    if (target)
      kos_dynchain_free(target, NULL);
    PyErr_NoMemory();
    return NULL;
  }

  goal->outer = NULL;
  goal->target = target;
  goal->seam = seam;
  goal->seam_id = seam ? kos_dynnode_id(seam) : 0;
  goal->frontier = NULL;
  goal->frontier_id = 0;
  goal->park_top = NULL;
  goal->payload = Py_XNewRef(payload);
  goal->continuation_ref = Py_XNewRef(continuation_ref);
  goal->resume_pc = resume_pc;
  goal->restore = NULL;
  goal->restore_count = 0;
  goal->phase = KOS_GOAL_LEAVING;
  goal->is_throw = (uint8_t) (is_throw != 0);
  goal->is_cc_unwind = (uint8_t) (is_cc_unwind != 0);
  return goal;
}


void kos_xfer_goal_free(KosXferGoal *goal) {

  if (! goal)
    return;
  if (goal->target) {
    kos_dynchain_free(goal->target, NULL);
    goal->target = NULL;
  }
  Py_CLEAR(goal->payload);
  Py_CLEAR(goal->continuation_ref);
  if (goal->restore) {
    for (size_t i = 0; i < goal->restore_count; i++) {
      KosXferRestore *r = &goal->restore[i];

      if (r->stack)
        KosStack_Release(r->stack);
      /* a cloned resume state never applied gets its kind's clear */
      if (r->resume_kind && r->resume_kind->clear)
        r->resume_kind->clear(r->resume_state);
    }
    PyMem_Free(goal->restore);
    goal->restore = NULL;
    goal->restore_count = 0;
  }
  PyMem_Free(goal);
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
      /* The template keeps its state for the next landing: the record
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
static int goal_apply_restore(KosEvalState *es, KosXferGoal *goal) {

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
         restore of that frame must land in them too. */
      if ((KosDynFrame *) node == KosEvalState_FRAME(es)) {
        KosEvalState_SET_PC(es, r->pc);
        es->acting_pc = r->acting_pc;
      }
    }
  }
  return 0;
}


void kos_xfer_goal_arm(KosEvalState *es, KosXferGoal *goal) {

  goal->outer = KosEvalState_GOAL(es);
  KosEvalState_GOAL(es) = goal;
}


/**
   Retire the active goal, making its outer (if any) active.
*/
static void kos_xfer_goal_pop(KosEvalState *es) {

  KosXferGoal *goal = KosEvalState_GOAL(es);

  KosEvalState_GOAL(es) = goal->outer;
  goal->outer = NULL;
  kos_xfer_goal_free(goal);
}


void kos_xfer_goal_abandon_all(KosEvalState *es) {

  while (KosEvalState_GOAL(es))
    kos_xfer_goal_pop(es);
}


int kos_xfer_goal_will_drive(KosEvalState *es) {

  KosXferGoal *goal = KosEvalState_GOAL(es);

  if (! goal)
    return 0;
  return ! goal->frontier || KosEvalState_DYN_TOP(es) == goal->park_top;
}


int kos_xfer_goal_waits_on(KosEvalState *es, const KosDynNode *node) {

  KosXferGoal *goal = KosEvalState_GOAL(es);

  return goal && goal->frontier == node;
}


int kos_xfer_goal_traverse(KosXferGoal *goal, visitproc visit, void *arg) {

  Py_VISIT(goal->payload);
  Py_VISIT(goal->continuation_ref);
  for (size_t i = 0; i < goal->restore_count; i++) {
    int r = KosStack_Traverse(goal->restore[i].stack, visit, arg);
    if (r)
      return r;
  }
  if (goal->target)
    return kos_dynchain_traverse(goal->target, NULL, visit, arg);
  return 0;
}


/* === Phases === */


/**
   Park the goal on node: its thunk or awaitable is in flight, and the
   driver does nothing until dyn_top is park_top again.
*/
static void goal_park(KosXferGoal *goal, KosDynNode *node, KosDynNode *top) {

  goal->frontier = node;
  goal->frontier_id = kos_dynnode_id(node);
  goal->park_top = top;
}


/**
   LEAVING: unlink from dyn_top down to the seam. A flattened generator
   on the way is finished as the raise walk finishes one; a root
   generator is the driver boundary, which only a cc-unwind reaches, as
   its seam.
*/
static KosOpResult goal_leave(KosEvalState *es, KosXferGoal *goal) {

#if KOS_EVAL_SAFETYNET
  /* checked before anything is unlinked: a seam that is not on the
     chain would otherwise be discovered only once the chain is empty */
  if (goal->seam
      && (! kos_dyn_reaches(es, goal->seam)
      || kos_dynnode_id(goal->seam) != goal->seam_id)) {
    PyErr_SetString(PyExc_SystemError,
        "transfer: the seam is not on the chain");
    return EVAL_RAISE();
  }
#endif

  while (KosEvalState_DYN_TOP(es) != goal->seam) {
    KosDynNode *top = KosEvalState_DYN_TOP(es);
    KosOpResult r;

    if (! top) {
      PyErr_SetString(PyExc_SystemError,
          "transfer: the seam is not on the chain");
      return EVAL_RAISE();
    }
    if (KosDynNode_IS_GEN(top)) {
      if (! KosDynNode_PARENT(top)) {
        PyErr_SetString(PyExc_SystemError,
            "transfer would leave a generator context");
        return EVAL_RAISE();
      }
      kos_dyn_finish_flattened_gen(es, (KosDynGen *) top);
      continue;
    }

    r = kos_dyn_unlink_top(es, KOS_XFER_EVENT_TRANSFER, 1);
    if (r.kind == KOS_RES_NORMAL)
      continue;
    if (r.kind == KOS_RES_REPROLOGUE || r.kind == KOS_RES_AWAIT)
      goal_park(goal, top, top);
    return r;
  }
  return EVAL_NORMAL();
}


/**
   LANDING: splice the target onto the seam and install the resume pc.
   Synchronous and infallible; runs once. An outer goal whose frontier
   LEAVING unlinked is void: the node it waited on and everything above
   it are gone, and the extent it was moving through no longer exists.
   With a NULL seam that is every outer.
*/
static KosOpResult goal_land(KosEvalState *es, KosXferGoal *goal) {

  int had_target = goal->target != NULL;

  if (goal->target) {
    KosDynNode *bottom = goal->target;

    while (KosDynNode_PARENT(bottom))
      bottom = KosDynNode_PARENT(bottom);
    KosDynNode_PARENT(bottom) = goal->seam;
    /* The clone's frames carry capture-time depths; the invoke checked
       that they fit above the seam before arming. */
    kos_dyn_renumber_frames(goal->target, goal->seam,
        kos_dyn_frame_depth_at(goal->seam));
    KosEvalState_DYN_TOP(es) = goal->target;
    goal->target = NULL;
    kos_dyn_resync_chain(es);
  }

  /* Below the seam the extents are the ones the capture ran in; only
     the operands they held come back. This runs after the splice and
     its resync: retargeting the live frame flushes the live pc into
     the frame it leaves, which would overwrite a restore done first. */
  if (goal->restore) {
    if (goal_apply_restore(es, goal) < 0)
      return EVAL_RAISE();
    if (! had_target)
      kos_dyn_resync_chain(es);
  }
  if (goal->resume_pc != (uint32_t) -1 && KosEvalState_FRAME(es))
    KosEvalState_SET_PC(es, goal->resume_pc);

  /* Retire every parked outer whose frontier LEAVING unlinked. An
     outer is parked on a node whose thunk this transfer started from;
     if that node left the chain, the extent the outer was moving
     through no longer exists, and resuming it later would act on
     whatever node came to occupy the same address. With a NULL seam
     the whole chain went, so every outer goes. Pointer reach plus id,
     as goal_leave checks its seam, so a recycled address cannot pass. */
  {
    KosXferGoal **link = &goal->outer;

    while (*link) {
      KosXferGoal *outer = *link;
      /* An outer is parked by construction; one with no frontier has
         no place to resume from and would stall, so it goes too. */
      int gone = ! goal->seam
          || ! outer->frontier
          || ! kos_dyn_reaches(es, outer->frontier)
          || kos_dynnode_id(outer->frontier) != outer->frontier_id;

      if (gone) {
        *link = outer->outer;
        outer->outer = NULL;
        kos_xfer_goal_free(outer);
      } else {
        link = &outer->outer;
      }
    }
  }
  return EVAL_NORMAL();
}


#define GOAL_SPAN_BUF 8


/**
   ENTERING: from the seam upward, enter each node that wants it. A
   node already entered answers NORMAL, so a pass after a suspension
   re-derives its place. Single-shot with nodes are never re-entered by
   a transfer; multi-shot ones (dyn-with) and winds are, and a param
   extent is re-bound.
*/
static KosOpResult goal_enter(KosEvalState *es, KosXferGoal *goal) {

  KosDynNode *stack_buf[GOAL_SPAN_BUF];
  KosDynNode **span = stack_buf;
  size_t count = 0;
  size_t cap = GOAL_SPAN_BUF;
  KosDynNode *node;
  KosOpResult out = EVAL_NORMAL();

  for (node = KosEvalState_DYN_TOP(es); node && node != goal->seam;
      node = KosDynNode_PARENT(node)) {
    if (count == cap) {
      KosDynNode **next = PyMem_Malloc(cap * 2 * sizeof(KosDynNode *));
      if (! next) {
        if (span != stack_buf)
          PyMem_Free(span);
        PyErr_NoMemory();
        return EVAL_RAISE();
      }
      memcpy(next, span, count * sizeof(KosDynNode *));
      if (span != stack_buf)
        PyMem_Free(span);
      span = next;
      cap *= 2;
    }
    span[count++] = node;
  }

  while (count > 0) {
    KosDynNode *before;
    KosOpResult r;

    node = span[--count];
    if (KosDynNode_IS_PARAM(node)) {
      if (KosDynParam_SAVED(node))
        continue;
      if (kos_dyn_param_wind(es, (KosDynParam *) node) < 0) {
        out = EVAL_RAISE();
        break;
      }
      continue;
    }
    if (! KosDynNode_HAS_CAP(node, KOS_DYN_CAP_CONTROL_HOOK))
      continue;
    if (KosDynNode_IS_WITH(node)
        && ! KosDynNode_HAS_CAP(node, KOS_DYN_CAP_NEEDS_INSTALL_WIND))
      continue;

    before = KosEvalState_DYN_TOP(es);
    r = kos_dyn_extent_enter(es, node);
    if (r.kind == KOS_RES_NORMAL)
      continue;
    if (r.kind == KOS_RES_REPROLOGUE || r.kind == KOS_RES_AWAIT)
      goal_park(goal, node, before);
    out = r;
    break;
  }

  if (span != stack_buf)
    PyMem_Free(span);
  return out;
}


/**
   DELIVERING: push the payload, arm it as a throw, or raise it at the
   boundary; then retire the goal.
*/
static KosOpResult goal_deliver(
    KosEvalState *es,
    KosXferGoal *goal,
    int *out_boundary) {

  KosContext *ctx = KosEvalState_CONTEXT(es);

  if (goal->is_cc_unwind) {
    /* the signal leaves this context: every goal, parked outers
       included, is void with it */
    PyErr_SetObject((PyObject *) Py_TYPE(goal->payload), goal->payload);
    kos_eval_prepare_boundary_raise(ctx, es);
    kos_xfer_goal_abandon_all(es);
    *out_boundary = 1;
    return EVAL_RAISE();
  }

  if (goal->is_throw) {
    KosContext_INJECT_MODE(ctx) = KOS_INJECT_THROW;
    Py_XSETREF(KosContext_INJECT_VALUE(ctx), Py_NewRef(goal->payload));
    Py_XSETREF(KosContext_INJECT_TYPE(ctx),
        Py_NewRef((PyObject *) Py_TYPE(goal->payload)));
  } else {
    if (! KosEvalState_FRAME(es)) {
      PyErr_SetString(PyExc_SystemError,
          "transfer: no frame to deliver to");
      return EVAL_RAISE();
    }
#if KOS_EVAL_SAFETYNET
    if (KosDynFrame_RESUME_FN(KosEvalState_FRAME(es))) {
      PyErr_SetString(PyExc_SystemError,
          "transfer: landed on a frame carrying a resume");
      return EVAL_RAISE();
    }
#endif
    if (KosEvalState_StackPushRef(es, goal->payload) < 0)
      return EVAL_RAISE();
  }

  kos_xfer_goal_pop(es);
  return EVAL_NORMAL();
}


KosOpResult kos_xfer_goal_drive(KosEvalState *es, int *out_boundary) {

  *out_boundary = 0;

  while (KosEvalState_GOAL(es)) {
    KosXferGoal *goal = KosEvalState_GOAL(es);
    KosOpResult r;

    if (goal->frontier) {
      /* Waiting on a thunk. Its frame installs above park_top, so any
         boundary event while it runs finds dyn_top above park_top; the
         first one that finds dyn_top at park_top is its return, or the
         revive of its awaitable with the value on the block. */
      if (KosEvalState_DYN_TOP(es) != goal->park_top)
        return EVAL_NORMAL();
#if KOS_EVAL_SAFETYNET
      if (! kos_dyn_reaches(es, goal->frontier)
          || kos_dynnode_id(goal->frontier) != goal->frontier_id) {
        PyErr_SetString(PyExc_SystemError,
            "transfer: the node it waited on left the chain");
        return EVAL_RAISE();
      }
#endif
      if (kos_dyn_extent_settle(es, goal->frontier) < 0)
        return EVAL_RAISE();
      goal->frontier = NULL;
      goal->frontier_id = 0;
      goal->park_top = NULL;
    }

    switch (goal->phase) {
      case KOS_GOAL_LEAVING:
        r = goal_leave(es, goal);
        if (r.kind != KOS_RES_NORMAL)
          return r;
        goal->phase = KOS_GOAL_LANDING;
        /* fall through */
      case KOS_GOAL_LANDING:
        r = goal_land(es, goal);
        if (r.kind != KOS_RES_NORMAL)
          return r;
        goal->phase = KOS_GOAL_ENTERING;
        /* fall through */
      case KOS_GOAL_ENTERING:
        r = goal_enter(es, goal);
        if (r.kind != KOS_RES_NORMAL)
          return r;
        goal->phase = KOS_GOAL_DELIVERING;
        /* fall through */
      case KOS_GOAL_DELIVERING:
      default:
        r = goal_deliver(es, goal, out_boundary);
        if (r.kind != KOS_RES_NORMAL)
          return r;
        break;
    }
  }
  return EVAL_NORMAL();
}


// The end.
