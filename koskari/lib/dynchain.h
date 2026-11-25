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
   koskari/lib/dynchain.h

   Dynamic chain infrastructure for delimited continuations and
   dynamic-wind semantics. Provides the DynNode kinds (FRAME, PROMPT,
   WIND, TRY, FINALLY, LOOP, FOREACH, NS, COLLECT, CC, PARAM, WITH,
   GEN) that link into one parent chain.

   :author: Christopher O'Brien <obriencj@preoccupied.net>
   :license: GNU General Public License v3
*/


#ifndef KOSKARI_DYNCHAIN_H
#define KOSKARI_DYNCHAIN_H


#include <Python.h>
#include "exception.h"
#include <stdint.h>

#include "stack.h"


/* === DynNode kind enum === */


/**
  KosDynKind enumerates the types of nodes in the dynamic chain.

  KOS_DYN_FRAME    - Function activation record
  KOS_DYN_PROMPT   - Delimiter for delimited continuations (reset): the
                     frame on it is the reset's body or a shift's
                     handler, and ends it
  KOS_DYN_WIND    - Dynamic-wind extent with before/after hooks
  KOS_DYN_TRY      - try region
  KOS_DYN_FINALLY  - finally region
  KOS_DYN_LOOP     - loop region
  KOS_DYN_FOREACH  - foreach region
  KOS_DYN_NS       - Namespace scope (PUSH_NS/POP_NS)
  KOS_DYN_COLLECT  - Collector scope (PUSH_COLLECT/POP_COLLECT); push ns
                     on pop
  KOS_DYN_CC       - Undelimited continuation block (call/cc)
  KOS_DYN_PARAM    - Parameter bindings (parameterize extent)
  KOS_DYN_WITH     - Context manager (with / dyn-with); a dyn-with carries
                     KOS_DYN_CAP_WINDS, not a second kind
  KOS_DYN_GEN      - Generator node; intercepts handle(RETURN/RAISE),
                     denies handle(ABORT)
  KOS_DYN_UNKNOWN  - Sentinel value for unknown node kind
*/
typedef enum KosDynKind {
  KOS_DYN_FRAME = 0,
  KOS_DYN_PROMPT,
  KOS_DYN_WIND,
  KOS_DYN_TRY,
  KOS_DYN_FINALLY,
  KOS_DYN_LOOP,
  KOS_DYN_FOREACH,
  KOS_DYN_NS,
  KOS_DYN_COLLECT,
  KOS_DYN_CC,
  KOS_DYN_PARAM,
  KOS_DYN_WITH,
  KOS_DYN_GEN,

  KOS_DYN_UNKNOWN,
} KosDynKind;


/* Debug helper functions */
const char *kos_dynnode_kind_name(KosDynKind kind);


/* === DynNode flags === */


/**
  KosDynFlags enumerates the flags set on a node.

  KOS_DYNF_NONE        - No flags set
  KOS_DYNF_CLONED      - Node was cloned for continuation capture
  KOS_DYNF_GOAL_WAIT   - A transfer goal waits on this node's thunk
                         (its frontier); set by the wait, cleared when
                         the wait ends, never copied to a clone. A
                         goal's LEAVING that unlinks a node carrying it
                         voids the waiting goal (see xfer_goal.c)
*/
typedef enum KosDynFlags {
  KOS_DYNF_NONE        = 0x00,
  KOS_DYNF_CLONED      = 0x01,
  KOS_DYNF_GOAL_WAIT   = 0x02,
} KosDynFlags;


/**
   KosDynNode capability bits: what a node does and what it is, in one
   field. A node is found by a bit, never by its kind; the kind selects
   a node's own behaviour once the node is in hand. Set at creation and
   copied on clone. Two change at run time: CLEAR_LOOP and
   CLEAR_FOREACH take LOOP_REGION away, and a frame's OWNS_NS follows
   its code object and moves with it (a placeholder filled with code
   gains it, a cached function frame keeps it). FRAME, GEN, MAJOR and
   CC are never cleared while the node lives.

   Bits 0 to 3 are the skip bits (KOS_DYN_CAP_SKIP): a node carrying
   one has work to do when control crosses it, and is what a node's
   skip link names.
*/

/* may consume or deny a control transit: the fast first check before
   kos_dyn_handle. Carried by TRY, FINALLY, GEN and PROMPT. */
#define KOS_DYN_CAP_INTERCEPT                (1u << 0)

/* the node reacts when control returns into it from a frame it owns:
   the extent contract has work for it (WITH, WIND), and it carries an
   obligation slot (KosObligation), what it owes when it is unlinked --
   a body value, an exception stored while its after thunk or __exit__
   runs, or a return crossing a leave in flight; kos_dyn_node_ob is the
   accessor behind the cap. A node without it takes a callee's value on
   its segment and nothing more */
#define KOS_DYN_CAP_LEAVES                   (1u << 1)

/* a transfer runs this node: its leave on the way out, its enter on
   re-entry. Carried by WIND, PARAM, and a WITH pushed by dyn-with; the
   kind's own enter and leave do the work (kos_dyn_extent_enter) */
#define KOS_DYN_CAP_WINDS                    (1u << 2)

/* the node holds runtime-owned state outside itself, which leaves with
   it when its span rests: the parameter cells a PARAM swapped in, the
   handled exception a TRY or FINALLY installed on the thread */
#define KOS_DYN_CAP_EFFECT                   (1u << 3)

/**
   A plain pop refuses to unlink the node (kos_dyn_unlink_top with
   allow_barrier 0); the raise walk crosses it. Carried by PROMPT.
*/
#define KOS_DYN_CAP_BARRIER                  (1u << 4)

/* a LOOP or FOREACH region still running its body: the region a break
   or continue targets. CLEAR_LOOP and CLEAR_FOREACH take it away when
   the else clause starts, so a break or continue written there belongs
   to the enclosing loop. The one record of the region's enabled state. */
#define KOS_DYN_CAP_LOOP_REGION              (1u << 5)

/* may swallow an exception during its unwind. Carried by WITH. */
#define KOS_DYN_CAP_CATCH                    (1u << 6)

/* The namespace in this node's ns field is its own: a scope the node
   introduced (a frame with code, a let, a collect), as opposed to the
   fill of the live namespace every other pushed node carries. The
   reference is owned either way; the cap says whose scope it is, and
   the collector reaches an untracked scope through its owner alone. A
   native or code-less frame carries a fill and does not have it. */
#define KOS_DYN_CAP_OWNS_NS                  (1u << 7)

/* an activation record. Carried by FRAME. */
#define KOS_DYN_CAP_FRAME                    (1u << 8)

/* a generator span's root marker. Carried by GEN. */
#define KOS_DYN_CAP_GEN                      (1u << 9)

/* the node owns an operand segment. Carried by every kind but PROMPT,
   CC and GEN. */
#define KOS_DYN_CAP_MAJOR                    (1u << 10)

/* a call/cc block. Carried by CC. */
#define KOS_DYN_CAP_CC                       (1u << 11)

#define KOS_DYN_CAP_SKIP                                        \
  (KOS_DYN_CAP_INTERCEPT | KOS_DYN_CAP_LEAVES                   \
   | KOS_DYN_CAP_WINDS | KOS_DYN_CAP_EFFECT)


/* === KosIterRequest: the iteration request sidecar === */


/** What a bound iterator is, so the step calls the one entry it needs.
    Async wins over sync when a type offers both, as the probing order
    always had it. */
#define KOS_IREQ_KIND_NONE      0u  /* no iterator bound, or not one */
#define KOS_IREQ_KIND_PROVIDER  1u  /* KosIter provider; islots set */
#define KOS_IREQ_KIND_PY_ASYNC  2u  /* tp_as_async->am_anext */
#define KOS_IREQ_KIND_PY_SYNC   3u  /* tp_iternext */


/**
   A snapshot of the live registers at one chain top: the frame, the
   owning stack slot, and the namespace that a resync would derive by
   walking from top. Borrowed pointers, valid while the chain below
   top is unchanged. A generator's state keeps two, one per end of the
   flattened step cycle (see KosGenState).
*/
typedef struct KosLiveRegs {
  struct KosDynNode *top;
  struct KosDynNode *frame;
  KosSegment **block;
  PyObject *ns;
} KosLiveRegs;


/**
   A point where a transfer attaches its copy in pieces: a node the
   transfer runs (KOS_DYN_CAP_WINDS) at regs.top, the node resting on
   it in the copy at rest (NULL when it is the copy's top), and the
   registers live with regs.top at dyn_top. Borrowed: they belong to
   the copy until attached and to the chain after.
*/
typedef struct KosXferPiece {
  struct KosDynNode *rest;
  KosLiveRegs regs;
} KosXferPiece;


/**
   What one pass of kos_dyn_clone_segment met, so no caller walks the
   clone again: its bottom, how many frames it holds, and the registers
   live with its top at dyn_top (top.top is the clone's top). Borrowed
   pointers into the clone, but for pieces.

   A copy cloned for an attach also carries what attaching it needs:
   every frame back link and segment back inside the copy is set, the
   lowest frame and the lowest segment are left open for the facts
   below the copy, and the split points come with their registers. A
   register field no node of the copy supplies is NULL, and is the one
   below the copy; ns_open heads the run of nodes at the copy's bottom
   that carry no namespace. kos_dyn_settle_clone fills all of these
   from the seam once.
*/
typedef struct KosCloneShape {
  struct KosDynNode *bottom;
  unsigned frames;
  KosLiveRegs top;
  /* where the attach's skip-link repair starts: the clone's lowest
     skip-bit node, or its top when it has none. The skip links that
     reach below the clone are NULL until then (kos_dyn_repair_tail) */
  struct KosDynNode *tail;

  /* for an attach only */
  struct KosDynNode *open_frame;
  KosSegment *open_segment;
  struct KosDynNode *ns_open;
  KosXferPiece *pieces;        /* outermost first; owned */
  size_t piece_count;
  /* a TRY or FINALLY holding its handled exception was met, which
     the attach puts back on the thread (kos_dyn_install_regions) */
  int frozen;
} KosCloneShape;


/**
   Where the step in flight runs, recorded in KosGenState::busy: on a
   consumer's chain, or on the generator's own context (a Python
   adapter's drive). Only a step on its own context can be left there
   mid-await with nothing driving it.
*/
typedef enum KosGenBusy {
  KOS_GEN_IDLE = 0,
  KOS_GEN_BUSY_CONSUMER,  /* installed on a consumer's chain */
  KOS_GEN_BUSY_OWN,       /* on the generator's own context */
} KosGenBusy;


/* KosExcSlot states */
#define KOS_EXC_NONE      0  /* the slot holds nothing */
#define KOS_EXC_HANDLING  1  /* saved is what the owner's exception displaced */
#define KOS_EXC_FROZEN    2  /* saved is the owner's own, off the thread */


/**
   One handled exception's place in a node: a TRY or FINALLY handling
   a raise, or a WITH whose __exit__ is handling one. The owner's
   exception is the thread's handled exception while it is HANDLING,
   and saved is the handler-visible state that displaced, given back
   when the owner is done. FROZEN is the same owner at rest off the
   chain, in a generator's span or a continuation: the two are
   exchanged, so the thread shows what it showed before and saved
   holds the owner's own. kos_exc_slot_out and kos_exc_slot_in make
   that exchange and nothing else does. lent is three bytes of the
   record's own padding, the embedding payload's to use for its small
   fields.
*/
typedef struct KosExcSlot {
  KosExcState saved;
  uint8_t state;
  uint8_t lent[3];
} KosExcSlot;


/**
   An effect list: the nodes of a resting span whose effect is out
   (kos_dyn_effect_out), innermost first, the order they went out in.
   They go back in from the list's end, so each node finds the tables
   as the node outside it left them. The nodes are the span's own and
   borrowed. Two entries are held inline; a longer list moves to a heap
   array that the record keeps from then on, so a list that has reached
   its size allocates nothing more. cap is that array's capacity, or 0
   while the list is inline.
*/
typedef struct KosDynEffects {
  union {
    struct KosDynNode *pair[2];
    struct KosDynNode **heap;
  } items;
  uint16_t count;
  uint16_t cap;
} KosDynEffects;


