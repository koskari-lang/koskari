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

  KOS_TRACE_EVENTS_INC(KOS_TE_TRANSFER_PLAN_BUILD);
  if (! goal) {
    if (target)
      kos_dynchain_free(target, NULL);
    PyErr_NoMemory();
    return NULL;
  }

  goal->outer = NULL;
  goal->target = target;
  goal->seam = seam;
  goal->seam_id = seam ? KosDynNode_NODE_ID(seam) : 0;
  goal->frontier = NULL;
  goal->frontier_id = 0;
  goal->park_top = NULL;
  goal->payload = Py_XNewRef(payload);
  goal->continuation_ref = Py_XNewRef(continuation_ref);
  goal->resume_pc = resume_pc;
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
  PyMem_Free(goal);
}


void kos_xfer_goal_arm(KosEvalState *es, KosXferGoal *goal) {

  goal->outer = KosEvalState_GOAL(es);
  KosEvalState_GOAL(es) = goal;
  KosContext_FLAGS(KosEvalState_CONTEXT(es)) |= KOS_CTX_IN_TRANSFER;
}


/**
   Retire the active goal, making its outer (if any) active.
*/
static void kos_xfer_goal_pop(KosEvalState *es) {

  KosXferGoal *goal = KosEvalState_GOAL(es);

  KosEvalState_GOAL(es) = goal->outer;
  goal->outer = NULL;
  kos_xfer_goal_free(goal);
  if (! KosEvalState_GOAL(es))
    KosContext_FLAGS(KosEvalState_CONTEXT(es)) &= ~KOS_CTX_IN_TRANSFER;
}


void kos_xfer_goal_abandon_all(KosEvalState *es) {

  while (KosEvalState_GOAL(es))
    kos_xfer_goal_pop(es);
}


int kos_xfer_goal_waits_on(KosEvalState *es, const KosDynNode *node) {

  KosXferGoal *goal = KosEvalState_GOAL(es);

  return goal && goal->frontier == node;
}


int kos_xfer_goal_traverse(KosXferGoal *goal, visitproc visit, void *arg) {

  Py_VISIT(goal->payload);
  Py_VISIT(goal->continuation_ref);
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
  goal->frontier_id = KosDynNode_NODE_ID(node);
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
          || KosDynNode_NODE_ID(goal->seam) != goal->seam_id)) {
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
   Synchronous and infallible; runs once. An undelimited landing (seam
   NULL) has replaced the chain every outer goal was moving through, so
   those are void.
*/
static KosOpResult goal_land(KosEvalState *es, KosXferGoal *goal) {

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

  if (goal->resume_pc != (uint32_t) -1 && KosEvalState_FRAME(es))
    KosEvalState_SET_PC(es, goal->resume_pc);

  if (! goal->seam) {
    while (goal->outer) {
      KosXferGoal *outer = goal->outer;
      goal->outer = outer->outer;
      outer->outer = NULL;
      kos_xfer_goal_free(outer);
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
          || KosDynNode_NODE_ID(goal->frontier) != goal->frontier_id) {
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
