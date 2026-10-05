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
   koskari/lib/xfer_goal.h

   Transfer goals: moving control from the current dynamic chain to a
   target chain state. A goal records where control is going, never
   what to do: the driver derives its next step from the live chain on
   every pass, so a chain that moved under a suspension is its input
   rather than a hazard.

   Four phases. LEAVING unlinks from dyn_top down to the seam. LANDING
   splices the target segment onto the seam and installs the resume pc.
   ENTERING runs the extent contract's enter for each node of the landed
   segment, outermost first. DELIVERING pushes the payload, or arms the
   throw, and retires the goal.

   A leave or enter that hands control to a thunk frame or an awaitable
   parks the goal: the driver records the node it is waiting on and
   dyn_top as it was at the firing, and does nothing until dyn_top is
   that again, which means the thunk's frame came and went. Everything
   above belongs to the thunk.

   Goals nest: a continuation invoked from inside a thunk arms an inner
   goal while the outer stays parked. A landing voids every outer goal
   whose frontier the inner's LEAVING unlinked, since the extent it was
   moving through is gone; with a NULL seam that is every outer. A raise
   frees the whole stack; the chain a walk left behind is a chain to
   raise through.

   :author: Christopher O'Brien <obriencj@gmail.com>
   :license: GNU General Public License v3
*/


#ifndef KOSKARI_XFER_GOAL_H
#define KOSKARI_XFER_GOAL_H


#include "lib.h"
#include "eval.h"
#include "dynchain.h"


typedef enum KosXferGoalPhase {
  KOS_GOAL_LEAVING = 0,
  KOS_GOAL_LANDING,
  KOS_GOAL_ENTERING,
  KOS_GOAL_DELIVERING,
} KosXferGoalPhase;


/**
   A node from the seam down to the seam's frame whose captured operand
   segment (and, for the frame, registers) LANDING puts back. The node
   itself keeps its identity and extent state: it is the same extent at
   the capture point and the invoke point, only the operands it held
   differ.
*/
typedef struct KosXferRestore {
  KosDynNode *node;            /* borrowed live node */
  uint32_t node_id;
  KosSegment *stack;           /* owned copy of the captured segment (NULL:
                                  none) */
  uint32_t pc;                 /* frame only */
  uint32_t acting_pc;
  const KosResumeKind *resume_kind;
  void *resume_state;          /* the kind's clone of the template's
                                  state, owned until applied */
} KosXferRestore;


struct KosXferGoal {
  KosXferGoal *outer;          /* parked enclosing goal, or NULL */

  KosDynNode *target;          /* owned clone to land, NULL until landed
                                  or when there is nothing to land */
  KosDynNode *seam;            /* borrowed: leave down to this node and
                                  land on it; NULL means the root */
  uint32_t seam_id;
  KosDynNode *frontier;        /* node whose thunk we wait on, or NULL */
  uint32_t frontier_id;
  KosDynNode *park_top;        /* dyn_top as it was when frontier fired */

  PyObject *payload;           /* owned: the value, or the exception */
  PyObject *continuation_ref;  /* owned, or NULL: keeps the wrapper alive */
  uint32_t resume_pc;          /* UINT32_MAX when unset */
  KosXferRestore *restore;     /* owned array, or NULL */
  size_t restore_count;

  uint8_t phase;               /* KosXferGoalPhase */
  uint8_t is_cc_unwind;        /* delivery is a boundary raise of payload */
};


#define KosXferGoal_OUTER(g)     (((KosXferGoal *)(g))->outer)


/**
   Allocate a goal. target is an owned, already cloned segment (or
   NULL); seam is borrowed and must be on the live chain (or NULL for
   the root); payload is borrowed and a reference is taken;
   continuation_ref likewise.

   :return: the goal, or NULL with PyErr set (target is then freed)
*/
KosXferGoal *kos_xfer_goal_new(
    KosDynNode *target,
    KosDynNode *seam,
    PyObject *payload,
    uint32_t resume_pc,
    PyObject *continuation_ref,
    int is_cc_unwind);


/**
   Release a goal's own resources: an unlanded target, the payload, the
   continuation reference, the restore records. Does not touch outer.
*/
void kos_xfer_goal_free(KosXferGoal *goal);

/**
   Record what LANDING puts back below the seam: for each node from the
   live seam down to and including the nearest frame, paired in step
   with its template counterpart from tmpl_seam down, a copy of the
   counterpart's operand segment and, for the frame, its registers.
   The two runs must match by node id.

   :return: 0, or -1 with PyErr set
*/
int kos_xfer_goal_capture_restore(
    KosXferGoal *goal,
    KosDynNode *live_seam,
    KosDynNode *tmpl_seam);


/**
   Make goal the active goal, parking whatever was active as its outer.
*/
void kos_xfer_goal_arm(KosEvalState *es, KosXferGoal *goal);


/**
   Free the active goal and every outer behind it. The chain is left as
   it is.
*/
void kos_xfer_goal_abandon_all(KosEvalState *es);


/**
   Drive the active goal, and each outer it retires into, as far as the
   chain allows.

   :param out_boundary: set to 1 when the answer is a RAISE the caller
     must return to the driver as it stands (a foreign cc-unwind's
     signal, already prepared as a boundary raise), 0 otherwise
   :return: EVAL_NORMAL when no goal remains or the active goal is
     parked; EVAL_REPROLOGUE when a leave or enter installed a frame;
     EVAL_AWAIT with the awaitable when one suspended; EVAL_RAISE with
     PyErr set when a step failed, or on a boundary raise
*/
KosOpResult kos_xfer_goal_drive(KosEvalState *es, int *out_boundary);


/**
   1 when the active goal is parked on node.
*/
int kos_xfer_goal_waits_on(KosEvalState *es, const KosDynNode *node);

/**
   1 when a goal is armed and the next drive will act on it: it is not
   parked, or the thunk it parked on has returned (dyn_top is back at
   park_top). A parked goal whose thunk is still running is dormant, and
   the code in that thunk owns the evaluator's staged deliveries.
*/
int kos_xfer_goal_will_drive(KosEvalState *es);


int kos_xfer_goal_traverse(KosXferGoal *goal, visitproc visit, void *arg);


#endif


// The end.