/**
   A generator's state between installs, held by the generator object:
   the span resting out of any chain and what the next install needs
   to put it back. The span is the unstarted frame until the first
   install, then the chain from the yield's top down to the bottom
   node, the generator's root frame, whose parent is NULL while it
   rests. A GEN node marks the span on a chain only while it is
   installed, and refers back here; between installs it rests here
   too, so the next install reuses it.

   host is the consumer's registers taken at install, restored at the
   freeze while the GEN's parent is still host.top. frozen is the
   generator's registers taken at the freeze, applied at install while
   active is still frozen.top. A guard mismatch falls back to the chain
   rebuild.

   The span's frame count is not kept: its frames are numbered without
   a gap, so the count is the depth of frozen.frame, the span's top
   frame, less the bottom frame's, plus one.
*/
typedef struct KosGenState {
  struct KosDynNode *active;   /* the resting span's top; NULL while
                                  installed or spent */
  struct KosDynNode *bottom;   /* the span's bottom node, the root frame */
  struct KosDynNode *node;     /* the GEN node resting between installs,
                                  or NULL */
  uint16_t base_depth;         /* consumer depth the span's frames are
                                  numbered for; UINT16_MAX until the
                                  first frozen install */
  uint8_t started;             /* the first install has entered the frame;
                                  a resting span resumes a yield rather
                                  than entering fresh code */
  uint8_t busy;                /* KosGenBusy: a step is in flight, from
                                  the drive's start to the owner's
                                  finish; any nonzero value is busy */
  KosDynEffects effects;       /* what the freeze took out and the
                                  install puts back */
  /* A span whose yield left winders. The list in effects then holds
     the winders among the effects, and its length is wound_count, with
     effects.count 0: nothing in it is put back by the plain install.
     pieces are those winders, outermost first, each with the node
     resting on it and the registers live with it at dyn_top
     (kos_dyn_gen_wind_out_next); owned, and kept from one yield to the
     next. The array's allocation begins with the storage of the goal
     that leaves and enters them, one at a time, so a yield and a step
     allocate nothing. piece_count and wound_count are nonzero from the
     yield's start of the leaves (kos_dyn_gen_wind_out_begin) until the
     step that enters the winders again is done, and frozen.top is NULL
     while such a span rests, which is what sends the install to
     them. */
  KosXferPiece *pieces;
  uint16_t piece_count;
  /* the pieces the allocation holds, kept across yields, so a later
     yield with no more winders reallocates nothing */
  uint16_t piece_cap;
  /* the list's length while the winding owns it: effects.count stays 0
     then, so the plain install's one test of it puts nothing back */
  uint16_t wound_count;
  KosLiveRegs host;
  KosLiveRegs frozen;
} KosGenState;


/**
   The iteration request record: the FOREACH region's iterator and
   how it steps. Embedded in the FOREACH node; the step reads the
   iterator and its kind from here and nowhere else, and the step
   resume finishes it on delivery.
*/
typedef struct KosIterRequest {
  /* stepped iterator; strong ref while the request is set */
  PyObject *iter;
  /* iter's provider suite when iter is a KosIter provider, else NULL;
     bound with iter through kos_dyn_iter_request_bind_iter, so the
     step and delivery paths never re-derive it */
  struct KosIterSlots *islots;
  /* the value a provider step sends into iter, borrowed for the one
     fetch; NULL for a plain next, which every bind leaves it as */
  PyObject *send;
  /* KOS_IREQ_KIND_*: how the step drives iter, classified once at bind
     so no step probes the type again */
  uint8_t kind;
} KosIterRequest;


/**
   Bind iter (a reference the caller already owns, or NULL) as the
   request's stepped iterator and resolve its provider suite once. Every
   write of req->iter goes through here.
*/
void kos_dyn_iter_request_bind_iter(KosIterRequest *req, PyObject *iter);

/**
   Tell the request's provider its step is over: kos_iter_finish on
   the bound suite, a no-op for a plain iterator.
*/
void kos_dyn_iter_request_finish_req(KosIterRequest *req, int exhausted);


/**
   The answer of one fetch of an iterator's next element
   (kos_iter_fetch and the entries over it).
*/
typedef enum {
  KOS_IREQ_DELIVERED = 0, /* *out: the element (new ref) */
  KOS_IREQ_EXHAUSTED,     /* *out NULL; no exception set */
  /* the producer's frames installed; the element reaches the frame's
     resume */
  KOS_IREQ_CONTINUES,
  /* *out: the awaitable to suspend on (new ref); the element arrives
     at revive */
  KOS_IREQ_EXIT,
  KOS_IREQ_ERROR,         /* PyErr set */
} KosIterRequestStatus;


/**
   KosXferEvent says why a node is leaving the chain. Every walk and
   unlink passes one, and kos_dyn_unlink_top acts on the node's
   obligation by kind and event (deliver a stored value, re-raise a
   stored exception, release, or fault), so the event decides what a
   leaving node does with what it holds.
*/
typedef enum KosXferEvent {
  KOS_XFER_EVENT_NONE = 0,
  KOS_XFER_EVENT_RETURN,
  KOS_XFER_EVENT_RAISE,
  KOS_XFER_EVENT_BREAK,
  KOS_XFER_EVENT_CONTINUE,
  KOS_XFER_EVENT_TRANSFER,   /* a transfer is tearing the node down */
} KosXferEvent;


/* === Obligations === */


/**
   What a WIND or WITH node holds for after its leave completes: a value
   waiting for the revealed block, an exception waiting to resume
   propagating, or a return waiting to resume. Stored by the walk that
   stopped at the node (or by the node's own leave), released by the
   unlink once the node is gone, and discarded with the node when a
   raise or a transfer tears it down. One slot: a node never holds two,
   because a raise inside a thunk supersedes what the thunk was running
   for.
*/
typedef enum KosObligationKind {
  KOS_OB_NONE = 0,
  KOS_OB_VALUE,
  KOS_OB_EXCEPTION,
  KOS_OB_RETURN,
  /* a with's __exit__ consumed the raise: None stands in for the body
     value, whatever event brought the raise */
  KOS_OB_SWALLOWED,
} KosObligationKind;


typedef struct KosObligation {
  uint8_t kind;
  /* three bytes of the record's own padding, the embedding record's
     to use (see KosExtent) */
  uint8_t lent[3];
  union {
    PyObject *value;               /* KOS_OB_VALUE: owned */
    KosExcState exc;               /* KOS_OB_EXCEPTION: owned triple */
    struct {
      PyObject *value;             /* owned */
      uint8_t path;                /* KosPendingReturnKind */
    } ret;
  } u;
} KosObligation;


/* KosExtent phases */
#define KOS_EXTENT_IDLE      0  /* no call in flight: before the enter,
                                   or in the body once entered */
#define KOS_EXTENT_ENTERING  1  /* the enter call is in flight */
#define KOS_EXTENT_LEAVING   2  /* the leave call is in flight */
#define KOS_EXTENT_LEFT      3  /* terminal: every further leave is a
                                   no-op */


/**
   The state of an extent that runs user code on the way in and on the
   way out: a WIND's before and after segments, a WITH's __enter__ and
   __exit__. One machine runs both kinds (kos_dyn_extent_enter, _leave,
   _settle, _abort); the kind supplies the two calls. A call in flight
   has been started and has not returned: its frame is installed above
   the node, or it suspended on an awaitable. entered is 1 once the
   enter has completed, and says whether a leave has anything to run.
   ob is what the node holds for after its leave. The phase and
   entered live in the obligation's lent bytes, so the extent is no
   larger than the obligation.
*/
typedef struct KosExtent {
  KosObligation ob;
} KosExtent;

#define KosExtent_PHASE(e)    ((e)->ob.lent[0])
#define KosExtent_ENTERED(e)  ((e)->ob.lent[1])


/**
   KosHandleResult indicates how a node responded to handle(node, event).

   KOS_HANDLE_PASS     - node passed through; continue walk
   KOS_HANDLE_CONSUMED - node consumed; stop walk, caller delivers to handler
   KOS_HANDLE_ERROR    - error; return -1
*/
typedef enum KosHandleResult {
  KOS_HANDLE_PASS = 0,
  KOS_HANDLE_CONSUMED,
  KOS_HANDLE_ERROR,
} KosHandleResult;


/* === KosDynNode: tagged union for all chain node kinds === */


/**
   KosDynNode is the single structure for all dynamic chain nodes.
   Base fields (parent, kind, flags) are fixed; the union u holds
   kind-specific payloads. All node kinds share one allocation size
   for a single freelist. Use the accessor macros; do not touch u
   directly outside this header.
*/


/**
   The behaviour of a frame's resume, one static record per resume
   function, shared by every frame that sets that function as its
   resume. The state stays per frame in the resume_state slot; the kind
   holds nothing that varies between frames. fn is the resume itself, a
   KosThreadFn. clear runs at a teardown with no delivery coming (a
   frame freed or cached with the resume still set); NULL means the
   state is borrowed and its owner cleans up. clone runs at a
   continuation capture and at each application of one; NULL means the
   capture refuses by name. throw_kind is the kind that takes a throw
   into this halt, with the exception object as its value and the same
   state: a throw ingress sets it on the frame in place of this kind
   and stages the exception; NULL means a throw raises at the frame.
   Clean completion is the owner's business as ever.
*/
typedef struct KosResumeKind {
  const char *name;
  struct KosOpResult (KOS_PRESERVE_NONE *fn)(struct KosContext *ctx,
                                              void *state, PyObject *value);
  void (*clear)(void *state);
  void *(*clone)(void *state);
  const struct KosResumeKind *throw_kind;
  /* the delivery is settled before fn sees it: 1 for a kind that takes
     a call's result, 0 for a kind whose delivery is data -- a for-each
     element, a next's element, a delegate's yielded value, a transform
     source's element -- which reaches fn as it is. A for-each step's
     region clears its kind from the frame when the region leaves the
     chain with the step in flight. */
  int settle;
  /* GC traverse of the state, for a kind whose state owns a Python
     object (a yield-from phase owns its delegate); NULL when the state
     owns none. Every holder of a resume state -- a frame, a
     delimiter's displaced resume, a transfer's restore record --
     visits through it. */
  int (*traverse)(void *state, visitproc visit, void *arg);
} KosResumeKind;

/* A kind that takes a call's result: the delivery is settled first. */
#define KOS_RESUME_KIND(ident, label, fnp, clearp, clonep)              \
  static const KosResumeKind ident = {                                  \
    (label), (fnp), (clearp), (clonep), NULL, 1, NULL }

/* A kind that takes its delivery as it is. */
#define KOS_RESUME_KIND_AS_IS(ident, label, fnp, clearp, clonep)        \
  static const KosResumeKind ident = {                                  \
    (label), (fnp), (clearp), (clonep), NULL, 0, NULL }

/* An as-is kind with a throw kind: a throw into the halt runs throwp
   with the exception object as its value. */
#define KOS_RESUME_KIND_AS_IS_THROWABLE(ident, label, fnp, clearp,      \
                                        clonep, throwp)                 \
  static const KosResumeKind ident = {                                  \
    (label), (fnp), (clearp), (clonep), (throwp), 0, NULL }

/* An as-is kind whose state owns a Python object: its clear, clone
   and traverse keep that object's reference count and visibility to
   the cyclic collector in step. */
#define KOS_RESUME_KIND_AS_IS_OWNING(ident, label, fnp, clearp,         \
                                     clonep, traversep)                 \
  static const KosResumeKind ident = {                                  \
    (label), (fnp), (clearp), (clonep), NULL, 0, (traversep) }

#define KOS_RESUME_KIND_AS_IS_OWNING_THROWABLE(ident, label, fnp,       \
                                               clearp, clonep,          \
                                               traversep, throwp)       \
  static const KosResumeKind ident = {                                  \
    (label), (fnp), (clearp), (clonep), (throwp), 0, (traversep) }

/* GC traverse of a held resume state through its kind. */
static inline int kos_resume_traverse(
    const KosResumeKind *kind,
    void *state,
    visitproc visit,
    void *arg) {

  if (kind && kind->traverse)
    return kind->traverse(state, visit, arg);
  return 0;
}

/* The clone of a kind whose state is owned elsewhere and copied with
   the chain (a node's resume, whose state is NULL or the node
   itself): the state is handed on as it is. */
void *kos_resume_clone_shared(void *state);

/* The clone of a state that is one owned Python object: a new
   reference to the same object. */
void *kos_resume_clone_object(void *state);


/* The record a LOOP and a FOREACH region share: the instructions
   break and continue jump to, and the body's last continue value. Jump
   targets held by a node are records of the frame's code, which the
   frame keeps alive; code never relocates, so they stay valid for the
   life of the node and of any clone. */
typedef struct KosDynRegionFields {
  KosOp *break_pc;
  KosOp *continue_pc;
  PyObject *result;
} KosDynRegionFields;


struct KosDynNode {
  KosDynNode *parent;   /* enclosing node (toward root); reused as freelist link */
  uint8_t kind;         /* KosDynKind: discriminator for the payload */
  uint8_t flags;        /* KosDynFlags bitfield */
  uint16_t caps;        /* capability bits (KOS_DYN_CAP_*) */
  uint32_t node_id;     /* stable allocation ID; copied on clone for LCA matching */
  /* The nearest ancestor carrying a skip bit (KOS_DYN_CAP_SKIP), or
     NULL: set with the parent by kos_dynnode_link. A walk that only
     has work at such nodes follows this and re-tests caps on arrival,
     since a node may lose a skip bit. A segment off the chain keeps
     the links inside it; the ones that name a node outside it are
     repaired when it is attached. */
  KosDynNode *skip;
  /* Owned operand segment. Region kinds always own one (aimed at the
     embedded iseg at construction or push, heap after a spill); NULL
     marks a transfer bookmark (CC, PROMPT, GEN), which inherits the
     enclosing extent's segment. */
  KosSegment *stack;
  /* Namespace (reference owned): the node's own scope when it has
     KOS_DYN_CAP_OWNS_NS, otherwise a fill of the live namespace taken
     at push. NULL only in the placeholder-frame window and on unfilled
     splices. */
  PyObject *ns;

