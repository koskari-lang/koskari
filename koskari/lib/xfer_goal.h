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

   Four phases. LEAVING unlinks from dyn_top down to the seam. ATTACHING
   splices the target onto the chain a piece at a time: each piece ends
   at the next node a transfer runs (KOS_DYN_CAP_WINDS), outermost
   first, and ENTERING runs that node's enter before the next piece goes
   on, so a before thunk or __enter__ runs with only the extents outside
   it on the chain, as dynamic-wind runs one. The two alternate until
   the target is used up, and the last piece installs the resume pc.
   The target arrives as one clone that already numbered its frames,
   linked its frames' and segments' back links, and recorded each split
   point with the registers live there (KosCloneShape); the first
   ATTACHING completes it from the registers at the seam, and every
   ATTACHING is a relink and a register load, with no walk. DELIVERING
   pushes the payload, or sets the pending throw, and retires the goal;
   a shift's abort, which has no target, instead calls its handler with
   k above the prompt, its seam.

   A leave or enter that hands control to a thunk frame or an awaitable
   makes the goal wait on that node: the thunk's frame installs directly
   above it, and the driver does nothing until dyn_top is the node
   again, which means the thunk's frame came and went. Everything above
   belongs to the thunk.

   Goals nest: a continuation invoked from inside a thunk pushes an inner
   goal while the outer stays waiting. ATTACHING retires every outer
   goal whose frontier the inner's LEAVING unlinked, since the extent it
   was moving through is gone; with a NULL seam that is every outer. The
   wait marks its frontier node, so LEAVING voids the outer as it
   unlinks that node, and an ATTACHING whose LEAVING voided none touches no
   outer. A raise frees every goal not waiting on a thunk still above
   its node; a waiting goal lives as long as its thunk, so a raise the
   thunk catches itself leaves the transfer going, and one that leaves
   the thunk frees the goal when the walk unlinks the node it waits on.
   The chain a walk left behind is a chain to raise through.

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
  KOS_GOAL_ATTACHING,
  KOS_GOAL_ENTERING,
  KOS_GOAL_DELIVERING,
} KosXferGoalPhase;


/**
   A node from the seam down to the seam's frame whose captured operand
   segment (and, for the frame, registers) ATTACHING puts back. The node
   itself keeps its identity and extent state: it is the same extent at
   the capture point and the invoke point, only the operands it held
   differ.
*/
typedef struct KosXferRestore {
  KosDynNode *node;            /* borrowed live node */
  uint32_t node_id;
  KosSegment *stack;           /* owned copy of the captured segment (NULL:
                                  none) */
  KosOp *pc;                   /* frame only */
  KosOp *acting_pc;
  const KosResumeKind *resume_kind;
  void *resume_state;          /* the kind's clone of the template's
                                  state, owned until applied */
} KosXferRestore;


struct KosXferGoal {
  KosXferGoal *outer;          /* waiting enclosing goal, or NULL */

  /* owned clone still to attach, NULL once all of it is attached or
     when there is nothing to attach */
  KosDynNode *target;
  KosDynNode *seam;            /* borrowed: leave down to this node and
                                  attach to it; NULL means the root */
  uint32_t seam_id;
  /* the node whose thunk we wait on, or NULL; the thunk's frame sits
     directly above it */
  KosDynNode *frontier;
  uint32_t frontier_id;

  /* what the clone of the target met: its bottom, its split points
     with their registers (shape.pieces, owned), and the registers at
     its top; empty when there is no target */
  KosCloneShape shape;
  size_t piece_next;           /* the entry the next ATTACHING splits at */
  size_t piece_entered;        /* entries whose node ENTERING has run */

  /* owned: the value, the exception, or a shift abort's handler */
  PyObject *payload;
  PyObject *continuation_ref;  /* owned, or NULL: keeps the wrapper alive */
  KosOp *resume_pc;            /* NULL when unset */
  KosXferRestore *restore;     /* owned array, or NULL */
  size_t restore_count;

