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
   pushes the payload, or sets the pending throw, and retires the goal.
   An abort to a prompt is the same four: its seam is the prompt, its
   target the handler's one frame, and it has no payload.

   A leave or enter that hands control to a thunk frame or an awaitable
   makes the goal wait on that node: the thunk's frame installs directly
   above it, and the driver does nothing until dyn_top is the node
   again, which means the thunk's frame came and went. Everything above
   belongs to the thunk.

   A goal is storage only for as long as its transfer is unfinished. A
   transfer that starts at an instruction which keeps its own frame
   runs at once from a goal in that instruction's storage
   (kos_xfer_goal_run), and is allocated only if a step pauses or
   fails. One that starts where the calling frame may be written to
   after the start, or inside the raise walk, is allocated and pushed
   (kos_xfer_goal_new, kos_xfer_goal_push) for the prologue to drive.

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
  /* A generator's yield leaving the winders of its span, innermost
     first, and its next step entering them again, outermost first. No
     node leaves the chain for good: see kos_dyn_gen_wind_out_next and
     kos_dyn_gen_wind_attach. */
  KOS_GOAL_WINDING_OUT,
  KOS_GOAL_WINDING_IN,
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

  /* A shift's abort: the continuation LEAVING keeps the nodes it
     unlinks into, top first, and the last node kept, which rests on
     the template's bottom. Borrowed: payload is the continuation's
     wrapper. NULL for every other goal, whose LEAVING frees. */
  KosContinuation *keep;
  KosDynNode *keep_tail;

  /* owned: the value or the exception; NULL for an abort to a prompt */
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
  /* the goal's storage is not its own: 1, kos_xfer_goal_run's
     caller's, which the goal leaves if it pauses; 2, its generator's
     (kos_xfer_goal_run_kept), which outlives it */
  uint8_t in_place;

  /* The rest is a winding goal's, phase KOS_GOAL_WINDING_OUT or
     KOS_GOAL_WINDING_IN, and is written and read for no other: the
     goals of a transfer pay nothing for it. */

  /* The generator's state, whose effect list and winders the goal
     works through, set when the goal is filled. piece_next is its
     place in the list and piece_entered the winders done; target is
     the part of the span cut off above the winder at dyn_top;
     shape.top the registers of the whole span; continuation_ref the
     generator. Kept here because dynchain does not know the
     generator's type, and the GEN is reached from the goal only by a
     walk. */
  KosGenState *gen_state;
  /* a winding in whose payload is an exception to raise at the yield;
     the payload alone does not say so, since a sent value may be an
     exception instance */
  uint8_t is_throw;
};


#define KosXferGoal_OUTER(g)     (((KosXferGoal *)(g))->outer)


/**
   Fill a goal in storage the caller provides, for kos_xfer_goal_run.
   target is an owned, already cloned segment (or NULL); seam is
   borrowed and must be on the live chain (or NULL for the root);
   payload is borrowed and a reference is taken; continuation_ref
   likewise. Cannot fail.

   The shape is cleared only when there is no target. A caller with a
   target sets the whole of shape to what its clone recorded, before or
   after this call, and the goal then owns it: this call leaves it as
   it stands.
*/
void kos_xfer_goal_init(
    KosXferGoal *goal,
    KosDynNode *target,
    KosDynNode *seam,
    PyObject *payload,
    KosOp *resume_pc,
    PyObject *continuation_ref,
    int is_cc_unwind);


/**
   Allocate a goal and fill it as kos_xfer_goal_init does, for
   kos_xfer_goal_push: a transfer that starts where its caller cannot
   run it. A caller with a target assigns the goal's shape on return.

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
   points, and its storage unless it is run in place. Does not touch
   outer.
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

   failure is 1 when the raise is an error, 0 for a continuation
   signal: a generator's winding in freed by an error raises the error
   from the exception its step threw (as kos_xfer_goal_thunk_died
   does), and one freed by a signal links nothing.
*/
void kos_xfer_goal_abandon_stepping(KosContext *ctx, int failure);