  union {
    struct {
      KosCode *code;      /* strong reference; NULL only for placeholder frames */
      KosOp *pc;          /* next instruction; NULL with no code */
      KosOp *acting_pc;   /* instruction currently being executed */
      uint32_t frame_flags;
      /* Frame nesting depth on this context's chain: the enclosing
         frame's depth + 1, the root frame at 1. Set at install, set by
         the clone for a continuation's copy, and renumbered whenever a
         generator's span is spliced in, so the frame
         depth test against the frame depth limit at install is one
         load and one compare. The limit defaults to 10000 and is
         settable up to KOS_FRAME_DEPTH_MAX, the width of this field. */
      uint16_t depth;
      /* A caller-owned one-frame cache (KosDynFrame **) this frame
         is stored into when the return path frees it; meaningful
         only while KOS_FRAME_RETURN_TO_CACHE is set, which
         KosNativeFrame_CacheOnFree alone does, for the return and for
         a resume declining a failure. */
      void *cache_slot;
      /* CPS consumer resume (the contract is in eval.h): the kind names
         the entry to invoke on the next value staged to this frame and
         the lifecycle of the state it receives; NULL means no resume is
         set. Set and cleared together with the state. The kind's
         name is what a capture-refusal message reports. */
      const KosResumeKind *resume_kind;
      void *resume_state;
      /* Owning KosFunction (strong) when this frame participates in the
         per-function frame cache; NULL opts out (clones, placeholder,
         code-less, and generator frames never set it).  Released --
         Py_CLEAR mandatory -- at free (after the cache offer) and at
         tail reuse. */
      PyObject *fn;
      /* Unwind cache: the frame displaced at install (borrowed; an
         ancestor on the chain, so it outlives this frame while
         installed). Set at install, and by the clone pass of a copy
         cloned for an attach; cleared at clone; a frozen GEN span
         keeps it, and the install rewrites the span's bottom frame's.
         NULL means
         fall back to the chain walk. While the
         frame is held in its function's cache this links the next
         cached frame instead (see KosFunction_CacheFrame), and the
         install that takes the frame back overwrites it. */
      struct KosDynNode *back_frame;
    } frame;
    struct {
      /* the tag a shift finds this prompt by, compared by identity;
         owned. NULL is the default tag, the one reset and shift use,
         and the one None names: None itself is never stored. */
      PyObject *tag;
    } prompt;
    struct {
      KosOp *resume_pc;  /* the instruction after POP_CC */
    } cc;
    struct {
      /* the code the PUSH_WIND is in (owned), and its row (borrowed,
         kept alive by the code), whose fields are the before and after
         asides, each NULL for none */
      KosCode *code;
      const KosExtra *row;
      /* the extent's phase, and in its obligation the body result at
         POP_WIND, or what a walk stored while the after-thunk runs */
      KosExtent extent;
    } wind;
    struct {
      KosOp *handler_pc;
      /* the raise the handler consumed; lent[0] is enabled */
      KosExcSlot exc;
    } try_;
    struct {
      KosOp *handler_pc;
      /* as for try_; lent[0] is enabled, lent[1] handling_raise, and
         lent[2] handling_control, a KosPendingReturnKind */
      KosExcSlot exc;
      KosExcState handling;   /* the raise being propagated through the body */
    } finally_;
    /* LOOP and FOREACH open with the same region record, so a break or
       continue reads either through KosDynRegion_*; the enabled state
       is the KOS_DYN_CAP_LOOP_REGION capability. */
    struct { KosDynRegionFields region; } loop;
    struct {
      KosDynRegionFields region;
      KosIterRequest req;    /* the loop's iterator and its in-flight step */
    } foreach;
    struct {
      PyObject *bindings;  /* PyDict: key -> value (what we bind; keys from ParamRef) */
      PyObject *saved;     /* PyDict: key -> value or SENTINEL_UNSET (filled at enter, used at restore; NULL if not yet entered) */
    } param;
    struct {
      PyObject *mgr;       /* context manager (owned) */
      PyObject *enter;     /* resolved enter callable (owned) */
      PyObject *exit;      /* resolved exit callable (owned) */
      PyObject *name;     /* binding name or NULL (owned) */
      KosOp *resume_pc;    /* the instruction after the with body (pop + 1) */
      /* the extent's phase, and in its obligation the exception
         __exit__ is handling, or what a walk stored while __exit__
         runs */
      KosExtent extent;
      /* __exit__ runs as a handler of the obligation's exception.
         lent[0] is self_bind, KOS_WITH_SELF_*: which of enter/exit is
         a plain function kept unbound, called with mgr first */
      KosExcSlot exc;
    } with;
    struct {
      /* The generator object whose span this node marks, borrowed. It
         is alive while the node is installed on a running chain: the
         record stepping it sits below the node on the chain or
         releases its context first, and the object's own context is
         cleared in its dealloc. A chain that outlives the owner, a
         consumer's context freed after the record let the generator
         go, frees the node without reading the owner or its state.
         The node exists from an install to the freeze or the
         teardown, and the chain frees it like any other. */
      PyObject *owner;
      /* the owner's state record, borrowed; valid while owner is */
      KosGenState *state;
    } gen;
  } u;

  /* Embedded operand-segment storage for region kinds: stack aims
     here at construction and spills to a heap segment on growth (the
     storage then sits dormant until node reuse). A transfer bookmark
     (CC, PROMPT, GEN) is a minor node, allocated without it and
     without the payloads of the other kinds: nothing may read past a
     minor node's own payload, and no code takes sizeof(KosDynNode)
     but the allocator. */
  KosInlineSegment iseg;
};


/**
   Aim a node's stack at its embedded inline segment, reset empty.
   For region-kind construction; cannot fail.
*/
static inline KosSegment *kos_dynnode_use_inline_segment(
        struct KosDynNode *node) {
  node->stack = kos_inline_segment_init(&node->iseg);
  return node->stack;
}


/**
   True when node introduces its own namespace extent: it carries
   KOS_DYN_CAP_OWNS_NS and one is attached.  Introspection-only:
   under the namespace owned-fill every pushed node carries an ns and
   the live-value question is answered by reading it, so retarget
   logic never needs this distinction -- only displays that report
   scope structure do.
*/
static inline int kos_dynnode_introduces_ns(const struct KosDynNode *node) {
  return (node->caps & KOS_DYN_CAP_OWNS_NS) && node->ns != NULL;
}

typedef struct KosDynNode KosDynFrame;


/**
   RAISE unwind walk: from dyn_top toward the innermost frame, offer
   handle(RAISE) to each node then pop, delivering to the first TRY or
   FINALLY that consumes and crossing frame and generator boundaries as
   the exception propagates.

   Like every walk it returns KosOpResult: EVAL_REPROLOGUE when a handler
   applied, EVAL_RAISE when no handler remains (PyErr set, surfacing raise
   prepared), EVAL_RESUME(NULL) when the live frame has a resume to take
   the failure, EVAL_AWAIT when an async unwind started, EVAL_RETURN when
   a resume-carrying frame answered the exception by completing itself
   (the caller applies the return from the live frame).

   Callers: process_raise (the unified RAISE ingress in eval.c).
   Implemented in dynchain.c (dyn_walk).
*/
KosOpResult kos_dyn_unwind_raise(KosContext *ctx);


typedef struct KosDynNode KosDynPrompt;
typedef struct KosDynNode KosDynWind;
typedef struct KosDynNode KosDynTry;
typedef struct KosDynNode KosDynFinally;
typedef struct KosDynNode KosDynLoop;
typedef struct KosDynNode KosDynForeach;
typedef struct KosDynNode KosDynNs;
typedef struct KosDynNode KosDynCollect;
typedef struct KosDynNode KosDynCC;
typedef struct KosDynNode KosDynParam;
typedef struct KosDynNode KosDynWith;
typedef struct KosDynNode KosDynGen;


/* === KosContinuation: captured delimited continuation === */


/**
   KosContinuation holds a captured delimited continuation.

   The captured_chain is a cloned segment of the dynamic chain from the
   capture point down through the prompt, whole frames up to and
   including the reset's body (delimited), or from above the CC block
   down to root (undelimited).

   The resume_pc is the instruction to resume at; the frame it
   resumes in is resolved from the chain at invoke time (the attached
   segment's innermost FRAME), never stored.

   A continuation is multi-shot: the chain and its stacks are cloned on
   each invocation.
*/

struct KosContinuation {
  KosDynNode *captured_chain;   /* cloned chain segment */
  KosOp *resume_pc;             /* the instruction to resume at */
  uint8_t delimited;            /* 1 = shift capture, 0 = call/cc */
  PyObject *token_ref;          /* KosEvaluatorToken (ownership marker) */
  unsigned frames;              /* frames in captured_chain */
};


#define KosContinuation_CAPTURED_CHAIN(k)  (((KosContinuation *)(k))->captured_chain)
#define KosContinuation_RESUME_PC(k)       (((KosContinuation *)(k))->resume_pc)
#define KosContinuation_DELIMITED(k)       (((KosContinuation *)(k))->delimited)
#define KosContinuation_TOKEN_REF(k)       (((KosContinuation *)(k))->token_ref)
#define KosContinuation_FRAMES(k)          (((KosContinuation *)(k))->frames)


/* === DynNode allocation and lifecycle === */


/**
   Allocate a new frame node.

   :param parent: parent node in dynamic chain (or NULL for root)
   :param code: bytecode object (borrowed reference)
   :param ns: namespace (reference is stolen)

   :return: new frame node, or NULL on error
*/
KosDynFrame *kos_dynframe_new(
    KosDynNode *parent,
    KosCode *code,
    PyObject *ns);


/**
   Clone a frame node for continuation capture.

   Clones the frame's stack using DeepCopy. Does NOT clone
   the parent chain; use kos_dyn_clone_segment for that.

   :param frame: frame to clone

   :return: cloned frame, or NULL on error
*/
KosDynFrame *kos_dynframe_clone(KosDynFrame *frame);


/**
   Free a frame node and release its resources.

   Releases code; the segment and namespace release in the freelist
   return.
   Does NOT free the parent chain.

   :param frame: frame to free (may be NULL)
*/
void kos_dynframe_free(KosDynFrame *frame);


/**
   Allocate a new prompt node.

   :param parent: parent node in dynamic chain
   :param tag: the prompt's tag, matched by identity, or NULL for the
      default tag (never None); a reference is taken

   :return: new prompt node, or NULL on error
*/
KosDynPrompt *kos_dynprompt_new(KosDynNode *parent, PyObject *tag);


/**
   Free a prompt node.

   :param prompt: prompt to free (may be NULL)
*/
void kos_dynprompt_free(KosDynPrompt *prompt);


/**
   Allocate a new wind node.

   :param parent: parent node in dynamic chain
   :param code: the code holding the PUSH_WIND (borrowed; the node
     takes its own reference), or NULL with row
   :param row: the PUSH_WIND's row, carrying the before and after asides,
     or NULL for a wind with neither

   :return: new wind node, or NULL on error
*/
KosDynWind *kos_dynwind_new(
   KosDynNode *parent,
   KosCode *code,
   const KosExtra *row);


/**
   Clone a wind node for continuation capture.

   :param wind: wind node to clone

   :return: cloned wind node, or NULL on error
*/
KosDynWind *kos_dynwind_clone(KosDynWind *wind);


/**
   Free a wind node.

   :param wind: wind to free (may be NULL)
*/
void kos_dynwind_free(KosDynWind *wind);


/**
   Allocate a new region node. TRY and FINALLY take the handler
   instruction; LOOP and FOREACH take the break and continue targets.

   :param parent: parent node in dynamic chain
   :param handler_pc: handler jump target (TRY, FINALLY)
   :param break_pc: break/exit jump target (LOOP, FOREACH)
   :param continue_pc: continue jump target (LOOP, FOREACH)

   :return: new region node, or NULL on error
*/
KosDynTry *kos_dyntry_new(KosDynNode *parent, KosOp *handler_pc);
KosDynFinally *kos_dynfinally_new(KosDynNode *parent, KosOp *handler_pc);
KosDynLoop *kos_dynloop_new(
    KosDynNode *parent,
    KosOp *break_pc,
    KosOp *continue_pc);
KosDynForeach *kos_dynforeach_new(
    KosDynNode *parent,
    KosOp *break_pc,
    KosOp *continue_pc);