  uint8_t phase;               /* KosXferGoalPhase */
  uint8_t is_cc_unwind;        /* delivery is a boundary raise of payload */
  /* LEAVING unlinked an outer's frontier; ATTACHING retires the voided
     outers */
  uint8_t voided_outer;
  /* a shift's abort: DELIVERING calls the handler in payload with the
     continuation in continuation_ref, above the seam, the prompt */
  uint8_t shift_abort;
};


#define KosXferGoal_OUTER(g)     (((KosXferGoal *)(g))->outer)


/**
   Allocate a goal. target is an owned, already cloned segment (or
   NULL); seam is borrowed and must be on the live chain (or NULL for
   the root); payload is borrowed and a reference is taken;
   continuation_ref likewise. A caller with a target sets shape to what
   its clone recorded, which the goal then owns.

   :return: the goal, or NULL with PyErr set (target is then freed)
*/
KosXferGoal *kos_xfer_goal_new(
    KosDynNode *target,
    KosDynNode *seam,
    PyObject *payload,
    KosOp *resume_pc,
    PyObject *continuation_ref,
    int is_cc_unwind);


/**
   Release a goal's own resources: the target still unattached, the
   payload, the continuation reference, the restore records, the split
   points. Does not touch outer.
*/
void kos_xfer_goal_free(KosXferGoal *goal);

/**
   Record what ATTACHING puts back below the seam: for each node from the
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
   Make goal the active goal; whatever was active waits as its outer.
*/
void kos_xfer_goal_push(KosContext *ctx, KosXferGoal *goal);


/**
   Free the active goal and every outer behind it. The chain is left as
   it is.
*/
void kos_xfer_goal_abandon_all(KosContext *ctx);


/**
   A raise entered the walk: free the goals that are not waiting on a
   live thunk, from the active one down to the first that waits on one.
   Those were stepping when the raise came, or their thunk is no longer
   above their node (dyn_top is the node), and the raise ends them. A
   goal whose thunk is still above its node stays: the thunk may catch
   the raise itself, after which it returns and the goal goes on, and
   if it does not, the walk unlinks the node and retires the goal there
   (kos_xfer_goal_thunk_died). Every outer of a waiting goal waits on a
   node further down.
*/
void kos_xfer_goal_abandon_stepping(KosContext *ctx);


/**
   The raise walk unlinked node, which a goal waited on (it carried
   KOS_DYNF_GOAL_WAIT, with node_id its id read before the unlink): the
   thunk the goal waited for is dead. Free that goal and every goal
   started above it. The node is never dereferenced. Nothing to do when
   no goal waits on it (a mark a freed goal left behind).
*/
void kos_xfer_goal_thunk_died(
    KosContext *ctx,
    const KosDynNode *node,
    uint32_t node_id);


/**
   Drive the active goal, and each outer it retires into, as far as the
   chain allows.

   :param out_boundary: set to 1 when the answer is a RAISE the caller
     must return to the driver as it stands (a foreign cc-unwind's
     signal, already prepared as a boundary raise), 0 otherwise
   :return: EVAL_NORMAL when no goal remains or the active goal is
     waiting; EVAL_REPROLOGUE when a leave or enter installed a frame;
     EVAL_AWAIT with the awaitable when one suspended; EVAL_RAISE with
     PyErr set when a step failed, or on a boundary raise
*/
KosOpResult kos_xfer_goal_drive(KosContext *ctx, int *out_boundary);


/**
   1 when the active goal waits on node.
*/
int kos_xfer_goal_waits_on(KosContext *ctx, const KosDynNode *node);


/**
   1 when a goal is active and the next drive will act on it: it is not
   waiting, or the thunk it waits on has returned (dyn_top is back at
   the node it waits on). A waiting goal whose thunk is still running is
   dormant, and the code in that thunk owns the evaluator's staged
   deliveries.
*/
int kos_xfer_goal_will_drive(KosContext *ctx);


int kos_xfer_goal_traverse(KosXferGoal *goal, visitproc visit, void *arg);


#endif


// The end.