/**
   The raise walk unlinked node, which a goal waited on (it carried
   KOS_DYNF_GOAL_WAIT, with node_id its id read before the unlink): the
   thunk the goal waited for is dead. Free that goal, every goal
   started above it, and the outers its LEAVING voided, which its
   ATTACHING would have retired. The node is never dereferenced.
   Nothing to do when no goal waits on it (a mark a freed goal left
   behind). A waiter that is a generator's winding in whose step threw
   raises the pending error from the thrown exception, unless the error
   is that exception or names its own cause.
*/
void kos_xfer_goal_thunk_died(
    KosContext *ctx,
    const KosDynNode *node,
    uint32_t node_id);


/**
   Drive the active goal, and each outer it retires into, as far as the
   chain allows. A RAISE with ctx->terminal set is a foreign
   cc-unwind's signal, already prepared as a boundary raise and every
   goal abandoned: the caller returns it to the driver as it stands,
   without the raise ingress. Nothing else in a drive sets terminal.

   :return: EVAL_NORMAL when no goal remains or the active goal is
     waiting; EVAL_REPROLOGUE when a leave or enter installed a frame;
     EVAL_AWAIT with the awaitable when one suspended; EVAL_RAISE with
     PyErr set when a step failed, or on a boundary raise
*/
KosOpResult kos_xfer_goal_drive(KosContext *ctx);


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


/**
   Run a transfer now, from a goal in the caller's own storage that
   kos_xfer_goal_init filled. The goal is the active one for as long as
   it runs. A transfer that completes leaves nothing behind: no goal
   was allocated. One whose step pauses on a thunk frame or an
   awaitable, or fails, outlives the caller, so it is moved to storage
   of its own first and goes on as a pushed goal does. Either way the
   caller's storage is dead on return.

   The caller is the instruction or install the transfer starts from,
   with no exception pending and the frame it transfers from able to
   go: nothing may be written to that frame afterward.

   :return: EVAL_NORMAL when the transfer is done and the evaluator
     threads on from the live registers; EVAL_REPROLOGUE when a frame
     was installed or the prologue has a delivery or an outer goal to
     see to; EVAL_AWAIT with the awaitable when a step suspended;
     EVAL_RAISE with PyErr set when a step failed
*/
KosOpResult kos_xfer_goal_run(KosContext *ctx, KosXferGoal *goal);


/**
   kos_xfer_goal_run for a goal in storage that outlives it, a
   generator's winding goal (kos_dyn_gen_wind_out_begin): a goal that
   pauses stays where it is.
*/
KosOpResult kos_xfer_goal_run_kept(KosContext *ctx, KosXferGoal *goal);


/**
   kos_xfer_goal_run for the active goal, already pushed, which will
   drive (kos_xfer_goal_will_drive): the goal a generator's install
   pushed on a running context to enter the winders its yield left
   (kos_dyn_gen_install), run from the step once the step has handed
   it what it sends; and a goal whose frame has just returned onto the
   node it waits on, run from that return. The answers are
   kos_xfer_goal_run's, YIELD from a generator's yield whose last
   winder is left, and, for a cc-unwind goal, the boundary raise as
   kos_xfer_goal_drive answers it, a RAISE with ctx->terminal set.
*/
KosOpResult kos_xfer_goal_run_pushed(KosContext *ctx);


/**
   Free a goal that is not finished and is already unlinked from its
   context: kos_xfer_goal_free, after a winding goal has told its
   generator that the nodes its lists named are gone. Every site that
   gives up a goal it did not create and run to its end frees through
   here.
*/
void kos_xfer_goal_discard(KosXferGoal *goal);


/**
   Give the winding in just pushed on ctx what the step sends into its
   generator: send_val, or the exception instance throw_exc, both
   borrowed, or nothing. The goal keeps a reference to it as it is,
   with nothing chained and the error indicator untouched, and delivers
   it at the suspended yield once the last winder is entered; a throw
   is chained there to the generator's own handled exception, if it
   has one (KosContext_ThrowAtYield).
*/
void kos_xfer_goal_hand(
    KosContext *ctx,
    PyObject *send_val,
    PyObject *throw_exc);


/**
   The evaluator's entry on a context whose suspension event is
   KOS_SUSP_WIND: the winding in pushed there takes send_val or the
   thrown exception instance, unless the step handed it one already.
*/
void kos_xfer_goal_take_entry(
    KosContext *ctx,
    PyObject *send_val,
    PyObject *throw_exc);


#endif


// The end.