/**
   Clone a region node for continuation capture.

   :param region: region to clone

   :return: cloned region, or NULL on error
*/
KosDynTry *kos_dyntry_clone(KosDynTry *node);
KosDynFinally *kos_dynfinally_clone(KosDynFinally *node);
KosDynLoop *kos_dynloop_clone(KosDynLoop *node);
KosDynForeach *kos_dynforeach_clone(KosDynForeach *node);


/**
   Free a region node.

   :param region: region to free (may be NULL)
*/
void kos_dyntry_free(KosDynTry *node);
void kos_dynfinally_free(KosDynFinally *node);
void kos_dynloop_free(KosDynLoop *node);
void kos_dynforeach_free(KosDynForeach *node);


/**
   Free a single DynNode of any kind.

   Dispatches to the appropriate type-specific free function.
   Does NOT free the parent chain.

   :param node: node to free (may be NULL)
*/
void kos_dynnode_free(KosDynNode *node);


/**
   Free an entire chain of DynNodes from node down to (but not
   including) stop.

   :param node: top of chain to free
   :param stop: stop at this node (not freed), or NULL to free all
*/
void kos_dynchain_free(KosDynNode *node, const KosDynNode *stop);


/**
   Clone a chain segment from top down to (but not including) stop.

   Each node in the segment is cloned and linked, in one pass from top
   down. The clone's bottom node's parent is set to stop (or NULL if
   stop is NULL); a caller that keeps the clone off the chain unlinks
   it through shape->bottom.

   :param top: top of segment to clone
   :param stop: stop at this node (not cloned), or NULL to clone all
   :param fresh_ids: non-zero for a delimited continuation's template
      and for each copy attached from it: every clone is a new instance,
      its id minted on first read. Zero for call/cc, whose clones keep
      their source's id so an attached clone counts as the same extent as
      the node it was captured from
   :param for_attach: non-zero for a copy an invoke attaches: the pass
      also numbers its frames, the topmost at top_depth and each one
      below it one less, as they stand once attached. Zero for a
      template, whose frames keep their source's depths
   :param top_depth: the depth the copy's topmost frame takes
   :param shape: filled with what the pass met; see KosCloneShape

   :return: cloned chain top, or NULL on error (shape is then empty)
*/
KosDynNode *kos_dyn_clone_segment(
    KosDynNode *top,
    KosDynNode *stop,
    int fresh_ids,
    int for_attach,
    unsigned top_depth,
    KosCloneShape *shape);


/**
   Complete a copy cloned for an attach from the facts below it: the
   live registers, read with the seam the copy goes on at dyn_top. The
   lowest frame's back link and the lowest segment's back take the
   live frame and block, the run of nodes at the bottom with no
   namespace takes the live one, and every register the copy left
   open takes the live value. Runs once, before the first piece is
   attached.
*/
void kos_dyn_settle_clone(KosContext *ctx, KosCloneShape *shape);


/**
   Make regs the live registers: dyn_top is regs->top, and the frame,
   block and namespace are loaded as a chain walk from it would find
   them, flushing the outgoing frame's pc.
*/
void kos_live_regs_apply(KosContext *ctx, const KosLiveRegs *regs);


/* === Unified dynamic chain API === */


/**
   Push a node onto the context's dynamic chain.  Caller must have
   allocated the node and set its parent.  Calls install handler if
   the type has one.  Updates dyn_top.

   :param ctx: execution context
   :param node: node to push (parent already set)

   :return: 0 on success, -1 on error
*/
int kos_dyn_push(KosContext *ctx, KosDynNode *node);


/**
   Flush live PCs, set dyn_top to new_top, retarget live fields on ctx.

   Use for pops, splices, gen install, and any dyn_top change that is not
   kos_dyn_push. Internally captures the previous dyn_top and calls
   kos_dyn_resync_after_pop (incremental parent pop or full rebuild).
   Ordinary pushes must use kos_dyn_push instead.
*/
void kos_dyn_commit_dyn_top(KosContext *ctx, KosDynNode *new_top);


/**
   Full chain walk: recompute live frame, block, and namespace on ctx from
   dyn_top.

   Call from outside the dynchain TU when eval lifecycle or opcodes must refresh
   live fields after a non-standard chain edit (koskari_eval entry, tail
   koscall, stack assigned on dyn_top after push). Inside dynchain.c, xfer
   INSTALL/PUSH, gen install fallback, and kos_dyn_resync_after_pop when the
   edit is not a single-head parent pop use this instead of incremental retarget.
   Do not call on every opcode; ordinary pushes and pops go through
   kos_dyn_push and kos_dyn_commit_dyn_top.
*/
void kos_dyn_resync_chain(KosContext *ctx);


/* === Dynamic chain context operations === */


/**
   Push a new frame onto the context's dynamic chain.

   Creates a new frame and sets it as dyn_top and the live frame.

   :param ctx: execution context
   :param code: bytecode object (borrowed)
   :param ns: namespace (reference is stolen)

   :return: new frame, or NULL on error
*/
KosDynFrame *kos_dyn_push_frame(KosContext *ctx, KosCode *code, PyObject *ns);

/**
   Whether ns can be the namespace a frame runs code in: a code whose
   root scope uses fast slots needs a function or collector namespace
   holding every slot it names (KosCode_FAST_NEED). The code's own spec
   was validated at finalize; this is the namespace that actually runs,
   which an Expression, a Function whose formals dedup, or a hand-built
   frame can make smaller. Returns 0, or -1 with RuntimeError set.
*/
int kos_code_fits_namespace(KosCode *code, PyObject *ns);

/* kos_dyn_push_frame without the frame-room check; the caller has run
   kos_eval_check_frame_room for one frame already. */
KosDynFrame *kos_dyn_push_frame_unchecked(
    KosContext *ctx,
    KosCode *code,
    PyObject *ns);

/* The frame of an aside: one of no arguments that runs aside, a
   code, over ns, with no function around it; ns is consumed, and
   with owns_ns set the frame introduces it. See
   dynchain/dyn_frame.c. */
KosDynFrame *kos_dynframe_new_aside(
    KosDynNode *parent,
    KosDynFrame *base,
    KosCode *aside,
    PyObject *ns,
    int owns_ns);

/* Push a frame that runs aside over ns, the namespace live at the
   instruction that runs it. */
KosDynFrame *kos_dyn_push_aside(
    KosContext *ctx,
    KosCode *aside,
    PyObject *ns);


/**
   Push a placeholder frame with code=NULL and ns=NULL for INSTALL to configure.

   :return: new frame, or NULL on error
*/
KosDynFrame *kos_dyn_push_frame_empty(KosContext *ctx);


/**
   Push a frame for a native program: no code, and the namespace slot
   holds an owned reference to the shared empty namespace -- a native
   frame reads and stores no name, so the slot only keeps the live
   register non-NULL over the program's extent, and nothing is visible
   or storable through it.

   :return: new frame, or NULL on error
*/
KosDynFrame *kos_dyn_push_frame_native(KosContext *ctx);

/**
   Count the FRAME nodes on the segment from top down to but excluding
   base (NULL: the whole chain).
*/
unsigned kos_dyn_frame_count(KosDynNode *top, KosDynNode *base);

/**
   Renumber the frames on the segment from top down to but excluding
   base so the lowest sits at base_depth + 1 and each frame above it is
   one deeper, for a caller that has already counted the segment's
   frames (count from kos_dyn_frame_count over the same bounds). Called
   after a segment carrying frames is spliced onto a chain; the caller
   has already established that the result fits under the frame depth
   limit, which never exceeds KOS_FRAME_DEPTH_MAX.
*/
void kos_dyn_renumber_frames_counted(
    KosDynNode *top,
    KosDynNode *base,
    unsigned base_depth,
    unsigned count);


/**
   Offer a detached native-program frame to a holder's one-frame cache:
   strip it to an inert allocation -- program, state, stack contents,
   and links all cleared, so the cached frame holds no references of
   any kind -- and store it in *cache when the slot is empty. A holder
   that caches frames this way cannot form a reference cycle through
   its cache, and frees a cached frame with kos_dynframe_free.

   :return: 1 when cached; 0 when the caller must free the frame
*/
int kos_dynframe_cache_inert(KosDynFrame *frame, KosDynFrame **cache);


void kos_dynframe_reset_for_tail(KosDynFrame *frame);


/**
   A tail call's install: ready, a bound root frame
   (KosFunction_ReadyFrameVector), takes the live frame's place on the
   chain, at its depth, under its parent and with its demand bit, and
   the live frame is freed, which clears its operand block (the call's
   window with it) and offers it to its function's cache. The caller
   has torn the chain down to the live frame (kos_dyn_teardown_to_frame)
   and bound the window into ready before this. The live registers
   name ready on return.
*/
void kos_dyn_swap_frame_for_tail(KosContext *ctx, KosDynFrame *ready);


/**
   Clear a resume that will get no delivery: the kind's clear, when it
   has one, releases what the state holds, then the kind and state are
   nulled. The one rule behind free, cache, tail reset and a transfer
   restore writing over the live frame's resume
*/
void kos_dynframe_clear_resume(KosDynFrame *frame);

/* Drop discardable extent nodes (NS, LOOP, FOREACH) above frame before
   tail-call frame reuse; errors on any other open node kind. The
   out-of-line half of kos_dyn_teardown_to_frame (eval.h). */
int kos_dyn_teardown_extents_to_frame(KosContext *ctx, KosDynFrame *frame);


/**
   Ready a generator's state record for an unstarted generator resting
   on frame: the span is the frame alone, its parent NULL.
*/
void kos_gen_state_init(KosGenState *state, KosDynFrame *frame);


/**
   Free what a generator's state holds: the resting span and the
   resting GEN node. A span installed on a chain is that chain's.
*/
void kos_gen_state_clear(KosGenState *state);


/**
   The GEN node marking owner's span on a chain: the one resting in
   the state, or a new one. The install links it under the consumer
   and the span above it.

   :param owner: the generator object (borrowed)
   :param state: the owner's state record (borrowed)

   :return: a GEN node with parent=NULL, or NULL on error
*/
KosDynGen *kos_dyngen_new(PyObject *owner, KosGenState *state);


/**
   Rest a GEN node in its state once the span has left the chain: the
   node waits there for the next install. The freeze and a refused
   install call it.
*/
void kos_dyngen_rest(KosDynGen *gen);


/**
   Free a GEN node: return it to the freelist, touching neither its
   owner nor the owner's state, which may already be gone. The chain
   free reaches it through kos_dynnode_free; the state's clear frees a
   resting one, taking it out of the state first.
*/
void kos_dyngen_free(KosDynGen *gen);


/**
   Install a resting, started span onto the context's chain: a new GEN
   node under the chain's top, the span above it, the live registers
   retargeted to the span's top, and Py_None pushed as the suspended
   yield's placeholder. Requires state->active and state->started.

   A span whose yield left winders (state->piece_count) is installed
   up to its outermost winder only, with a goal pushed that enters each
   winder and attaches what rests on it (KOS_GOAL_WINDING_IN). The rest
   of the span is the goal's until then, no placeholder is pushed, and
   the goal delivers the sent value or the throw after the last enter
   (kos_xfer_goal_hand, kos_xfer_goal_take_entry). A context that is
   not running is left with KOS_SUSP_WIND as its suspension event, so
   its next entry hands the goal what it carries.

   :param ctx: execution context (may have empty or non-empty chain)
   :param owner: the generator object the GEN node refers to
   :param state: the owner's state record

   :return: 0 with the whole span installed, 1 with the goal pushed,
       -1 on error with the span resting
*/
int kos_dyn_gen_install(KosContext *ctx, PyObject *owner, KosGenState *state);


/**
   Undo an install whose caller failed after the splice, before the
   installed frame ran: the span goes back to rest in the state through
   the freeze, and the context's registers return to the consumer, so
   the generator stays suspended where it was and the next drive finds
   a resting span. A pending error is kept across the freeze. No-op
   when the span is not spliced.

   :param ctx: the context the span was installed on
   :param state: the owner's state record
*/
void kos_dyn_gen_unsplice(KosContext *ctx, KosGenState *state);


/**
   Bootstrap install: graft the chain headed by chain_head onto the
   consumer ctx. A chain whose root is a bare frame, the unstarted
   generator's, gets a new GEN node under it and marks the state
   started; a chain already rooted on a GEN node, one in flight moved
   from a context's dyn top, is relinked under the consumer. The
   caller has taken chain_head out of its resting place.
*/
int kos_dyn_gen_install_bootstrap(
    KosContext *ctx,
    PyObject *owner,
    KosGenState *state,
    KosDynNode *chain_head);


/**
   Unwind an exhausted gen from the chain. Call when gen's frame returns
   (parent is GEN). Frees the frame and the GEN node, and restores the
   consumer.
*/
int kos_dyn_teardown_gen_exhausted(KosContext *ctx);


/**
   Pop the top frame from the context's dynamic chain.

   Frees the frame and updates dyn_top to the frame's parent.
   Updates the live frame to the new topmost frame.

   :param ctx: execution context

   :return: 0 on success, -1 on error
*/
int kos_dyn_pop_frame(KosContext *ctx);


/**
   Push a prompt delimiter onto the dynamic chain. It owns no operands:
   the frame called on it, a reset's body, holds the region's.

   :param ctx: execution context
   :param tag: the prompt's tag, matched by identity, or NULL for the
      default tag (never None); consumed, on failure too

   :return: new prompt, or NULL on error
*/
KosDynPrompt *kos_dyn_push_prompt(KosContext *ctx, PyObject *tag);


/**
   Pop a prompt from the dynamic chain.

  Uses barrier-crossing pop policy with shared uninstall machinery.

   :param ctx: execution context
   :param prompt: prompt to pop (must be dyn_top)

  :return: 0 on success, -1 on error
*/
int kos_dyn_pop_prompt(KosContext *ctx, KosDynPrompt *prompt);


/**
   Push a CC (call/cc) block onto the dynamic chain.

   :param ctx: execution context
   :param resume_pc: the instruction to resume at when the continuation
      is invoked

   :return: the new CC node, or NULL on error
*/
KosDynCC *kos_dyn_push_cc(KosContext *ctx, KosOp *resume_pc);


/**
   Pop a CC block from the dynamic chain.

   :param ctx: execution context
   :param cc: the CC node to pop (must be at dyn_top)
*/
int kos_dyn_pop_cc(KosContext *ctx, KosDynCC *cc);


/**
   The nearest prompt carrying tag that an abort can reach.

   :param ctx: execution context
   :param tag: the tag sought, matched by identity, or NULL for the
     default tag (never None); borrowed

   :return: the prompt, borrowed, or NULL with KoskariPromptNotFound or
     KoskariPromptUnreachable set
*/
KosDynPrompt *kos_abort_prompt(KosContext *ctx, PyObject *tag);


/**
   Leave everything up to a prompt and run an aside above it.

   The live frame stands after the instruction that aborts. A frame is
   made for aside over ns, linked to nothing. The caller chooses ns:
   the live namespace, which belongs to a frame the abort leaves, so
   the caller reveals it (KosNamespace_TrackChain) and that frame's
   release does not reclaim it; or one built from the aside's spec
   over the live namespace, which the new frame introduces. The
   transfer leaves everything from dyn_top down to the prompt, the
   live frame and the reset's body frame included, which holds the
   region's operands until the abort can no longer be cancelled: a
   dyn-wind after thunk or a with's __exit__ that raises on the way
   out abandons the transfer, the aside's frame is released, and the
   region carries on where the raise is caught. Once the prompt is at
   dyn_top the frame is attached above it and runs the aside, with
   payload, when there is one, pushed on its stack. Its return takes
   the prompt with it and is the reset's return. A shift or an abort
   made in the aside finds the same prompt.

   Runs the transfer at once (kos_xfer_goal_run), its target the new
   frame; the caller relays the result.

   :param ctx: execution context
   :param prompt: the prompt to abort to
   :param aside: the aside to run above it
   :param ns: the namespace its frame runs over; consumed, on failure
     too. NULL, with PyErr set, is a namespace the caller failed to
     build: payload is released and the result is EVAL_RAISE
   :param owns_ns: nonzero when ns was built for the aside, zero when
     it is the live namespace
   :param payload: a value for the aside's stack, or NULL; consumed,
     on failure too
   :param keep: the continuation kos_shift_capture began, which the
     nodes the transfer leaves are kept into, or NULL to free them;
     borrowed, and kept alive by payload

   :return: EVAL_NORMAL when the aside's frame is live above the
     prompt and the evaluator threads on in it; otherwise the result
     to relay: EVAL_REPROLOGUE, EVAL_AWAIT, or EVAL_RAISE with an
     exception set
*/
KosOpResult kos_abort_to_prompt(
    KosContext *ctx,
    KosDynPrompt *prompt,
    KosCode *aside,
    PyObject *ns,
    int owns_ns,
    PyObject *payload,
    KosContinuation *keep);


/**
   Push a wind extent onto the dynamic chain.

   :param ctx: execution context
   :param code: the code holding the PUSH_WIND (borrowed)
   :param row: the PUSH_WIND's row, carrying the before and after asides

   :return: new wind node, or NULL on error
*/
KosDynWind *kos_dyn_push_wind(
   KosContext *ctx,
   KosCode *code,
   const KosExtra *row);


/**
   Enter a wind fresh from kos_dyn_push_wind: phase IDLE, not entered.
   Runs the before segment, or with none marks the wind entered. Any
   other wind enters through kos_dyn_extent_enter.

   :return: EVAL_NORMAL, EVAL_REPROLOGUE with the segment's frame
     installed, or EVAL_RAISE
*/
KosOpResult kos_dyn_wind_call_enter(KosContext *ctx, KosDynWind *node);


/**
   Push a region onto the dynamic chain. TRY and FINALLY take the
   handler instruction; LOOP and FOREACH take the break and continue
   targets.

   :param ctx: context
   :param handler_pc: handler jump target (TRY, FINALLY)
   :param break_pc: break/exit jump target (LOOP, FOREACH)
   :param continue_pc: continue jump target (LOOP, FOREACH)

   :return: new region, or NULL on error
*/
KosDynTry *kos_dyn_push_try(KosContext *ctx, KosOp *handler_pc);
KosDynFinally *kos_dyn_push_finally(KosContext *ctx, KosOp *handler_pc);
KosDynLoop *kos_dyn_push_loop(
    KosContext *ctx,
    KosOp *break_pc,
    KosOp *continue_pc);
KosDynForeach *kos_dyn_push_foreach(
    KosContext *ctx,
    KosOp *break_pc,
    KosOp *continue_pc);


/* Uninstall hook: a step still in flight is finished and the loop
   frame's resume cleared. Callers: kos_dyn_uninstall_handler. */
void kos_dyn_foreach_unwind(KosContext *ctx, KosDynForeach *node);



/* === The generator lookup (dyn_gen.c) and the FOREACH request (dyn_iter_node.c) === */


/**
   The GEN node of the generator the running frame belongs to: the
   nearest GEN walking outward from the frame, or NULL when the frame
   is not in a generator. It crosses every node: a caller that wants the
   chain's generators starts from it.
*/
KosDynGen *kos_dyn_find_gen(KosContext *ctx);


/**
   The GEN node a yield in the running frame suspends: the nearest GEN
   walking outward from the frame. NULL with a RuntimeError set when
   the chain ran out. Callers: kos_yield_deliver and the trampoline's
   YIELD arm, which then freeze the node through kos_dyn_gen_freeze.
*/
KosDynGen *kos_dyn_yield_gen(KosContext *ctx);


/**
   Pop a region from the dynamic chain. The caller passes dyn_top as it
   read it. kos_dyn_pop_try refuses a NULL or a node of another kind;
   kos_dyn_pop_finally takes a node its caller has already tested.

   :param ctx: execution context
   :param node: the region to pop, which is dyn_top
*/
int kos_dyn_pop_try(KosContext *ctx, KosDynTry *node);
int kos_dyn_pop_finally(KosContext *ctx, KosDynFinally *node);
int kos_dyn_pop_loop(KosContext *ctx, KosDynLoop *node);
int kos_dyn_pop_foreach(KosContext *ctx, KosDynForeach *node);


/**
   Push a namespace scope node (for PUSH_NS). The node owns its active
   namespace in the generic node namespace slot.

   :param ctx: execution context
   :param ns: active namespace for this NS scope (new ref)

   :return: new KosDynNs node, or NULL on error
*/
KosDynNs *kos_dyn_push_ns(KosContext *ctx, PyObject *ns);


/**
   Pop a namespace scope node.

   :param ctx: execution context
   :param node: NS node to pop (must be dyn_top)
*/
int kos_dyn_pop_ns(KosContext *ctx, KosDynNs *node);


/**
   Push a collector scope node (for PUSH_COLLECT). The node owns its
   collector namespace in the generic node namespace slot.

   :param ctx: execution context
   :param ns: collector namespace active in this scope (new ref)

   :return: new KosDynCollect node, or NULL on error
*/
KosDynCollect *kos_dyn_push_collect(KosContext *ctx, PyObject *ns);


/**
   Pop the collector scope node at the top of the chain and hand out
   the namespace it collected, revealed for the value stack.

   :param ctx: context
   :param node: COLLECT node to pop (must be dyn_top)
   :param out_ns: the collected namespace (new ref); untouched on error
   :return: 0, or -1 with an exception set when node is not the top of
     the chain or not a COLLECT node
*/
int kos_dyn_pop_collect(
    KosContext *ctx,
    KosDynCollect *node,
    PyObject **out_ns);


/**
   Push a parameter extent onto the dynamic chain from a bindings dict.

   Creates a PARAM node wrapping each value in a fresh cell, runs enter
   (swap the cells into the context's shadow table, saving what they
   displace), and pushes.

   :param ctx: evaluator state
   :param bindings: PyDict mapping names to values (borrowed)

   :return: new KosDynParam node, or NULL on error
*/
KosDynParam *kos_dyn_push_param_dict(KosContext *ctx, PyObject *bindings);


/**
   Pop a parameter extent from the dynamic chain.

   Restores the displaced cells, updates dyn_top, and frees the node.

   :param ctx: evaluator state
   :param node: PARAM node to pop (must be dyn_top)
*/
int kos_dyn_pop_param(KosContext *ctx, KosDynParam *node);


/**
   Push a provisional WITH node (phase PENDING_ENTER_AWAIT) before calling
   enter. winds is KOS_DYN_CAP_WINDS for a dyn-with, which a transfer
   runs, or 0 for a plain with, which is local to its body. self_bind
   carries KOS_WITH_SELF_ENTER and KOS_WITH_SELF_EXIT for the protocol
   methods that were resolved on the type as plain functions and are
   called with mgr as their first argument; the others are already
   bound or need no receiver.

   :return: new KosDynWith node, or NULL on error
*/
KosDynWith *kos_dyn_push_with_provisional(
    KosContext *ctx,
    PyObject *mgr,
    PyObject *enter,
    PyObject *exit,
    uint8_t self_bind,
    PyObject *name,
    KosOp *resume_pc,
    uint32_t winds);


/**
   Enter a WITH fresh from kos_dyn_push_with_provisional: phase IDLE,
   not entered. Calls __enter__ and binds its result when it returns
   in hand. Any other WITH enters through kos_dyn_extent_enter.

   :return: EVAL_NORMAL, EVAL_REPROLOGUE with __enter__'s frame
     installed, EVAL_AWAIT, or EVAL_RAISE
*/
KosOpResult kos_dyn_with_call_enter(KosContext *ctx, KosDynWith *node);


/**
   Clone a WITH node for continuation capture. Copies mgr, enter, exit,
   name with new references and the caps from src. A dyn-with's clone is
   not entered, for a transfer to enter; a plain with's clone keeps the
   source's entered state, so the copy runs __exit__ when it exits.

   :return: cloned node, or NULL on error
*/
KosDynWith *kos_dyn_with_clone(KosDynWith *src);


/**
   Pop a with extent from the dynamic chain.

   :param ctx: execution context
   :param node: WITH node to pop (must be dyn_top)
*/
int kos_dyn_pop_with(KosContext *ctx, KosDynWith *node);


/**
   Unlink dyn_top: the one site a node leaves the chain through.

   1. A KOS_DYN_CAP_LEAVES node is asked to leave
      (kos_dyn_extent_leave); any answer but NORMAL returns unchanged,
      with the node still on the chain -- the caller stores what it
      carries and asks again later. Under KOS_XFER_EVENT_TRANSFER a
      node without KOS_DYN_CAP_WINDS is local and is not asked: a call
      of its in flight is discharged (kos_dyn_extent_abort) and nothing
      else of it runs.
   2. The synchronous per-kind teardown runs (PARAM restore,
      TRY/FINALLY exception-state restore).
   3. The node's obligation is detached.
   4. The parent becomes dyn_top; the node is freed.
   5. The obligation is acted on, by its kind and by ev. Under
      KOS_XFER_EVENT_NONE (a plain pop, or the hook finishing a leave):
      a VALUE goes onto the revealed block, an EXCEPTION is re-raised,
      a RETURN is a caller error (only kos_dyn_settle_top can receive
      one). Under KOS_XFER_EVENT_RAISE: an EXCEPTION is chained as
      __context__ of the exception in flight, everything else is
      discarded. Under any other event a walk or a transfer crossed
      the node: everything is discarded. A SWALLOWED (a with's
      __exit__ consumed the raise) puts None on the revealed block
      under every event.

   :param ev: why the node is leaving; KOS_XFER_EVENT_NONE for a plain pop
   :param allow_barrier: 0 refuses a KOS_DYN_CAP_BARRIER node with
     EVAL_RAISE; 1 crosses it
   :return: EVAL_NORMAL when the node is gone; EVAL_REPROLOGUE or
     EVAL_AWAIT when its leave is in flight and it stays; EVAL_RAISE
*/
KosOpResult kos_dyn_unlink_top(
    KosContext *ctx,
    KosXferEvent ev,
    int allow_barrier);


/**
   Unlink dyn_top and keep it: the node leaves the chain as a transfer
   has it leave, and is handed to the caller instead of freed. It
   shares nothing with kos_dyn_unlink_top, whose callers pay nothing
   for it.

   This is how a delimited continuation is captured. A shift leaves
   every node between itself and its prompt, and those same nodes are
   what the continuation is a template of, so the transfer that leaves
   them keeps each one as it comes off the chain and links it into the
   continuation, in place of cloning the run first and freeing it
   after. A kept node is never linked on a chain again: each call of
   the continuation clones the template, so a kept node only has to be
   what a clone taken before the leave would have been.

   The node leaves as it does under a transfer, and what its leave
   does outside itself still happens: a wind's after thunk and a
   dyn-with's __exit__ run, a PARAM puts the outer cells back, a
   FOREACH finishes the request of a step in flight. A TRY or FINALLY
   that is handling an exception takes it off the thread into itself
   and puts back the one it displaced, as when a generator's span is
   frozen, so a copy of it attached later is handling that exception
   again. What a clone would have recorded stays on the node: the
   resume a FOREACH's step left on the frame below, which is kept in
   its turn. A frame gives up a
   holder's cache slot it was to return to and has its namespace
   revealed, as a frame's clone does.

   The caller has checked every node it will keep with
   kos_dyn_node_capturable, before the first one left.

   :param ctx: execution context
   :param out_kept: receives the node, owned by the caller, with no
     parent; written only on EVAL_NORMAL
   :return: EVAL_NORMAL when the node is the caller's; EVAL_REPROLOGUE
     or EVAL_AWAIT when its leave is in flight and it stays on the
     chain; EVAL_RAISE
*/
KosOpResult kos_dyn_unlink_top_keep(KosContext *ctx, KosDynNode **out_kept);


/**
   The bytes a node of kind is allocated with: the whole KosDynNode
   for a major kind, the header and the largest minor payload for a
   PROMPT, CC or GEN. A node's size is not stored; the kind's
   KOS_DYN_CAP_MAJOR says which. For the allocator and the native
   tests.
*/
size_t kos_dyn_kind_size(KosDynKind kind);


/**
   Take node, which is dyn_top and carries no skip bit
   (KOS_DYN_CAP_SKIP), off the chain. Such a node is offered nothing,
   runs nothing and holds nothing outside itself, and no goal waits on
   one, so its leaving is one step with no answer to read: release
   frees it, as kos_dyn_unlink_top would, and keep hands it to the
   caller with no parent, as kos_dyn_unlink_top_keep would. A FOREACH
   finishes the request of a step in flight either way, by kind on the
   node in hand. Every walk that takes nodes off the chain tests the
   skip bits first and comes here for a node without one. Implemented
   in dynchain/dyn_walk.c.
*/
void kos_dyn_release_top(KosContext *ctx, KosDynNode *node);

void kos_dyn_keep_top(KosContext *ctx, KosDynNode *node);


/**
   Put the handled exception of each frozen TRY or FINALLY from top
   down to stop (not included) back on the thread, outermost first:
   the region's exception becomes handler-visible and the region
   records what was visible before it, which it gives back when it
   leaves. Run on each piece of a continuation's copy as it is
   attached, when the clone met such a region, so a body resumed
   inside a handler is handling its exception again, as a generator
   resumed there is.
*/
void kos_dyn_install_regions(
    KosContext *ctx,
    KosDynNode *top,
    const KosDynNode *stop);


/**
   Whether the generator span whose top is top holds a handled
   exception of its own on the thread: a TRY or FINALLY region (the
   nodes carrying both KOS_DYN_CAP_INTERCEPT and KOS_DYN_CAP_EFFECT)
   handling its exception, between top and the span's GEN, the first
   node down carrying KOS_DYN_CAP_GEN. The consumer's regions below the
   GEN are not the span's. Proportional to the span; for the throw
   paths only.
*/
int kos_dyn_span_handles(const KosDynNode *top);


/**
   Whether node can be part of a continuation's template: what its
   clone refuses, kos_dyn_unlink_top_keep's caller refuses here, before
   any node leaves. A wind or a with whose thunk is in flight, or that
   holds something stored for its completion, cannot; nor can a frame
   whose resume is of a kind that cannot clone its state.

   :return: 0, or -1 with the clone's exception set
*/
int kos_dyn_node_capturable(KosDynNode *node);


/**
   Which return path a stored pending return re-enters once the node
   whose leave thunk suspended the unwind walk has finished.
*/
typedef enum KosPendingReturnKind {
  KOS_PENDING_RETURN_NONE = 0,
  KOS_PENDING_RETURN_FRAME,     /* a bytecode frame's RETURN */
  KOS_PENDING_RETURN_BREAK,    /* a loop's BREAK, value in hand */
  KOS_PENDING_RETURN_CONTINUE,  /* a loop's CONTINUE, value in hand */
} KosPendingReturnKind;


/**
   The unwind walks. Each pops from dyn_top toward its target through
   kos_dyn_unlink_top and answers KosOpResult: EVAL_NORMAL when the
   target is dyn_top; EVAL_REPROLOGUE or EVAL_AWAIT (payload in .val)
   when a node's leave went into a thunk frame or onto an awaitable, the
   node still at dyn_top for the hook to settle; EVAL_RAISE or EVAL_FATAL
   with PyErr set.

   kos_dyn_unwind_to_node offers nothing: it unlinks each node above
   the target through kos_dyn_unlink_top with the barrier allowed, so
   it crosses barriers and no node can consume. A
   suspension leaves the value the caller is carrying with the caller,
   to store on the node whose leave is in flight, which is stored in
   *out_pending (NULL otherwise; the out-param itself may be NULL when
   the caller carries nothing). That node is not dyn_top when the leave
   installed a frame.

   kos_dyn_unwind_return is the RETURN walk. It offers handle(RETURN): a
   FINALLY consumes the return, taking val onto its block and branching
   to its handler, and the walk answers EVAL_REPROLOGUE. EVAL_NORMAL
   leaves val the caller's. A suspension stores val on the in-flight node as a RETURN
   obligation carrying the given kind before answering; the loop walk
   stores a break or continue the same way. val is consumed on every
   answer but EVAL_NORMAL.

   kos_dyn_unwind_to_loop serves BREAK and CONTINUE: it finds the
   innermost loop region in the current frame, pops to it (every node
   between is the frame's own), and on
   EVAL_NORMAL has set the pc and either stored result on the region
   (CONTINUE) or popped the region and pushed result (BREAK). result is
   consumed on every answer.
*/
KosOpResult kos_dyn_unwind_to_node(
    KosContext *ctx,
    const KosDynNode *target,
    KosXferEvent event,
    KosDynNode **out_pending);

KosOpResult kos_dyn_unwind_return(
    KosContext *ctx,
    KosDynFrame *frame,
    PyObject *val,
    KosPendingReturnKind kind);

KosOpResult kos_dyn_unwind_to_loop(
    KosContext *ctx,
    KosXferEvent event,
    PyObject *result);


int kos_dyn_param_wind(KosContext *ctx, KosDynParam *node);

void kos_dyn_param_unwind(KosContext *ctx, KosDynParam *node);


/**
  The extent contract for KOS_DYN_CAP_LEAVES nodes (WIND, WITH).
  Total and re-entrant: asking again is always legal, the node never
  unlinks itself, and the node never reads the transfer state. enter
  also serves a PARAM, which a transfer re-enters as it does a WIND
  (KOS_DYN_CAP_WINDS): it binds synchronously, answering EVAL_NORMAL
  (also when already bound) or EVAL_RAISE.

  enter / leave answer EVAL_NORMAL when the obligation is discharged,
  EVAL_REPROLOGUE when a thunk or __enter__/__exit__ frame was
  installed, EVAL_AWAIT (payload in .val) when the call suspended on an
  awaitable, EVAL_RAISE when the node's own code raised. A WIND or
  WITH runs through one machine over its KosExtent, the kind supplying
  the two calls. A node whose phase is ENTERING or LEAVING is in
  flight: enter answers REPROLOGUE while its call's frame sits above
  it, and leave aborts it once it is dyn_top, since nothing is above
  it and the call is gone (it raised, or a walk crossed it).

  settle: the pending call has returned and its result sits on the
  live block; take it and advance the phase. abort: the pending call
  died; advance without a result.
*/
KosOpResult kos_dyn_extent_enter(KosContext *ctx, KosDynNode *node);

KosOpResult kos_dyn_extent_leave(
    KosContext *ctx,
    KosDynNode *node,
    KosXferEvent ev);

int kos_dyn_extent_settle(KosContext *ctx, KosDynNode *node);

void kos_dyn_extent_abort(KosDynNode *node);


/**
  True when node is a CONTROL_HOOK node whose enter or leave is in
  flight.
*/
int kos_dyn_extent_in_flight(const KosDynNode *node);


/**
  The one hook a returning thunk, __enter__ or __exit__ fires: when
  dyn_top is a CONTROL_HOOK node in a pending phase, settle it, and
  when what settled was a leave, unlink it (unless a transfer step is
  waiting on that node, in which case the step does). Called after a
  frame return delivers into a non-frame node, and at an await revive
  with the awaited value already on the block.

  A stored return the unlink detaches is handed back in *out_return
  (kind KOS_OB_RETURN, owning its value) for the caller to resume
  through KosEval_ResumeReturn; the caller passes it in empty.

  :return: EVAL_NORMAL, or the unlink's result: EVAL_RAISE with the
    stored exception re-raised, or a settle failure
*/
KosOpResult kos_dyn_settle_top(KosContext *ctx, KosObligation *out_return);


/**
  The obligation slot of a KOS_DYN_CAP_LEAVES node. Callers
  test the cap first; this is the per-kind dispatch behind it.
*/
KosObligation *kos_dyn_node_ob(KosDynNode *node);

void kos_dyn_ob_clear(KosObligation *ob);

int kos_dyn_ob_traverse(KosObligation *ob, visitproc visit, void *arg);

/**
  Store on node. Each consumes what it is given, on failure as well; the
  slot must be empty, and node must carry one (see kos_dyn_node_ob).

  :return: 0, or -1 with PyErr set
*/
int kos_dyn_ob_store_value(KosDynNode *node, PyObject *val);

int kos_dyn_ob_store_exception(
    KosDynNode *node,
    PyObject *typ,
    PyObject *val,
    PyObject *tb);

int kos_dyn_ob_store_return(
    KosDynNode *node,
    PyObject *val,
    uint8_t path);


/**
  Whether a live node is still in the phase its template clone recorded
  at capture; the seam of an undelimited invoke stops below a node that
  has moved on. Kinds without a phase match on identity.
*/
int kos_dyn_node_same_phase(KosDynNode *live, KosDynNode *tmpl);

/**
   Offer a control transit to a node.  Does not unlink; only decides
   whether the node consumes, passes through, defers, or errors.

   :param ctx: context
   :param node: node to offer the event to
   :param event: transfer event (RETURN, RAISE, etc.)

   :return: KOS_HANDLE_PASS (unwind then pop), KOS_HANDLE_CONSUMED
     (deliver to node, stop walk), or KOS_HANDLE_ERROR (-1)
*/
KosHandleResult kos_dyn_handle(KosContext *ctx, KosDynNode *node,
                               KosXferEvent event);


/**
  The handler instruction to branch to after a TRY or FINALLY
  consumed a RAISE via kos_dyn_handle: the node's handler_pc, fixed
  from the extent table at push time.

  :param region: TRY or FINALLY node (caller must not pass GEN)

  :return: the handler instruction
*/
KosOp *kos_dynnode_raise_handler_pc_for_try_finally(
    KosContext *ctx,
    KosDynNode *region);


/**
  The instruction to resume at when a CATCH-capable node swallows an
  exception during pop, or NULL when not applicable
*/
KosOp *kos_dynnode_catch_resume_pc(KosDynNode *node);


/**
  Freeze gen at a yield: the span from dyn_top down to gen leaves the
  live chain into the owner's state record, gen is unlinked and freed,
  dyn_top returns to what gen's parent was, and the live registers
  retarget to it. A yield is not an unwind: no node between the frame
  and its GEN acts on it (no finally, no __exit__), so the open extents
  freeze with the chain and return on the next install. A node whose
  effect lives outside itself takes it out (kos_dyn_effect_out) and
  the install puts it back: the freeze follows the skip links from
  dyn_top to gen, lists the live effects in the state, then takes
  them out innermost first. The list is sized before anything
  changes. gen is the node kos_dyn_find_gen returned for the running
  frame.

  A span that holds a winder, a dyn-wind or a dyn-with, is not frozen
  until each has been left: the freeze lists them with the effects,
  changes nothing and answers 1, and the yield leaves them through a
  goal (kos_dyn_gen_wind_out_begin), then rests the span with
  kos_dyn_gen_freeze_wound.

  Callers: eval.c eval_event_yield.

  :return: 0; 1 with the winders listed and nothing changed; -1 with
      PyErr set; or -2 with MemoryError set and the chain, the span and
      its effects as they were, when the list could not be sized
*/
int kos_dyn_gen_freeze(KosContext *ctx, KosDynGen *gen);


/**
  Rest a span whose winders the yield has left
  (kos_dyn_gen_wind_out_next answered NULL): every listed effect is out
  already, so nothing is listed and nothing taken out. The span rests
  with no top recorded, which sends the next install to its winders.
  Returns 0, or -1 with PyErr set.
*/
int kos_dyn_gen_freeze_wound(KosContext *ctx, KosDynGen *gen);


/**
  Start the leaves a yield owes: record each listed winder with the
  node resting on it, by one walk from dyn_top to the outermost winder,
  and fill the generator's winding goal to run them
  (KOS_GOAL_WINDING_OUT, for kos_xfer_goal_run_kept). The goal takes
  its own reference to val and to the generator. Returns the goal, or
  NULL with MemoryError set, the list emptied and nothing else changed.
*/
KosXferGoal *kos_dyn_gen_wind_out_begin(
    KosContext *ctx,
    KosDynGen *gen,
    PyObject *val);


/**
  The next winder a yield leaves, innermost first, at dyn_top with only
  the chain outside it: every listed effect inside it is out, and what
  rested on it is cut off and held as the goal's target. Called again
  for a winder whose leave was in flight, it answers the same node.
  NULL when none is left: the span is whole at dyn_top again, with the
  registers the yield had.
*/
KosDynNode *kos_dyn_gen_wind_out_next(KosContext *ctx, KosXferGoal *goal);


/**
  node's leave is done. A yield's leave does not end the extent: it is
  put back as one never entered, so the next step's enter runs.
*/
void kos_dyn_gen_wind_left(
    KosContext *ctx,
    KosXferGoal *goal,
    KosDynNode *node);


/**
  The winder at dyn_top is entered: put back the effects listed between
  it and the next winder, then attach what rested on it, up to and
  including the next winder, or all of it when there is none. Returns
  0, or -1 with PyErr set and nothing attached.
*/
int kos_dyn_gen_wind_attach(KosContext *ctx, KosXferGoal *goal);


/**
  Load the registers of the outermost winder: at the install for a
  context that is not running, and at the goal's first run for a
  running context, whose winding in was pushed without them
  (kos_dyn_gen_install). The frame under the winder is live, and takes
  the demand bit of the frame below the GEN, the one the splice
  recorded in state->host.
*/
void kos_dyn_gen_wind_load(KosContext *ctx, KosXferGoal *goal);


/**
  Forget the listed effects and winders: their nodes are gone, or are
  back on the chain and entered.
*/
void kos_dyn_gen_wind_reset(KosGenState *state);


/**
  Generic dispatch over the node kinds: switches on the node's kind and
  invokes the per-kind implementation directly, so the compiler emits
  a jump table with inlined handlers rather than an indirect call.
  Allocate a copy of the node; NULL on error or when the kind forbids
  cloning.
*/
KosDynNode *kos_dyn_clone(KosDynNode *node);


/**
  Find the innermost enabled LOOP or FOREACH region on the dynamic
  chain, walking from dyn_top toward root. Read-only; never sets
  PyErr.

  Callers: opcode/ITER_NEXT.c, kos_dyn_unwind_to_loop.

  :param ctx: context

  :return: the topmost enabled LOOP or FOREACH region, or NULL if the
    walk reaches the root first
*/
KosDynNode *kos_dyn_find_loop(KosContext *ctx);


/* === Prompt and CC lookup === */


/**
  Find the topmost CC (call/cc) block on the dynamic chain.

  :param ctx: context

  :return: topmost KosDynCC, or NULL if none
*/
KosDynCC *kos_dyn_find_cc(KosContext *ctx);


/**
  Begin the capture of a delimited continuation.

  Finds the prompt, checks that every node between dyn_top and it can
  be captured, and creates a KosContinuation whose template holds the
  prompt's copy alone. The transfer that aborts to the prompt keeps
  the nodes it leaves into the template (kos_abort_to_prompt), so the
  continuation is whole once the handler's frame is live.

  :param ctx: context
  :param tag: the tag of the prompt to capture up to, nearest first,
     matched by identity, or NULL for the default tag (never None);
     borrowed
  :param out_k: output for captured continuation
  :param out_prompt: output for the live prompt the capture stopped at;
     borrowed

  :return: 0 on success, -1 on error
*/
int kos_shift_capture(
   KosContext *ctx,
   PyObject *tag,
   KosContinuation **out_k,
   KosDynPrompt **out_prompt);


/**
   Raise KoskariPromptUnreachable at a node that refused a capture. fmt
   takes the boundary word once; operation may be NULL for None.
*/
void kos_raise_prompt_unreachable(
    const char *operation,
    const char *boundary,
    const char *fmt);


/**
  Capture the undelimited continuation up to the current CC block.

  The continuation resumes at the instruction after POP_CC in the
  frame that was current when PUSH_CC was executed. Multi-shot by
  default.

  :param ctx: context
  :param out_k: output for captured continuation

  :return: 0 on success, -1 on error
*/
int kos_capture_cc(KosContext *ctx, KosContinuation **out_k);


/**
  Free a captured continuation.

  Frees the cloned chain and releases the stack reference.

  :param k: continuation to free (may be NULL)
*/
void kos_continuation_free(KosContinuation *k);


/* === Continuation invocation === */


/**
  The lowest common ancestor of the live chain and a template segment:
  the top of the run of pairs, matched by node id and by phase (a live
  node that has moved on since the capture ends the match), that
  reaches both roots. *out_live on the chain, *out_tmpl its
  counterpart; both NULL when the roots differ. *out_frames is the
  number of template frames above *out_tmpl, or in the whole template
  when there is none: the frames a transfer attaches.
*/
void kos_chain_common_ancestor(
    KosContext *ctx,
    KosDynNode *tmpl,
    KosDynNode **out_live,
    KosDynNode **out_tmpl,
    unsigned *out_frames);

/**
  Invoke a captured continuation.

  Pushes a transfer goal from the current position to the continuation's
  captured chain, for the prologue to drive.

  :param ctx: context
  :param k: continuation to invoke
  :param value: resume value

  :return: 0 on success, -1 on error
*/
int kos_invoke_continuation(
   KosContext *ctx,
   KosContinuation *k,
   PyObject *value,
   PyObject *continuation_ref);


/**
  Call a captured continuation from a call site.

  A delimited continuation's transfer runs at once
  (kos_xfer_goal_run): the calling frame stays on the chain under the
  copy. An undelimited one is pushed as kos_invoke_continuation pushes
  it.

  :param ctx: context
  :param k: continuation to call
  :param value: resume value
  :param continuation_ref: the continuation object, kept alive for the
    transfer; borrowed

  :return: EVAL_NORMAL when the transfer is done or pushed; otherwise
    the result to relay: EVAL_REPROLOGUE, EVAL_AWAIT, or EVAL_RAISE
    with an exception set
*/
KosOpResult kos_call_continuation(
   KosContext *ctx,
   KosContinuation *k,
   PyObject *value,
   PyObject *continuation_ref);


/* === GC traversal === */


/**
  Traverse a DynNode for GC.

  Visits all PyObject references in the node.

  :param node: node to traverse
  :param visit: visitproc callback
  :param arg: opaque arg for visit

  :return: 0 on success, non-zero if visit returns non-zero
*/
int kos_dynnode_traverse(KosDynNode *node, visitproc visit, void *arg);


/**
  Traverse a DynNode chain for GC.

  Visits all PyObject references in all nodes from node to stop.

  :param node: top of chain
  :param stop: stop at this node (not traversed), or NULL
  :param visit: visitproc callback
  :param arg: opaque arg for visit

  :return: 0 on success, non-zero if visit returns non-zero
*/
int kos_dynchain_traverse(
   KosDynNode *node,
   const KosDynNode *stop,
   visitproc visit,
   void *arg);


/**
  Traverse a KosContinuation for GC.

  :param k: continuation to traverse
  :param visit: visitproc callback
  :param arg: opaque arg for visit

  :return: 0 on success, non-zero if visit returns non-zero
*/
int kos_continuation_traverse(
   KosContinuation *k,
   visitproc visit,
   void *arg);


/**
  Snapshot a dynamic chain to a Python list of per-node dicts.

  Each list entry includes generic node fields and a kind-specific
  ``data`` dict payload.

  :param node: top of chain

  :return: new list object, or NULL on error
*/
PyObject *kos_dynchain_snapshot_to_list(KosDynNode *node);


/* === Utility macros === */


#define KosDynNode_KIND(n)    (((KosDynNode *)(n))->kind)
#define KosDynNode_PARENT(n)  (((KosDynNode *)(n))->parent)
#define KosDynNode_FLAGS(n)   (((KosDynNode *)(n))->flags)
#define KosDynNode_CAPS(n)    (((KosDynNode *)(n))->caps)
#define KosDynNode_NODE_ID(n) (((KosDynNode *)(n))->node_id)
#define KosDynNode_SKIP(n)    (((KosDynNode *)(n))->skip)

/* Mint a fresh, never-zero node id. */
uint32_t kos_dynnode_mint_id(void);

/**
  The node's id, minted on first read. Ids serve identity matching
  across clones and grafts (LCA seams, prompt lookup by a shift,
  snapshots); a node that is never captured, matched or snapshotted
  never pays for one. Zero is the unminted state: kos_dynnode_init
  leaves it so, a clone copies its source's id through this accessor,
  and a reused cached frame is reset to it.
*/
static inline uint32_t kos_dynnode_id(struct KosDynNode *node) {
  uint32_t id = KosDynNode_NODE_ID(node);
  if (! id)
    id = KosDynNode_NODE_ID(node) = kos_dynnode_mint_id();
  return id;
}
#define KosDynNode_HAS_CAP(n, cap)  (KosDynNode_CAPS(n) & (cap))

#define KosDynNode_IS_FRAME(n)   (KosDynNode_CAPS(n) & KOS_DYN_CAP_FRAME)
#define KosDynNode_IS_PROMPT(n)  (KosDynNode_KIND(n) == KOS_DYN_PROMPT)
#define KosDynNode_IS_WIND(n)    (KosDynNode_KIND(n) == KOS_DYN_WIND)
#define KosDynNode_IS_TRY(n)     (KosDynNode_KIND(n) == KOS_DYN_TRY)
#define KosDynNode_IS_FINALLY(n) (KosDynNode_KIND(n) == KOS_DYN_FINALLY)
#define KosDynNode_IS_LOOP(n)    (KosDynNode_KIND(n) == KOS_DYN_LOOP)
#define KosDynNode_IS_FOREACH(n) (KosDynNode_KIND(n) == KOS_DYN_FOREACH)
#define KosDynNode_IS_NS(n)      (KosDynNode_KIND(n) == KOS_DYN_NS)
#define KosDynNode_IS_COLLECT(n) (KosDynNode_KIND(n) == KOS_DYN_COLLECT)
#define KosDynNode_IS_CC(n)      (KosDynNode_CAPS(n) & KOS_DYN_CAP_CC)
#define KosDynNode_IS_PARAM(n)       (KosDynNode_KIND(n) == KOS_DYN_PARAM)
#define KosDynNode_IS_WITH(n)       (KosDynNode_KIND(n) == KOS_DYN_WITH)
#define KosDynNode_IS_GEN(n)      (KosDynNode_CAPS(n) & KOS_DYN_CAP_GEN)

#define KosDynGen_OWNER(n)        (((KosDynNode *)(n))->u.gen.owner)
#define KosDynGen_STATE(n)        (((KosDynNode *)(n))->u.gen.state)

/** a GEN with a parent is flattened: its yields deliver to a consumer
    on this chain; a GEN at the chain root surfaces them to an adapter */
#define KosDynGen_IS_FLATTENED(n) (KosDynNode_PARENT(n) != NULL)

#define KosDynFrame_CODE(f)       (((KosDynNode *)(f))->u.frame.code)
#define KosDynFrame_PC(f)         (((KosDynNode *)(f))->u.frame.pc)
#define KosDynFrame_ACTING_PC(f)  (((KosDynNode *)(f))->u.frame.acting_pc)
#define KosDynNode_STACK(n)       (((KosDynNode *)(n))->stack)
#define KosDynNode_NS(n)          (((KosDynNode *)(n))->ns)
#define KosDynFrame_STACK(f)      KosDynNode_STACK((KosDynNode *)(f))
#define KosDynFrame_NS(f)         KosDynNode_NS((KosDynNode *)(f))

#define KosDynFrame_FLAGS(f)      (((KosDynNode *)(f))->u.frame.frame_flags)
#define KosDynFrame_DEPTH(f)      (((KosDynNode *)(f))->u.frame.depth)

/* The hard cap on the process-wide frame depth limit, which defaults
   to KOS_FRAME_DEPTH_DEFAULT and is settable from 1 up to this cap.
   Frames live on the heap, so the limit guards against runaway memory
   from unbounded non-tail recursion; it is not Python's recursion
   limit, which sys.setrecursionlimit sets and which CPython enforces by
   itself where Koskari and Python call each other. Both values are
   reported in koskari.lib.build_flags.

   Natively the cap is 65535, the widest depth a frame can record.
   Under Emscripten it is 2000: every Koskari frame an exception
   crosses adds a traceback entry, CPython frees a traceback chain by
   recursing once per entry, and that recursion runs on the JavaScript
   engine's call stack, which CPython's recursion guard does not
   measure (the guard reads the linear-memory stack, and the dealloc
   functions use none of it). A chain of about 6000 entries overflows
   Node's default stack and ends the interpreter, so the cap keeps
   every traceback well under that. */
#ifdef __EMSCRIPTEN__
# define KOS_FRAME_DEPTH_MAX      2000u
#else
# define KOS_FRAME_DEPTH_MAX      UINT16_MAX
#endif

/* The frame depth limit in force until one is set. */
#ifdef __EMSCRIPTEN__
# define KOS_FRAME_DEPTH_DEFAULT  1000u
#else
# define KOS_FRAME_DEPTH_DEFAULT  10000u
#endif

/* frame is a demand-sync root */
#define KOS_FRAME_DEMAND          (1u << 0)

/* the root frame of a Coroutine driven from Python; a trace fact, set
   and read only under KOS_TRACE_EVENTS by the frame-cache trace count
   in kos_dynframe_free */
#define KOS_FRAME_CORO_ROOT       (1u << 1)

/* a completing code-less frame: cache_slot is the holder's cache slot
   and kos_dynframe_free offers the frame there through
   kos_dynframe_cache_inert; set only by KosNativeFrame_CacheOnFree */
#define KOS_FRAME_RETURN_TO_CACHE   (1u << 2)

/* the frame's parent is a FRAME node: the fact the return path tests,
   recorded wherever a frame is linked under a parent
   (kos_dynframe_note_parent) */
#define KOS_FRAME_PARENT_IS_FRAME   (1u << 3)
#define KosDynFrame_CACHE_SLOT(f) (((KosDynNode *)(f))->u.frame.cache_slot)
#define KosDynFrame_FN(f)         (((KosDynNode *)(f))->u.frame.fn)
#define KosDynFrame_BACK_FRAME(f) (((KosDynNode *)(f))->u.frame.back_frame)

#define KosDynFrame_RESUME_KIND(f)  (((KosDynNode *)(f))->u.frame.resume_kind)
#define KosDynFrame_RESUME_STATE(f) (((KosDynNode *)(f))->u.frame.resume_state)


/**
  Record on frame whether the parent it is now linked under is a FRAME
  node, for the return path's one test (kos_return_deliver). Every
  site that sets a frame's parent calls this after: the pushes, the
  tail swap, the generator's parenting, and the splices that re-link a
  captured chain's bottom.
*/
static inline void kos_dynframe_note_parent(KosDynFrame *frame) {
  KosDynNode *parent = KosDynNode_PARENT(frame);

  if (parent && KosDynNode_IS_FRAME(parent))
    KosDynFrame_FLAGS(frame) |= KOS_FRAME_PARENT_IS_FRAME;
  else
    KosDynFrame_FLAGS(frame) &= ~KOS_FRAME_PARENT_IS_FRAME;
}


/* The re-link of a chain's bottom under a new parent: a frame bottom
   records the parent it now has. */
static inline void kos_dynnode_note_parent(KosDynNode *node) {
  if (KosDynNode_IS_FRAME(node))
    kos_dynframe_note_parent((KosDynFrame *) node);
}


/**
   The skip link of a node whose parent is parent: the parent itself
   when it carries a skip bit, else whatever the parent names.
*/
static inline KosDynNode *kos_dynnode_skip_under(KosDynNode *parent) {
  if (! parent)
    return NULL;
  return (KosDynNode_CAPS(parent) & KOS_DYN_CAP_SKIP)
      ? parent : KosDynNode_SKIP(parent);
}


/**
   Put node on parent: the parent link, the skip link that follows from
   it, and a frame's record of its parent's kind. Every write of a
   parent goes through here but three: a node going to a freelist or a
   cache, whose next link sets both; a cut, a parent set to NULL to
   take a segment off a chain, which leaves the skip links for the
   attach to repair; and a segment's own interior, linked as it is
   built (kos_dyn_clone_segment, a continuation's kept nodes).
*/
static inline void kos_dynnode_link(KosDynNode *node, KosDynNode *parent) {
  KosDynNode_PARENT(node) = parent;
  KosDynNode_SKIP(node) = kos_dynnode_skip_under(parent);
  kos_dynnode_note_parent(node);
}


/**
   Repair the skip links at the bottom of a segment just attached. The
   nodes from tail down to bottom are the ones whose nearest skip-bit
   ancestor lies below the segment: tail is the segment's lowest
   skip-bit node, or its top when it has none. bottom is linked
   already (kos_dynnode_link), so its skip link is the value they all
   take.
*/
static inline void kos_dyn_repair_tail(KosDynNode *tail, KosDynNode *bottom) {
  KosDynNode *outer = KosDynNode_SKIP(bottom);

  for (KosDynNode *n = tail; n != bottom; n = KosDynNode_PARENT(n))
    KosDynNode_SKIP(n) = outer;
}


/**
   Recompute every skip link of the segment from top down to the node
   resting on stop, which is linked already. For a segment whose
   lowest skip-bit node is not known; proportional to the segment.
   Implemented in dynchain.c.
*/
void kos_dyn_relink_skips(KosDynNode *top, KosDynNode *stop);


/**
   Whether every skip link from top down to the node resting on stop
   is the one its parents give. For the native tests; proportional to
   the run walked. Implemented in dynchain.c.
*/
int kos_dyn_skips_hold(KosDynNode *top, KosDynNode *stop);


/* A resume is set when the kind is non-NULL; the name is a read-only
   view through the kind for messages and the snapshot. */
#define KosDynFrame_RESUME_NAME(f)                                        \
  (KosDynFrame_RESUME_KIND(f) ? KosDynFrame_RESUME_KIND(f)->name : NULL)

#define KosDynPrompt_TAG(p)              (((KosDynNode *)(p))->u.prompt.tag)

#define KosDynCC_RESUME_PC(c)     (((KosDynNode *)(c))->u.cc.resume_pc)

#define KosDynWind_CODE(w)         (((KosDynNode *)(w))->u.wind.code)
#define KosDynWind_ROW(w)          (((KosDynNode *)(w))->u.wind.row)
#define KosDynWind_BEFORE(w)       KosExtra_ASIDE(KosDynWind_ROW(w), 0)
#define KosDynWind_AFTER(w)        KosExtra_ASIDE(KosDynWind_ROW(w), 1)
#define KosDynWind_EXTENT(w)       (&((KosDynNode *)(w))->u.wind.extent)
#define KosDynWind_OB(w)           (&KosDynWind_EXTENT(w)->ob)
#define KosDynWind_ENTERED(w)      KosExtent_ENTERED(KosDynWind_EXTENT(w))
#define KosDynWind_PHASE(w)        KosExtent_PHASE(KosDynWind_EXTENT(w))

#define KosDynTry_HANDLER_PC(r)      (((KosDynNode *)(r))->u.try_.handler_pc)
#define KosDynTry_ENABLED(r)         (((KosDynNode *)(r))->u.try_.exc.lent[0])
#define KosDynTry_EXC(r)             (&((KosDynNode *)(r))->u.try_.exc)
#define KosDynFinally_HANDLER_PC(r)  (((KosDynNode *)(r))->u.finally_.handler_pc)
#define KosDynFinally_ENABLED(r) \
  (((KosDynNode *)(r))->u.finally_.exc.lent[0])
#define KosDynFinally_HANDLING_RAISE(r) \
  (((KosDynNode *)(r))->u.finally_.exc.lent[1])
#define KosDynFinally_HANDLING_CONTROL(r) \
  (((KosDynNode *)(r))->u.finally_.exc.lent[2])
#define KosDynFinally_EXC(r)         (&((KosDynNode *)(r))->u.finally_.exc)
#define KosDynFinally_HANDLING(r)    (&((KosDynNode *)(r))->u.finally_.handling)

/* The region record of a LOOP or a FOREACH, read the same way for
   either: both arms open with it. */
#define KosDynRegion_BREAK_PC(r)     (((KosDynNode *)(r))->u.loop.region.break_pc)
#define KosDynRegion_CONTINUE_PC(r)  (((KosDynNode *)(r))->u.loop.region.continue_pc)
#define KosDynRegion_RESULT(r)       (((KosDynNode *)(r))->u.loop.region.result)

/* Whether node is a LOOP or FOREACH region that is still running its
   body: a region whose else clause has started no longer answers a
   break or continue. Read-only; CLEAR_LOOP and CLEAR_FOREACH take the
   capability away. */
#define KosDynRegion_ENABLED(r) \
  (! ! (KosDynNode_CAPS(r) & KOS_DYN_CAP_LOOP_REGION))

#define KosDynLoop_BREAK_PC(r)       KosDynRegion_BREAK_PC(r)
#define KosDynLoop_CONTINUE_PC(r)    KosDynRegion_CONTINUE_PC(r)
#define KosDynLoop_ENABLED(r)        KosDynRegion_ENABLED(r)
#define KosDynLoop_RESULT(r)         KosDynRegion_RESULT(r)
#define KosDynForeach_BREAK_PC(r)    (((KosDynNode *)(r))->u.foreach.region.break_pc)
#define KosDynForeach_CONTINUE_PC(r) (((KosDynNode *)(r))->u.foreach.region.continue_pc)
#define KosDynForeach_ENABLED(r)     KosDynRegion_ENABLED(r)

static inline int kos_dyn_loop_is_enabled(const KosDynNode *node) {
  return KosDynRegion_ENABLED(node);
}
#define KosDynForeach_RESULT(r)      (((KosDynNode *)(r))->u.foreach.region.result)
#define KosDynForeach_REQ(r)         (&((KosDynNode *)(r))->u.foreach.req)
#define KosDynForeach_ITER(r)        (KosDynForeach_REQ(r)->iter)


#define KosDynParam_BINDINGS(n)    (((KosDynNode *)(n))->u.param.bindings)
#define KosDynParam_SAVED(n)       (((KosDynNode *)(n))->u.param.saved)


/** enter is a plain function: call it with mgr as the first argument */
#define KOS_WITH_SELF_ENTER 1u

/** exit is a plain function: call it with mgr as the first argument */
#define KOS_WITH_SELF_EXIT  2u

#define KosDynWith_MGR(n)          (((KosDynNode *)(n))->u.with.mgr)
#define KosDynWith_ENTER(n)        (((KosDynNode *)(n))->u.with.enter)
#define KosDynWith_EXIT(n)         (((KosDynNode *)(n))->u.with.exit)
#define KosDynWith_SELF_BIND(n)    (((KosDynNode *)(n))->u.with.exc.lent[0])
#define KosDynWith_EXTENT(n)       (&((KosDynNode *)(n))->u.with.extent)
#define KosDynWith_ENTERED(n)      KosExtent_ENTERED(KosDynWith_EXTENT(n))
#define KosDynWith_PHASE(n)        KosExtent_PHASE(KosDynWith_EXTENT(n))
#define KosDynWith_NAME(n)         (((KosDynNode *)(n))->u.with.name)
#define KosDynWith_RESUME_PC(n)    (((KosDynNode *)(n))->u.with.resume_pc)
#define KosDynWith_OB(n)           (&KosDynWith_EXTENT(n)->ob)
#define KosDynWith_EXC(n)          (&((KosDynNode *)(n))->u.with.exc)


/**
  True while __enter__ or __exit__ has been started but has not
  returned: it suspended on an awaitable, or it is a callable whose
  frame is installed and has yet to run. A pending phase means in flight
  and nothing else; kos_dyn_extent_settle advances it once the call has
  returned, kos_dyn_extent_abort once the call has died.
*/
#define KosDynWith_ENTER_PENDING(n) \
  (KosDynWith_PHASE(n) == KOS_EXTENT_ENTERING)
#define KosDynWith_EXIT_PENDING(n) \
  (KosDynWith_PHASE(n) == KOS_EXTENT_LEAVING)


/* === Unit lifecycle === */


int koskari_dynchain_init(PyObject *mod);
int koskari_dynchain_clear(PyObject *mod);


#endif


// The end.
