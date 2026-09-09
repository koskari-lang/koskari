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
   dynamic-wind semantics. Provides DynNode types (FRAME, PROMPT,
   WIND, REGION, and others) that form a unified parent
   chain, replacing the separate frame and handler chains.

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

   KOS_DYN_FRAME  - Function activation record
  KOS_DYN_PROMPT - Delimiter for delimited continuations (reset)
  KOS_DYN_DELIM  - Delimited invoke return marker with return metadata
   KOS_DYN_WIND   - Dynamic-wind extent with before/after hooks
   KOS_DYN_TRY - try region
   KOS_DYN_FINALLY - finally region
   KOS_DYN_LOOP - loop region
   KOS_DYN_FOREACH - foreach region
   KOS_DYN_NS     - Namespace scope (PUSH_NS/POP_NS)
   KOS_DYN_COLLECT - Collector scope (PUSH_COLLECT/POP_COLLECT); push ns on pop
   KOS_DYN_CC     - Undelimited continuation block (call/cc)
   KOS_DYN_YIELD_FROM - Yield-from delegation state
   KOS_DYN_PARAM     - Parameter bindings (parameterize extent)
   KOS_DYN_WITH      - Context manager (with / dyn-with); multi vs single-shot
                       is u.with.shot (see KOS_DYN_WITH_SHOT_*), not a second kind
   KOS_DYN_SHIFT     - Shift handler bookmark (transparent; no segment,
                       frame, or namespace of its own)
   KOS_DYN_GEN       - Generator node; intercepts handle(YIELD/RETURN/RAISE),
                       denies handle(SHIFT)
*/

typedef enum KosDynKind {
  KOS_DYN_FRAME = 0,
  KOS_DYN_PROMPT,
  KOS_DYN_DELIM,
  KOS_DYN_WIND,
  KOS_DYN_TRY,
  KOS_DYN_FINALLY,
  KOS_DYN_LOOP,
  KOS_DYN_FOREACH,
  KOS_DYN_NS,
  KOS_DYN_COLLECT,
  KOS_DYN_CC,
  KOS_DYN_YIELD_FROM,
  KOS_DYN_PARAM,
  KOS_DYN_WITH,
  KOS_DYN_SHIFT,
  KOS_DYN_GEN,
  /**
   KOS_DYN_NEXT - Iteration node for standalone NEXT opcode over Koskari
   async gen (flatten). Pushed at runtime from NEXT.c; self-pops after yield
   or return is delivered.
  */
  KOS_DYN_NEXT,
  /* sentinel value */
  KOS_DYN_UNKNOWN,
} KosDynKind;


/* Debug helper functions */
const char *kos_dynnode_kind_name(KosDynKind kind);


/* === DynNode flags === */


typedef enum KosDynFlags {
  KOS_DYNF_NONE        = 0x00,
  KOS_DYNF_CLONED      = 0x01,  /* node was cloned for continuation capture */
  KOS_DYNF_GEN_KEEPALIVE = 0x04,  /* GEN: holds a counted ref on its KPAG owner while active */
  KOS_DYNF_INVOCATION  = 0x08,  /* PROMPT: instance created by a k-invocation
                                   (graft instantiation or in-place invoke);
                                   no POP_PROMPT instruction pairs with it, so
                                   delivery routes through the paired DELIM */
  KOS_DYNF_HOLDS_RETURN_FRAME = 0x10,  /* DELIM: owns its RETURN_FRAME, the
                                   yoyo return-address snapshot of a nested
                                   handler; restitch clears the flag when it
                                   re-parents the frame onto the chain.
                                   Traversal and teardown must cover it
                                   while held */
} KosDynFlags;


/**
   KosDynNode capability bits. Generic code checks these for behavior
   instead of node type. Set at creation; copied on clone.
*/
#define KOS_DYN_CAP_MAY_SWALLOW_EXCEPTION    (1u << 0)
#define KOS_DYN_CAP_PRESERVE_ERR_ACROSS_POP  (1u << 1)
#define KOS_DYN_CAP_NEEDS_UNWIND_ACTION      (1u << 2)
#define KOS_DYN_CAP_NEEDS_WIND_BEFORE_ACTION (1u << 3)
#define KOS_DYN_CAP_NEEDS_INSTALL_WIND       (1u << 4)
#define KOS_DYN_CAP_AWAITS_THUNK_RESULT      (1u << 5)
/* the node reacts when control returns into it from a frame it owns:
   the extent contract has work for it (WITH, WIND); a node
   without it takes a callee's value on its segment and nothing more */
#define KOS_DYN_CAP_CONTROL_HOOK             (1u << 6)
#define KOS_DYN_CAP_BARRIER                  (1u << 7)
#define KOS_DYN_CAP_HANDLES_EXCEPTIONS       (1u << 8)
/** Iteration node (KOS_DYN_NEXT, extended FOREACH stepping) */
#define KOS_DYN_CAP_ITER_RECEIVER     (1u << 9)
/** Node participates in raise-walk dispatch (handle/restitch); the fast
   first check before kos_dyn_handle. Distinct from HANDLES_EXCEPTIONS,
   which marks failed-pop recovery (kos_dyn_exception_unwind). */
#define KOS_DYN_CAP_INTERCEPTS_RAISE         (1u << 10)

/* This kind binds its own namespace extent (frame, ns, collect); the
   ns field holds an introduced scope rather than the owned live-ns
   fill every other pushed node carries. */
#define KOS_DYN_CAP_BINDS_NS                 (1u << 11)

/* This kind carries an obligation slot (KosObligation): what it owes
   when it is unlinked -- a body value, an exception parked while its
   after thunk or __exit__ runs, or a return crossing a leave in flight.
   kos_dyn_node_ob is the generic accessor behind this cap. */
#define KOS_DYN_CAP_HOLDS_OBLIGATION         (1u << 12)

/* Alias for KOS_DYN_CAP_MAY_SWALLOW_EXCEPTION (unified API naming) */
#define KOS_DYN_CAP_CATCH KOS_DYN_CAP_MAY_SWALLOW_EXCEPTION


typedef enum KosDynWindPhase {
  KOS_WIND_PHASE_IDLE,
  KOS_WIND_PHASE_PRE_BEGIN,
  KOS_WIND_PHASE_POST_BEGIN,
  KOS_WIND_PHASE_PRE_AFTER,
  KOS_WIND_PHASE_POST_AFTER,
} KosDynWindPhase;


typedef enum KosRegionStyle {
  KOS_REGION_TRY      = 0,
  KOS_REGION_FINALLY  = 1,
  KOS_REGION_LOOP     = 2,
  KOS_REGION_FOREACH  = 4,
} KosRegionStyle;


/* === KosIterRequest: the iteration request sidecar === */


/**
   Wake events a requestor can receive: request completions only. The
   sidecar consumes machinery-internal traffic (awaited values,
   provider parks, mediated source yields) itself.
*/
typedef enum {
  KOS_IWAKE_VALUE = 0,  /* the requested element arrived */
  KOS_IWAKE_END,        /* the source is exhausted (payload or NULL) */
  KOS_IWAKE_EXCEPTION,  /* the request failed; PyErr set */
} KosIterWakeEvent;


/** In-flight step modes; private to the sidecar and the iteration-node
    delivery machinery -- sites and providers never read them */
#define KOS_IREQ_IDLE         0u
#define KOS_IREQ_INSTALLED    1u  /* provider's own frames are in-eval */
#define KOS_IREQ_AWAITING_PY  2u  /* parked python async step; the
                                     awaited completion is the element */
#define KOS_IREQ_GRAFT_VALUE  3u  /* value graft: the next frame return
                                     below is the element */


/**
   A snapshot of the live registers at one chain top: the frame, the
   owning stack slot, and the namespace that a resync would derive by
   walking from top. Borrowed pointers, valid while the chain below
   top is unchanged. The GEN node keeps two, one per end of the
   flattened step cycle (see KosDynGen_HOST / KosDynGen_FROZEN).
*/
typedef struct KosLiveRegs {
  struct KosDynNode *top;
  struct KosDynNode *frame;
  KosSegment **block;
  PyObject *ns;
} KosLiveRegs;


/**
   The iteration request record: the single home of an in-flight
   step's state. Embedded in the FOREACH, NEXT, and YIELD_FROM node
   arms; the delivery machinery reads the stepped iterator and the
   mode from here and nowhere else.
*/
typedef struct KosIterRequest {
  PyObject *iter;       /* stepped iterator; strong ref while armed */
  struct KosIterSlots *islots; /* iter's provider suite when iter is a
                                  KosIter provider, else NULL; bound with
                                  iter through kos_dyn_iter_request_bind_iter,
                                  so the step and delivery paths never
                                  re-derive it */
  uint8_t mode;         /* KOS_IREQ_* */
} KosIterRequest;


/**
   Bind iter (a reference the caller already owns, or NULL) as the
   request's stepped iterator and resolve its provider suite once. Every
   write of req->iter goes through here.
*/
void kos_dyn_iter_request_bind_iter(KosIterRequest *req, PyObject *iter);

/**
   Tell the request's provider its step is over: ki_finish on the bound
   suite, a no-op for a plain iterator. exhausted marks the provider
   closed, unless it recycles its durable context and re-derives
   exhaustion from its source, in which case it stays open (a mapped
   function's StopIteration is a truncation that builtins.map resumes
   from).
*/
void kos_dyn_iter_request_finish(KosDynNode *node, int exhausted);
void kos_dyn_iter_request_finish_req(KosIterRequest *req, int exhausted);


typedef enum {
  KOS_IREQ_DELIVERED = 0, /* *fast_out: the element (new ref) */
  KOS_IREQ_EXHAUSTED,     /* *fast_out: end payload (new ref) or NULL */
  KOS_IREQ_CONTINUES,     /* work installed in-eval; wake comes later */
  KOS_IREQ_EXIT,          /* suspension: propagate the eval result */
  KOS_IREQ_ERROR,         /* PyErr set */
} KosIterRequestStatus;


/**
   KosXferEvent indicates why an unwind is occurring.  Passed to
   kos_dyn_unwind and propagated through the unwind path so nodes
   can react appropriately (e.g. dynamic-wind after-thunk runs
   regardless; event is for diagnostics or future use).
*/
typedef enum KosXferEvent {
  KOS_XFER_EVENT_NONE = 0,
  KOS_XFER_EVENT_RETURN,
  KOS_XFER_EVENT_RAISE,
  KOS_XFER_EVENT_YIELD,
  KOS_XFER_EVENT_BREAK,
  KOS_XFER_EVENT_CONTINUE,
  KOS_XFER_EVENT_SHIFT,
  KOS_XFER_EVENT_TRANSFER,   /* a transfer is tearing the node down */
} KosXferEvent;


/* === Obligations === */


/**
   What a WIND or WITH node holds for after its leave completes: a value
   waiting for the revealed block, an exception waiting to resume
   propagating, or a return waiting to resume. Parked by the walk that
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
} KosObligationKind;


typedef struct KosObligation {
  uint8_t kind;
  union {
    PyObject *value;               /* KOS_OB_VALUE: owned */
    KosExcState exc;               /* KOS_OB_EXCEPTION: owned triple */
    struct {
      PyObject *value;             /* owned */
      uint8_t path;                /* KosPendingReturnKind */
      struct KosDynNode *delim;    /* borrowed bookmark, delimited path */
    } ret;
  } u;
} KosObligation;


/**
   KosHandleResult indicates how a node responded to handle(node, event).

   KOS_HANDLE_PASS     - node passed through; continue walk
   KOS_HANDLE_CONSUMED - node consumed; stop walk, caller delivers to handler
   KOS_HANDLE_DEFERRED - node deferred (e.g. async); return to caller
   KOS_HANDLE_ERROR    - error; return -1
*/
typedef enum KosHandleResult {
  KOS_HANDLE_PASS = 0,
  KOS_HANDLE_CONSUMED,
  KOS_HANDLE_DEFERRED,
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

struct KosDynNode {
  KosDynNode *parent;   /* enclosing node (toward root); reused as freelist link */
  KosDynKind kind;      /* discriminator for node type */
  uint32_t flags;       /* KosDynFlags bitfield */
  uint32_t caps;        /* capability bits for generic code (KOS_DYN_CAP_*) */
  uint32_t node_id;     /* stable allocation ID; copied on clone for LCA matching */
  /* Owned operand segment. Region kinds always own one (aimed at the
     embedded iseg at construction or push, heap after a spill); NULL
     marks a transfer bookmark, which inherits the enclosing extent's
     segment. */
  KosSegment *stack;
  /* Owned namespace: an introduced scope for KOS_DYN_CAP_BINDS_NS
     kinds, otherwise the owned live-ns fill taken at push. NULL only
     in the placeholder-frame window and on unfilled splices. */
  PyObject *ns;

  union {
    struct {
      KosCode *code;      /* strong reference; NULL only for placeholder frames */
      uint32_t pc;        /* next instruction */
      uint32_t acting_pc; /* instruction currently being executed */
      uint32_t frame_flags;
      /* Frame nesting depth on this context's chain: the enclosing
         frame's depth + 1, the root frame at 1. Set at install and
         renumbered whenever a segment carrying frames is spliced in
         (continuation landing, generator install), so the recursion
         limit test at install is one load and one compare. */
      uint16_t depth;
      /* A caller-owned one-frame cache (KosDynFrame **) this frame
         parks itself into when the return path frees it; meaningful
         only while KOS_FRAME_PARK_TO_CACHE is set, which
         KosNativeFrame_ReturnPark alone does. */
      void *park_cache;
      /* CPS consumer resume (see eval.h, KosResumeFn): the trampoline
         entry to invoke on the next value staged to this frame, and
         the state it receives. void * to avoid the include-order
         constraint of naming KosOpResult here. Set and cleared
         together; NULL means nothing is set. */
      void *resume_fn;
      void *resume_state;
      /* Borrowed static string naming the resume's installer, used
         only to name the frame in a capture-refusal message. Set by
         KosResumeFrame_Install and lives for the frame's occupancy
         (cleared at free, park, and tail reset), so it may be non-NULL
         while resume_fn is momentarily NULL between deliveries. */
      const char *resume_name;
      /* Owning KosFunction (strong) when this frame participates in the
         per-function frame cache; NULL opts out (clones, placeholder,
         code-less, and generator frames never set it).  Released --
         Py_CLEAR mandatory -- at free (after the park attempt) and at
         tail reuse. */
      PyObject *fn;
      /* Unwind cache: the frame displaced at install (borrowed; an
         ancestor on the chain, so it outlives this frame while
         installed). Set at install only; cleared at park, clone, and
         span detach; NULL means fall back to the chain walk. */
      struct KosDynNode *back_frame;
    } frame;
    struct {
      uint32_t prompt_id;
      /* pc of the POP_PROMPT closing this prompt's region, resolved from
         the PROMPT extent when PUSH_PROMPT ran. POP_SHIFT resumes at
         pop_pc + 1; stored resolved because by the time a shift body
         runs, no live acting pc still points into the extent. */
      uint32_t pop_pc;
    } prompt;
    struct {
      uint8_t mode;       /* KOS_DYN_DELIM_MODE_* */
      KosDynNode *chain;  /* owned when mode == INPLACE */
      uint32_t pc;
      /* The yoyo return address: the frame control lands in when the
         invoke yoyos back from the slice. For an INPLACE invoke of a
         nested handler this is a parentless CLONED snapshot (the slice
         returns out of and frees the original); restitch pushes it
         above the prompt so the handler has a frame to resume in. */
      KosDynNode *frame;
      KosDynNode *node;
      /* INPLACE only: displaced resume-frame state (stack owned) and
         the invoke-site acting pc for extent lookups after restitch */
      KosSegment *saved_stack;
      uint32_t saved_pc;
      uint32_t saved_acting_pc;
    } delim;
    struct {
      uint32_t resume_pc;
    } cc;
    struct {
      PyObject *before_thunk;  /* callable (owned) or NULL */
      PyObject *after_thunk;   /* callable (owned) or NULL */
      KosObligation ob;        /* the body result at POP_WIND, or what a
                                  walk parked while the after-thunk runs */
      uint8_t entered;
      uint8_t phase;          /* KosDynWindPhase; WIND_IDLE until thunk invoked */
    } wind;
    struct {
      uint32_t handler_pc;
      uint8_t enabled;
      uint8_t exc_saved;      /* consumed a raise: saved is what it displaced */
      KosExcState saved;      /* handler-visible state before this region */
    } try_;
    struct {
      uint32_t handler_pc;
      uint8_t enabled;
      uint8_t handling_raise;
      uint8_t handling_return;
      uint8_t exc_saved;      /* as for try_ */
      PyObject *handling_result;
      KosExcState saved;      /* handler-visible state before this region */
      KosExcState handling;   /* the raise being propagated through the body */
    } finally_;
    struct { uint32_t break_pc; uint32_t continue_pc; uint8_t enabled; PyObject *result; } loop;
    struct {
      uint32_t break_pc;
      uint32_t continue_pc;
      uint8_t enabled;
      PyObject *result;
      PyObject *iter;
      uint8_t exhausted;
      uint32_t iter_next_else_pc;
      KosIterRequest req;    /* in-flight ITER_NEXT step state */
    } foreach;
    struct {
      PyObject *delegate;
      uint8_t kind;
      uint8_t pending_action;
      PyObject *pending_arg;
      PyObject *pending_arg_type;  /* for KOS_YF_THROW: exception type */
      PyObject *inflight;
      KosIterRequest req;    /* in-flight KOS_ITER delegate step state */
      /* KosThreadFn: the drive body elected for kind when the node was
         pushed, tail-called on every entry; the delegate never changes,
         so no guard. Set by the YIELD_FROM handler, which owns the four
         bodies. See design/interpreter/electors.md. */
      void *drive;
      /* KosThreadFn: the phase resume armed on the delegating frame for
         the delegation's current suspension (the send that answers an
         emitted yield, an awaited result, a flattened inner yield), run
         by the prologue's delivery step with what arrived; NULL while
         the drive runs. The disarm on pop and unwind compares against
         it. resume_emit is the inner-yield phase, set beside drive at
         push so the node's own delivery code can arm it. */
      void *resume;
      void *resume_emit;
    } yield_from;
    struct {
      PyObject *bindings;  /* PyDict: key -> value (what we bind; keys from ParamRef) */
      PyObject *saved;     /* PyDict: key -> value or SENTINEL_UNSET (filled at enter, used at restore; NULL if not yet entered) */
    } param;
    struct {
      PyObject *mgr;       /* context manager (owned) */
      PyObject *enter;     /* resolved enter callable (owned) */
      PyObject *exit;      /* resolved exit callable (owned) */
      uint8_t self_bind;   /* KOS_WITH_SELF_*: which of enter/exit is a plain
                              function kept unbound, called with mgr first */
      uint8_t entered;     /* 1 after enter succeeded */
      uint8_t phase;       /* KOS_WITH_PHASE_* */
      uint8_t shot;        /* KOS_DYN_WITH_SHOT_*; internal to dyn_with only */
      uint8_t swallowed;   /* __exit__ answered true to a raise; the unlink
                              delivers None in place of the body value */
      PyObject *exit_done; /* KosRef shared across clones (owned); non-empty
                              once the single-shot __exit__ has run. NULL for
                              multi-shot */
      PyObject *name;      /* binding name or NULL (owned) */
      uint32_t resume_pc;  /* PC to jump to after with body (pop_pc + 1) */
      KosObligation ob;    /* the exception __exit__ is handling, or what
                              a walk parked while __exit__ runs */
    } with;
    struct {
      /* shift: marker delimiting the handler body; owns no block, so
         handler operands land in the innermost owner below (the
         prompt's region block, or a frame). prompt_node_id is the
         instance id of the PROMPT this shift captured against;
         POP_SHIFT resolves its delimiter by this, never by chain
         position. */
      uint32_t prompt_node_id;
    } shift;
    struct {
      PyObject *owner;     /* owning generator object (borrowed) or NULL */
      struct KosDynNode *active; /* suspended frame when released; NULL when running */
      uintptr_t refcount;  /* reference count; init 1; release decrements; free when 0 */
      /* Register snapshots for the flattened step cycle. host is the
         consumer's registers taken at install, restored at freeze while
         the GEN's parent is still host.top. frozen is the generator's
         registers taken at freeze, applied at install while active is
         still frozen.top. A guard mismatch falls back to the chain
         rebuild. */
      KosLiveRegs host;
      KosLiveRegs frozen;
    } gen;
    struct {
      KosIterRequest req;    /* the stepped object and in-flight mode */
      PyObject *default_val; /* owned; 2-arg NEXT */
      int n;                 /* 1 or 2 */
    } next;
  } u;

  /* Embedded operand-segment storage for region kinds: stack aims
     here at construction and spills to a heap segment on growth (the
     storage then sits dormant until node reuse). Transfer bookmarks
     leave it unused. */
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
   KOS_DYN_CAP_BINDS_NS and one is attached.  Introspection-only:
   under the namespace owned-fill every pushed node carries an ns and
   the live-value question is answered by reading it, so retarget
   logic never needs this distinction -- only displays that report
   scope structure do.
*/
static inline int kos_dynnode_introduces_ns(const struct KosDynNode *node) {
  return (node->caps & KOS_DYN_CAP_BINDS_NS) && node->ns != NULL;
}

typedef struct KosDynNode KosDynFrame;


/**
   RAISE unwind walk: from dyn_top toward the innermost frame, offer
   handle(RAISE) to each node then pop, delivering to the first TRY or
   FINALLY that consumes and crossing frame and generator boundaries as
   the exception propagates.

   Like every walk it returns KosOpResult: EVAL_REPROLOGUE when a handler
   applied, EVAL_RAISE when no handler remains (PyErr set, surfacing raise
   prepared), EVAL_AWAIT when an async unwind started.

   Callers: process_raise (the unified RAISE ingress in eval.c).
   Implemented in dynchain.c (dyn_walk).
*/
KosOpResult kos_dyn_unwind_raise(KosEvalState *es);


typedef struct KosDynNode KosDynPrompt;
typedef struct KosDynNode KosDynDelim;
typedef struct KosDynNode KosDynWind;
typedef struct KosDynNode KosDynTry;
typedef struct KosDynNode KosDynFinally;
typedef struct KosDynNode KosDynLoop;
typedef struct KosDynNode KosDynForeach;
typedef struct KosDynNode KosDynNs;
typedef struct KosDynNode KosDynCollect;
typedef struct KosDynNode KosDynCC;
typedef struct KosDynNode KosDynYieldFrom;
typedef struct KosDynNode KosDynParam;
typedef struct KosDynNode KosDynWith;
typedef struct KosDynNode KosDynShift;
typedef struct KosDynNode KosDynGen;
typedef struct KosDynNode KosDynNext;


/* === KosContinuation: captured delimited continuation === */


/**
   KosContinuation holds a captured delimited continuation.

   The captured_chain is a cloned segment of the dynamic chain from the
   capture point down through the prompt and its owning FRAME node
   (delimited), or from above the CC block down to root (undelimited).

   The resume_pc is the program counter to resume at; the frame it
   resumes in is resolved from the chain at invoke time (the landed
   segment's innermost FRAME), never stored. The resume_stack points at
   the innermost node-owned operand stack on captured_chain; it is an
   alias for that node-owned block, not separately released here.

   For multi-shot continuations, the chain/stack are cloned on
   each invocation. For one-shot, they are consumed directly.
*/

struct KosContinuation {
  KosDynNode *captured_chain;   /* cloned chain segment */
  uint32_t resume_pc;           /* pc to resume at */
  KosSegment *resume_stack;  /* aliases node-owned stack on captured_chain */
  uint32_t prompt_id;           /* target prompt tag */
  uint8_t one_shot;             /* 0 = multi-shot, 1 = one-shot */
  uint8_t live_slice_consumed;  /* 1 = in-place invoke consumed the live slice */
  PyObject *token_ref;          /* KosEvaluatorToken (ownership marker) */

  /* Capture-time live node identities for in-place (Path A) invoke
   * detection. Borrowed pointers: dereference only after a pointer +
   * node_id ancestry match from dyn_top confirms the node is still live
   * on this chain. Zero for undelimited continuations. */
  KosDynNode *live_prompt_node;
  uint32_t live_prompt_node_id;
  KosDynNode *live_shift_node;
  uint32_t live_shift_node_id;
};


#define KosContinuation_CAPTURED_CHAIN(k)  (((KosContinuation *)(k))->captured_chain)
#define KosContinuation_RESUME_PC(k)       (((KosContinuation *)(k))->resume_pc)
#define KosContinuation_RESUME_STACK(k)    (((KosContinuation *)(k))->resume_stack)
#define KosContinuation_PROMPT_ID(k)       (((KosContinuation *)(k))->prompt_id)
#define KosContinuation_ONE_SHOT(k)        (((KosContinuation *)(k))->one_shot)
#define KosContinuation_LIVE_SLICE_CONSUMED(k) \
  (((KosContinuation *)(k))->live_slice_consumed)
#define KosContinuation_TOKEN_REF(k)       (((KosContinuation *)(k))->token_ref)
#define KosContinuation_LIVE_PROMPT_NODE(k) \
  (((KosContinuation *)(k))->live_prompt_node)
#define KosContinuation_LIVE_PROMPT_NODE_ID(k) \
  (((KosContinuation *)(k))->live_prompt_node_id)
#define KosContinuation_LIVE_SHIFT_NODE(k) \
  (((KosContinuation *)(k))->live_shift_node)
#define KosContinuation_LIVE_SHIFT_NODE_ID(k) \
  (((KosContinuation *)(k))->live_shift_node_id)


/* === Hook invocation contract === */


/**
   Result of "run one hook invocation": done (advance step) or in progress
   (hook suspended on await; do not advance; store awaitable/state for resume).
*/
#define KOS_HOOK_DONE        0
#define KOS_HOOK_IN_PROGRESS 1


/**
   Signature for running one hook (or one chunk). Run the hook in es; fill
   out_ev on suspend. Return KOS_HOOK_DONE when the hook finished, or
   KOS_HOOK_IN_PROGRESS when it suspended on await (caller will resume later
   with send_val/throw_exc). hook_state is opaque state for the runner.
*/
typedef int (*KosHookInvokeFn)(KosEvalState *es, void *hook_state);


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
   :param prompt_id: tag for matching (0 = default)

   :return: new prompt node, or NULL on error
*/
KosDynPrompt *kos_dynprompt_new(KosDynNode *parent, uint32_t prompt_id);


/**
   Free a prompt node.

   :param prompt: prompt to free (may be NULL)
*/
void kos_dynprompt_free(KosDynPrompt *prompt);


/**
   Allocate a new DELIM marker node for delimited return bookkeeping.

   :param parent: parent node in dynamic chain
   :param mode: KOS_DYN_DELIM_MODE_*
   :param chain: optional owned chain payload for INPLACE mode
   :param pc: resume program counter
   :param frame: resume frame payload
   :param node: resume node payload

   :return: new DELIM node, or NULL on error
*/
KosDynDelim *kos_dyndelim_new(
   KosDynNode *parent,
   uint8_t mode,
   KosDynNode *chain,
   uint32_t pc,
   KosDynFrame *frame,
   KosDynNode *node);


/**
   Free a DELIM node.

   :param delim: DELIM node to free (may be NULL)
*/
void kos_dyndelim_free(KosDynDelim *delim);


/**
   Allocate a new wind node.

   :param parent: parent node in dynamic chain
   :param before_thunk: before hook callable (new ref, or NULL)
   :param after_thunk: after hook callable (new ref, or NULL)

   :return: new wind node, or NULL on error
*/
KosDynWind *kos_dynwind_new(
   KosDynNode *parent,
   PyObject *before_thunk,
   PyObject *after_thunk);


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
   Allocate a new region node.

   :param parent: parent node in dynamic chain
   :param style: region type
   :param break_pc: break/exit jump target
   :param continue_pc: continue jump target (or 0)

   :return: new region node, or NULL on error
*/
KosDynTry *kos_dyntry_new(KosDynNode *parent, uint32_t handler_pc);
KosDynFinally *kos_dynfinally_new(KosDynNode *parent, uint32_t handler_pc);
KosDynLoop *kos_dynloop_new(KosDynNode *parent, uint32_t break_pc, uint32_t continue_pc);
KosDynForeach *kos_dynforeach_new(KosDynNode *parent, uint32_t break_pc, uint32_t continue_pc);


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

   Each node in the segment is cloned and linked. The cloned chain's
   bottom node's parent is set to stop (or NULL if stop is NULL).

   :param top: top of segment to clone
   :param stop: stop at this node (not cloned), or NULL to clone all

   :return: cloned chain top, or NULL on error
*/
KosDynNode *kos_dyn_clone_segment(
   KosDynNode *top,
   KosDynNode *stop);


/* === Unified dynamic chain API === */


/**
   Push a node onto the context's dynamic chain.  Caller must have
   allocated the node and set its parent.  Calls install handler if
   the type has one.  Updates dyn_top.

   :param ctx: execution context
   :param node: node to push (parent already set)

   :return: 0 on success, -1 on error
*/
int kos_dyn_push(KosEvalState *es, KosDynNode *node);


/**
   Like kos_dyn_push but skips kos_dyn_install (e.g. TRY/FINALLY regions).
*/
int kos_dyn_push_noinstall(KosEvalState *es, KosDynNode *node);


/**
   Flush live PCs, set dyn_top to new_top, retarget live fields on es.

   Use for pops, splices, gen install, and any dyn_top change that is not
   kos_dyn_push. Internally captures the previous dyn_top and calls
   kos_dyn_resync_after_pop (incremental parent pop or full rebuild).
   Ordinary pushes must use kos_dyn_push or kos_dyn_push_noinstall instead.
*/
void kos_dyn_commit_dyn_top(KosEvalState *es, KosDynNode *new_top);


/**
   Full chain walk: recompute live frame, block, and namespace on es from
   dyn_top, then snapshot onto ctx.

   Call from outside the dynchain TU when eval lifecycle or opcodes must refresh
   live fields after a non-standard chain edit (KosContext_New bootstrap, tail
   koscall, stack assigned on dyn_top after push). Inside dynchain.c, xfer
   INSTALL/PUSH, gen install fallback, and kos_dyn_resync_after_pop when the
   edit is not a single-head parent pop use this instead of incremental retarget.
   Do not call on every opcode; ordinary pushes and pops go through
   kos_dyn_push and kos_dyn_commit_dyn_top.
*/
void kos_dyn_resync_chain(KosEvalState *es);


/**
   Clone a node and push onto the chain.  Fails for types that cannot
   clone (e.g. returns error for types that must not cross capture).

   :param ctx: execution context
   :param node: node to clone

   :return: 0 on success, -1 on error
*/
int kos_dyn_push_clone(KosEvalState *es, KosDynNode *node);


/* === Dynamic chain context operations === */


/**
   Push a new frame onto the context's dynamic chain.

   Creates a new frame and sets it as dyn_top. Updates frame_top cache.

   :param ctx: execution context
   :param code: bytecode object (borrowed)
   :param ns: namespace (reference is stolen)

   :return: new frame, or NULL on error
*/
KosDynFrame *kos_dyn_push_frame(KosEvalState *es, KosCode *code, PyObject *ns);


/**
   Push a frame with code=NULL, ns=empty dict, stack=NULL for INSTALL to configure.

   :return: new frame, or NULL on error
*/
KosDynFrame *kos_dyn_push_frame_empty(KosEvalState *es);


/**
   Push a frame for a native program: no code, and the namespace slot
   holds an owned fill of the live namespace -- a native frame
   introduces no namespace of its own, so it carries the fill like any
   other non-introducer node and the live register stays truthful over
   the program's extent.

   :return: new frame, or NULL on error
*/
KosDynFrame *kos_dyn_push_frame_native(KosEvalState *es);

/**
   The depth of the nearest frame at or below node, 0 when the chain
   under node holds no frame (or node is NULL).
*/
unsigned kos_dyn_frame_depth_at(KosDynNode *node);

/**
   Count the FRAME nodes on the segment from top down to but excluding
   base (NULL: the whole chain).
*/
unsigned kos_dyn_frame_count(KosDynNode *top, KosDynNode *base);

/**
   Renumber the frames on the segment from top down to but excluding
   base so the lowest sits at base_depth + 1 and each frame above it is
   one deeper. Called after a segment carrying frames is spliced onto a
   chain; the caller has already established that the result fits the
   recursion limit.
*/
void kos_dyn_renumber_frames(
    KosDynNode *top,
    KosDynNode *base,
    unsigned base_depth);


/**
   Push a prepared frame -- code, namespace, and cleared stack already
   attached, node base normalized by KosFunction_TryParkFrame -- as
   DYN_TOP and FRAME_TOP.  The install mirrors kos_dyn_push_frame minus
   the allocation; cannot fail.
*/
void kos_dyn_push_ready_frame(KosEvalState *es, KosDynFrame *frame);


/**
   Offer a detached native-program frame to a holder's one-frame cache:
   strip it to an inert allocation -- program, state, stack contents,
   and links all cleared, so the parked frame holds no references of
   any kind -- and store it in *cache when the slot is empty. A holder
   that parks frames this way cannot form a reference cycle through
   its cache, and frees a parked frame with kos_dynframe_free.

   :return: 1 when parked; 0 when the caller must free the frame
*/
int kos_dynframe_park_inert(KosDynFrame *frame, KosDynFrame **cache);


void kos_dynframe_reset_for_tail(KosDynFrame *frame);

/* Drop discardable extent nodes (NS, LOOP, FOREACH) above frame before
   tail-call frame reuse; errors on any other open node kind. */
int kos_dyn_teardown_to_frame(KosEvalState *es, KosDynFrame *frame);


/**
   Create a generator node node.  The generator object owns the node;
   the dynchain borrows it during execution.  Caller links the frame under
   it (frame->parent = gen) before passing the frame to KosContext_New.

   :param owner: owning generator object (borrowed) or NULL

   :return: new GEN node with parent=NULL, or NULL on error
*/
KosDynGen *kos_dyngen_new(PyObject *owner);


void kos_dyngen_free(KosDynGen *gen);


void kos_dyngen_acquire(KosDynGen *gen);


void kos_dyngen_release(KosDynGen *gen);


/**
   Finish a flattened generator whose frame has already left the chain
   and which is now dyn_top: mark its owner closed, unlink it, and drop
   its chain references. The raise walk and a transfer's leave both
   cross a flattened generator this way. PyErr is preserved across it.
*/
void kos_dyn_finish_flattened_gen(KosEvalState *es, KosDynGen *gen);


/**
   Install GEN's active frame onto the context's chain for execution.

   Takes frame from GEN.active, links GEN and frame into the chain
   (caller_chain -> GEN -> frame), pushes Py_None for YIELD resume, and
   sets frame as the execution top. Requires GEN.active != NULL.

   :param ctx: execution context (may have empty or non-empty chain)
   :param gen: GEN node with active frame set

   :return: 0 on success, -1 on error
*/
int kos_dyn_gen_install(KosEvalState *es, KosDynGen *gen);


/**
   Bootstrap install: graft GEN and src dyn_top chain onto consumer es.
   ACTIVE must be NULL; runnable chain lives on src only.
*/
int kos_dyn_gen_install_bootstrap(
    KosEvalState *es,
    KosContext *src,
    KosDynGen *gen);


/**
   True when GEN should be passed to kos_dyn_gen_install before the next
   koskari_eval on eval_ctx: GEN.active holds the chain but GEN is not
   under eval_ctx->dyn_top (yield freeze, or first run while still linked).

   If GEN.active is NULL, returns 0 (already installed or exhausted).
*/
int kos_dyn_gen_frozen_needs_install_es(KosEvalState *es, KosDynGen *gen);


/**
   Unwind an exhausted gen from the chain. Call when gen's frame returns
   (parent is GEN). Frees frame and restores consumer.
*/
int kos_dyn_teardown_gen_exhausted(KosEvalState *es);


/**
   Pop the top frame from the context's dynamic chain.

   Frees the frame and updates dyn_top to the frame's parent.
   Updates frame_top cache to find the new topmost frame.

   :param ctx: execution context

   :return: 0 on success, -1 on error
*/
int kos_dyn_pop_frame(KosEvalState *es);


/**
   Attach an existing frame to the context's dynamic chain (no allocation).

   :param ctx: execution context
   :param frame: existing frame (not freed by this module)

   :return: 0 on success, -1 on error
*/
int kos_dyn_attach_frame(KosEvalState *es, KosDynFrame *frame);


/**
   Detach the top frame without freeing it. Clears the detached frame's
   parent link so it is fully disconnected from the chain. Caller receives
   the frame.

   :param ctx: execution context
   :param frame_out: output; set to the detached frame (may be NULL)

   :return: 0 on success, -1 if top is not a frame
*/
int kos_dyn_detach_top_frame(KosEvalState *es, KosDynFrame **frame_out);


/**
   Push a prompt delimiter onto the dynamic chain.

   :param ctx: execution context
   :param prompt_id: tag for matching (0 = default)
   :param pop_pc: pc of the POP_PROMPT closing the region (from the
      PROMPT extent at the push site); POP_SHIFT resumes at pop_pc + 1

   :return: new prompt, or NULL on error
*/
KosDynPrompt *kos_dyn_push_prompt(
    KosEvalState *es,
    uint32_t prompt_id,
    uint32_t pop_pc);


/**
   Pop a prompt from the dynamic chain.

  Uses barrier-crossing pop policy with shared uninstall machinery.

   :param ctx: execution context
   :param prompt: prompt to pop (must be dyn_top)

  :return: 0 on success, -1 on error
*/
int kos_dyn_pop_prompt(KosEvalState *es, KosDynPrompt *prompt);


/**
   Push a DELIM node via kos_dyn_push.

   :return: new DELIM node, or NULL on error
*/
KosDynDelim *kos_dyn_push_delim(
   KosEvalState *es,
   uint8_t mode,
   KosDynNode *chain,
   uint32_t pc,
   KosDynFrame *frame,
   KosDynNode *node);


/**
   Pop a DELIM node from the dynamic chain.

   :param ctx: execution context
   :param delim: DELIM node to pop (must be dyn_top)
*/
int kos_dyn_pop_delim(KosEvalState *es, KosDynDelim *delim);


/**
   Complete a delimited POP_PROMPT return after the unwind walk succeeds
   (non-async). Pushes value onto the delimited return frame's stack, sets
   PC and frame_top, clears invocation and delimited-return fields.

   :param ctx: execution context
   :param value: return value (consumes a reference on success or failure)

   :return: 0 on success, -1 if ctx, value, delimited_return_frame, or
      delimited_return_pc is invalid (sets PyExc_RuntimeError)
*/
int kos_dyn_finish_delimited_pop_return(
   KosEvalState *es,
   KosDynDelim *delim,
   PyObject *value);


/**
   Complete an in-place (Path A) delimited POP_PROMPT return. Restores the
   resume frame's displaced stack/pc from the INPLACE delim, re-links the
   stashed invoke span above the prompt (which stays on the chain for the
   shift body's POP_SHIFT), frees the delim, and pushes value at the
   restored invoke site. Expects the delim at dyn_top.

   :param es: eval state
   :param delim: INPLACE DELIM node at dyn_top
   :param value: return value (consumes a reference on success or failure)

   :return: 0 on success, -1 on error (sets PyExc_RuntimeError)
*/
int kos_dyn_finish_inplace_pop_return(
   KosEvalState *es,
   KosDynDelim *delim,
   PyObject *value);


/**
   Restitch an in-place (Path A) delimited invoke at its INPLACE delim
   without delivering a value: restore the resume frame's displaced
   stack/pc, re-link the stashed invoke span above the prompt, free the
   delim, and restore invoke-site live registers. Used by the POP_PROMPT
   value return and by exception propagation out of the slice. Expects
   the delim at dyn_top.

   :return: 0 on success, -1 on error (sets PyExc_RuntimeError)
*/
int kos_dyn_inplace_restitch(KosEvalState *es, KosDynDelim *delim);


/**
   Push a CC (call/cc) block onto the dynamic chain.

   :param ctx: execution context
   :param resume_pc: instruction index to resume at when continuation is invoked

   :return: the new CC node, or NULL on error
*/
KosDynCC *kos_dyn_push_cc(KosEvalState *es, uint32_t resume_pc);


/**
   Pop a CC block from the dynamic chain.

   :param ctx: execution context
   :param cc: the CC node to pop (must be at dyn_top)
*/
int kos_dyn_pop_cc(KosEvalState *es, KosDynCC *cc);


/**
   Push a SHIFT block onto the dynamic chain.  A shift node pushes a stack
   block for its body (no frame, no namespace).  Used by PUSH_SHIFT after
   unwinding to the prompt.

   :param ctx: execution context
   :param prompt_node_id: instance id of the PROMPT the shift captured
      against; POP_SHIFT resolves the delimiter by this id

   :return: 0 on success, -1 on error
*/
int kos_dyn_push_shift(KosEvalState *es, uint32_t prompt_node_id);


/**
   Push a wind extent onto the dynamic chain.

   :param ctx: execution context
   :param before_thunk: before hook callable (new ref, or NULL)
   :param after_thunk: after hook callable (new ref, or NULL)

   :return: new wind node, or NULL on error
*/
KosDynWind *kos_dyn_push_wind(
   KosEvalState *es,
   PyObject *before_thunk,
   PyObject *after_thunk);


/**
   Push a region onto the dynamic chain.

   :param ctx: execution context
   :param style: region type
   :param break_pc: break/exit jump target
   :param continue_pc: continue jump target (or 0)

   :return: new region, or NULL on error
*/
KosDynTry *kos_dyn_push_try(KosEvalState *es, uint32_t handler_pc);
KosDynFinally *kos_dyn_push_finally(KosEvalState *es, uint32_t handler_pc);
KosDynLoop *kos_dyn_push_loop(KosEvalState *es, uint32_t break_pc, uint32_t continue_pc);
KosDynForeach *kos_dyn_push_foreach(KosEvalState *es, uint32_t break_pc, uint32_t continue_pc);


int kos_dyn_push_next_node(
    KosEvalState *es, PyObject *iter_obj, PyObject *default_val, int n);

void kos_dyn_next_abort_after_failed_attach(KosEvalState *es);

void kos_dyn_unlink_next_node(KosEvalState *es, KosDynNext *node);

int kos_dyn_next_deliver_yield(
    KosEvalState *es, KosDynNext *next, PyObject *val);

int kos_dyn_next_deliver_exhausted(
    KosEvalState *es,
    KosDynNext *next,
    PyObject *val);

int kos_dyn_foreach_deliver_yield_kos_agen(
    KosEvalState *es, KosDynForeach *fe, PyObject *val);

int kos_dyn_foreach_deliver_exhausted(
    KosEvalState *es, KosDynForeach *fe, PyObject *val);

int kos_dyn_yield_from_deliver_inner_yield(
    KosEvalState *es, KosDynYieldFrom *yf,
    PyObject *inner_val);

int kos_dyn_yield_from_deliver_exhausted(
    KosEvalState *es, KosDynYieldFrom *yf, PyObject *val);

KosDynNext *kos_dyn_next_clone(KosDynNext *node);


/* === Generic iteration-node dispatch (dyn_iter_node.c) === */


/**
   Locate the nearest receiving iteration node from active chain state.

   A node is a receiving iteration node iff it carries
   KOS_DYN_CAP_ITER_RECEIVER -- the capability is the single
   routing key; no node-kind checks.  NEXT and flattened-delegate
   YIELD_FROM nodes carry it for their whole lifetime; FOREACH toggles it
   with the request mode (kos_dyn_iter_request_set_mode).
   Future receiver kinds (e.g. a GEN-carried yield-from passthru field)
   join by carrying the capability.

   dyn_top is preferred: after a yield freeze the node that was
   directly driving the generator sits at dyn_top.  Otherwise walk
   outward from the running frame and return the nearest receiver.

   :return: borrowed node, or NULL when no receiver is active
*/
KosDynNode *kos_dyn_find_iter_node(KosEvalState *es);

/**
   The GEN node of the generator the running frame belongs to: the
   nearest GEN walking outward from the frame, or NULL when the frame
   is not in a generator. Callers: op_YIELD and the trampoline's YIELD
   arm, which then freeze it through kos_dyn_gen_freeze.
*/
KosDynGen *kos_dyn_find_gen(KosEvalState *es);

/**
   Deliver a flattened generator's YIELD value to a receiver located by
   kos_dyn_find_iter_node.  val ownership transfers to the consumer.

   :return: 0 on delivery (caller should REPROLOGUE); -1 on error
*/
int kos_dyn_iter_node_deliver_yield(
    KosEvalState *es, KosDynNode *node, PyObject *val);

/**
   Deliver exhaustion to a receiving iteration node whose flattened generator
   was already finished (raise-walk StopIteration conversion): the
   node-specific exhaustion tail without the GEN unwind. val consumed.

   :return: 0 delivered; -1 with PyErr set when the node re-emits
       (e.g. 1-arg NEXT); -1 without PyErr on internal failure
*/
int kos_dyn_iter_node_deliver_exhausted(
    KosEvalState *es, KosDynNode *node, PyObject *val);

/**
   The exception type that ends this node's in-flight step:
   StopIteration for a flattened Koskari agen step, StopAsyncIteration
   for a suspended python async iterator step.
*/
PyObject *kos_dyn_iter_node_exhaustion_exc(KosDynNode *node);


/**
   True when the node's in-flight step is a parked KosIter provider
   under the re-entry contract: the awaited value feeds back into the
   provider's ki_step rather than delivering as the element.
*/

/**
   True when the node's in-flight step is a value graft: the next
   frame return below delivers as the step element.
*/
int kos_dyn_iter_node_value_mode(KosDynNode *node);


/**
   The iterator object a parked iteration node is stepping (borrowed): the
   FOREACH iter or the NEXT node's object.
*/
PyObject *kos_dyn_iter_node_iter_obj(KosDynNode *node);

/**
   True when the node's in-flight step is a suspended python async
   iterator being driven through the pending context await.
*/
int kos_dyn_iter_node_awaiting_py(KosDynNode *node);

/**
   Clear a receiving iteration node after its driven generator was torn down on
   an error path: unlink NEXT / YIELD_FROM nodes, idle a FOREACH step.
*/
void kos_dyn_iter_node_abort(KosEvalState *es, KosDynNode *node);


/**
   Pop a region from the dynamic chain.

   :param ctx: execution context
   :param region: region to pop (must be dyn_top)
*/
int kos_dyn_pop_try(KosEvalState *es, KosDynTry *node);
int kos_dyn_pop_finally(KosEvalState *es, KosDynFinally *node);
int kos_dyn_pop_loop(KosEvalState *es, KosDynLoop *node);
int kos_dyn_pop_foreach(KosEvalState *es, KosDynForeach *node);


/**
   Undo a successful kos_dyn_push_try/finally/loop/foreach when a follow-up
   install step fails. region must be the pointer returned from the
   corresponding push.

   :param ctx: execution context
   :param region: region node still at dyn_top

   :return: 0 after popping the region; -1 if kind is not TRY/FINALLY/LOOP/FOREACH
      (does not pop; caller should set PyExc_RuntimeError)
*/
int kos_dyn_abort_push_region(KosEvalState *es, KosDynNode *region);


/**
   Push a namespace scope node (for PUSH_NS). The node owns its active
   namespace in the generic node namespace slot.

   :param ctx: execution context
   :param ns: active namespace for this NS scope (new ref)

   :return: new KosDynNs node, or NULL on error
*/
KosDynNs *kos_dyn_push_ns(KosEvalState *es, PyObject *ns);


/**
   Pop a namespace scope node.

   :param ctx: execution context
   :param node: NS node to pop (must be dyn_top)
*/
int kos_dyn_pop_ns(KosEvalState *es, KosDynNs *node);


/**
   Push a collector scope node (for PUSH_COLLECT). The node owns its
   collector namespace in the generic node namespace slot.

   :param ctx: execution context
   :param ns: collector namespace active in this scope (new ref)

   :return: new KosDynCollect node, or NULL on error
*/
KosDynCollect *kos_dyn_push_collect(KosEvalState *es, PyObject *ns);


/**
   Pop a collector scope node.

   :param ctx: execution context
   :param node: COLLECT node to pop (must be dyn_top)
*/
int kos_dyn_pop_collect(KosEvalState *es, KosDynCollect *node);


void kos_dyn_collect_unwind(KosEvalState *es, KosDynCollect *node);


/**
   Push a parameter extent onto the dynamic chain from a bindings dict.

   Creates a PARAM node wrapping each value in a fresh cell, runs enter
   (swap the cells into the context's shadow table, saving what they
   displace), and pushes.

   :param es: evaluator state
   :param bindings: PyDict mapping names to values (borrowed)

   :return: new KosDynParam node, or NULL on error
*/
KosDynParam *kos_dyn_push_param_dict(KosEvalState *es, PyObject *bindings);


/**
   Pop a parameter extent from the dynamic chain.

   Restores the displaced cells, updates dyn_top, and frees the node.

   :param es: evaluator state
   :param node: PARAM node to pop (must be dyn_top)
*/
int kos_dyn_pop_param(KosEvalState *es, KosDynParam *node);


/**
   Push a provisional WITH node (phase PENDING_ENTER_AWAIT) before calling
   enter. :c:member:`shot` is KOS_DYN_WITH_SHOT_MULTI or
   KOS_DYN_WITH_SHOT_SINGLE_ACTIVE. self_bind carries KOS_WITH_SELF_ENTER
   and KOS_WITH_SELF_EXIT for the protocol methods that were resolved on
   the type as plain functions and are called with mgr as their first
   argument; the others are already bound or need no receiver.

   :return: new KosDynWith node, or NULL on error
*/
KosDynWith *kos_dyn_push_with_provisional(
    KosEvalState *es,
    PyObject *mgr,
    PyObject *enter,
    PyObject *exit,
    uint8_t self_bind,
    PyObject *name,
    uint32_t resume_pc,
    uint8_t shot);


/**
   Clone a WITH node for continuation capture. Copies mgr, enter, exit,
   name with new references and copies shot/caps from src.

   :return: cloned node, or NULL on error
*/
KosDynWith *kos_dyn_with_clone(KosDynWith *src);


/**
   Pop a with extent from the dynamic chain.

   :param ctx: execution context
   :param node: WITH node to pop (must be dyn_top)
*/
int kos_dyn_pop_with(KosEvalState *es, KosDynWith *node);


/**
   Unlink dyn_top: the one site a node leaves the chain through.

   1. A KOS_DYN_CAP_CONTROL_HOOK node is asked to leave
      (kos_dyn_extent_leave); any answer but NORMAL returns unchanged,
      with the node still on the chain -- the caller parks and asks
      again later.
   2. The synchronous per-kind teardown runs (PARAM restore, YIELD_FROM
      close, TRY/FINALLY exception-state restore).
   3. The node's obligation is detached, with a with's swallow flag and
      body value.
   4. The parent becomes dyn_top; the node is freed.
   5. The obligation is acted on, by its kind and by ev. Under
      KOS_XFER_EVENT_NONE (a plain pop, or the hook finishing a leave):
      a VALUE and a with's body value go onto the revealed block, an
      EXCEPTION is re-raised, a RETURN is a caller error (only
      kos_dyn_settle_top can receive one). Under KOS_XFER_EVENT_RAISE:
      an EXCEPTION is chained as __context__ of the exception in
      flight, everything else is discarded. Under any other event a
      walk or a transfer crossed the node: everything is discarded.

   :param ev: why the node is leaving; KOS_XFER_EVENT_NONE for a plain pop
   :param allow_barrier: 0 refuses a KOS_DYN_CAP_BARRIER node with
     EVAL_RAISE; 1 crosses it
   :return: EVAL_NORMAL when the node is gone; EVAL_REPROLOGUE or
     EVAL_AWAIT when its leave is in flight and it stays; EVAL_RAISE
*/
KosOpResult kos_dyn_unlink_top(
    KosEvalState *es,
    KosXferEvent ev,
    int allow_barrier);


static inline KosOpResult kos_dyn_pop_nonbarrier_top(
    KosEvalState *es,
    KosXferEvent event) {
  return kos_dyn_unlink_top(es, event, 0);
}


static inline KosOpResult kos_dyn_pop_top_allow_barrier(
    KosEvalState *es,
    KosXferEvent event) {
  return kos_dyn_unlink_top(es, event, 1);
}


/**
   Which return path a parked pending return re-enters once the node
   whose leave thunk suspended the unwind walk has finished.
*/
typedef enum KosPendingReturnKind {
  KOS_PENDING_RETURN_NONE = 0,
  KOS_PENDING_RETURN_FRAME,     /* a bytecode frame's RETURN */
  KOS_PENDING_RETURN_NATIVE,    /* a native frame's return */
  KOS_PENDING_RETURN_DELIM,     /* routed to a delimited-invoke bookmark */
  KOS_PENDING_RETURN_BREAK,     /* a loop's BREAK, value in hand */
  KOS_PENDING_RETURN_CONTINUE,  /* a loop's CONTINUE, value in hand */
} KosPendingReturnKind;


/**
   The unwind walks. Each pops from dyn_top toward its target through
   kos_dyn_unlink_top and answers KosOpResult: EVAL_NORMAL when the
   target is dyn_top; EVAL_REPROLOGUE or EVAL_AWAIT (payload in .val)
   when a node's leave went into a thunk frame or onto an awaitable, the
   node still at dyn_top for the hook to settle; EVAL_RAISE or EVAL_FATAL
   with PyErr set.

   kos_dyn_unwind_to_frame and kos_dyn_unwind_to_node offer handle(event)
   to each node before popping and cross barriers and delimiters; a node
   that CONSUMES is an error for them. A suspension leaves the value the
   caller is carrying with the caller, to park on the node whose leave
   is in flight, which is stored in *out_pending (NULL otherwise; the
   out-param itself may be NULL when the caller carries nothing). That
   node is not dyn_top when the leave installed a frame.

   kos_dyn_unwind_return is the RETURN walk. It offers handle(RETURN): a
   FINALLY consumes the return, taking val onto its block and branching
   to its handler, and the walk answers EVAL_REPROLOGUE. A live DELIM
   invoke bookmark stops the walk: it is left at dyn_top, stored in
   *took, and the walk answers EVAL_NORMAL with val still the caller's to
   route. A suspension parks val as the context's pending return of the
   given kind before answering. val is consumed on every answer but
   EVAL_NORMAL.

   kos_dyn_unwind_to_loop serves BREAK and CONTINUE: it finds the
   innermost loop region in the current frame, pops to it, and on
   EVAL_NORMAL has set the pc and either stored result on the region
   (CONTINUE) or popped the region and pushed result (BREAK). result is
   consumed on every answer.
*/
KosOpResult kos_dyn_unwind_to_frame(
    KosEvalState *es,
    KosDynFrame *frame,
    KosXferEvent event,
    KosDynNode **out_pending);

KosOpResult kos_dyn_unwind_to_node(
    KosEvalState *es,
    const KosDynNode *target,
    KosXferEvent event,
    KosDynNode **out_pending);

KosOpResult kos_dyn_unwind_return(
    KosEvalState *es,
    KosDynFrame *frame,
    PyObject *val,
    KosPendingReturnKind kind,
    KosDynDelim **took);

KosOpResult kos_dyn_unwind_to_loop(
    KosEvalState *es,
    KosXferEvent event,
    PyObject *result);


/**
   1 when target lies on the chain from dyn_top, 0 when not. Used to
   validate a node pointer kept across a suspension before it is
   dereferenced.
*/
int kos_dyn_reaches(KosEvalState *es, const KosDynNode *target);




int kos_dyn_param_wind(KosEvalState *es, KosDynParam *node);
void kos_dyn_param_unwind(KosEvalState *es, KosDynParam *node);


/**
   The extent contract for KOS_DYN_CAP_CONTROL_HOOK nodes (WIND, WITH).
   Total and re-entrant: asking again is always legal, the node never
   unlinks itself, and the node never reads the transfer state.

   enter / leave answer EVAL_NORMAL when the obligation is discharged,
   EVAL_REPROLOGUE when a thunk or __enter__/__exit__ frame was
   installed, EVAL_AWAIT (payload in .val) when the call suspended on an
   awaitable, EVAL_RAISE when the node's own code raised. A node in a
   pending phase (WIND PRE_*, WITH PENDING_*) is in flight: enter and
   leave answer REPROLOGUE while its thunk frame sits above it, and
   leave aborts it once it is dyn_top, since nothing is above it and
   the thunk is gone (it raised, or a walk crossed it).

   settle: the pending call has returned and its result sits on the
   live block; take it and advance the phase. abort: the pending call
   died; advance without a result.
*/
KosOpResult kos_dyn_extent_enter(KosEvalState *es, KosDynNode *node);
KosOpResult kos_dyn_extent_leave(
    KosEvalState *es,
    KosDynNode *node,
    KosXferEvent ev);
int kos_dyn_extent_settle(KosEvalState *es, KosDynNode *node);
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

   A parked return the unlink detaches is handed back in *out_return
   (kind KOS_OB_RETURN, owning its value) for the caller to resume
   through KosEval_ResumeReturn; the caller passes it in empty.

   :return: EVAL_NORMAL, or the unlink's result: EVAL_RAISE with the
     parked exception re-raised, or a settle failure
*/
KosOpResult kos_dyn_settle_top(KosEvalState *es, KosObligation *out_return);


/**
   The obligation slot of a KOS_DYN_CAP_HOLDS_OBLIGATION node. Callers
   test the cap first; this is the per-kind dispatch behind it.
*/
KosObligation *kos_dyn_node_ob(KosDynNode *node);

void kos_dyn_ob_clear(KosObligation *ob);
int kos_dyn_ob_traverse(KosObligation *ob, visitproc visit, void *arg);

/**
   Park on node. Each consumes what it is given, on failure as well; the
   slot must be empty, and node must carry one (see kos_dyn_node_ob).

   :return: 0, or -1 with PyErr set
*/
int kos_dyn_ob_park_value(KosDynNode *node, PyObject *val);
int kos_dyn_ob_park_exception(
    KosDynNode *node,
    PyObject *typ,
    PyObject *val,
    PyObject *tb);
int kos_dyn_ob_park_return(
    KosDynNode *node,
    PyObject *val,
    uint8_t path,
    KosDynNode *delim);


/**
   Generic dispatch functions for the seven dynchain operations.  Each
   switches on the node's kind and invokes the appropriate per-type
   implementation directly, allowing the compiler to emit a jump table
   with inlined handlers rather than an indirect function-pointer call.

   kos_dyn_install: run install-phase setup when a node is pushed.
   kos_dyn_exception_unwind: called when exception propagation reaches a
     HANDLES_EXCEPTIONS node. Returns EVAL_NORMAL to continue unwinding,
     EVAL_REPROLOGUE if handler applied, EVAL_AWAIT if async started,
     EVAL_RAISE on error.
   kos_dyn_clone: allocate a copy of the node; NULL return on error or if
     the type forbids cloning.
*/
int kos_dyn_install(KosEvalState *es, KosDynNode *node);
KosOpResult kos_dyn_exception_unwind(KosEvalState *es, KosDynNode *node);

/**
   Offer a control transit to a node.  Does not unlink; only decides
   whether the node consumes, passes through, defers, or errors.

   :param es: eval state (borrowed ctx for frame/chain reads)
   :param node: node to offer the event to
   :param event: transfer event (RETURN, RAISE, etc.)
   :param out_ev: event output (for transformed events; may be NULL)

   :return: KOS_HANDLE_PASS (unwind then pop), KOS_HANDLE_CONSUMED
     (deliver to node, stop walk), KOS_HANDLE_DEFERRED, or
     KOS_HANDLE_ERROR (-1)
*/
KosHandleResult kos_dyn_handle(KosEvalState *es, KosDynNode *node,
                               KosXferEvent event);


/**
   Resolve bytecode PC after TRY or FINALLY consumed RAISE via
   kos_dyn_handle.  Merges node handler_pc with kos_try_handler_for
   extent entries.

   :param region: TRY or FINALLY node (caller must not pass GEN)

   :return: handler PC to branch to
*/
uint32_t kos_dynnode_raise_handler_pc_for_try_finally(
    KosEvalState *es,
    KosDynNode *region);


/**
   Resume PC when a CATCH-capable node swallows an exception during pop,
   or (uint32_t)-1 when not applicable
*/
uint32_t kos_dynnode_catch_resume_pc(KosDynNode *node);


/**
   Freeze gen at a yield: the span from dyn_top down to gen leaves the
   live chain into gen's active slot, gen's parent becomes dyn_top, and
   the live registers retarget to it. A yield is not an unwind: no node
   between the frame and its GEN acts on it (no finally, no __exit__),
   so the open extents freeze with the chain and return on the next
   install. gen is the node kos_dyn_find_gen returned for the running
   frame.

   Callers: eval.c eval_event_yield.

   :return: 0, or -1 with PyErr set
*/
int kos_dyn_gen_freeze(KosEvalState *es, KosDynGen *gen);

KosDynNode *kos_dyn_clone(KosDynNode *node);

void kos_dyn_ns_unwind(KosEvalState *es, KosDynNs *node);

void kos_dyn_yieldfrom_unwind(KosEvalState *es, KosDynYieldFrom *node);

/**
   Clear the node's phase resume from the live frame when it is armed
   there: the delegation is ending (finished, or unwound by a raise),
   and the frame threads on from its pc. Idempotent.
*/
/**
   Arm resume, a KosThreadFn, as the node's phase resume on the live
   frame: the frame's next delivery runs it instead of an instruction.
   Every suspension of the delegation ends in this.
*/
void kos_dyn_yield_from_arm(KosEvalState *es, KosDynYieldFrom *node,
                            void *resume);

void kos_dyn_yield_from_disarm(KosEvalState *es, KosDynYieldFrom *node);

/**
   Close the delegate of a YIELD_FROM dyn node (on unwind or error).
   Does not free the node. Callers: kos_dyn_yieldfrom_unwind, opcode
   YIELD_FROM.

   :param node: YIELD_FROM node, or NULL (no-op)
*/
void kos_yieldfrom_close_delegate(KosDynYieldFrom *node);


/**
   Find the innermost LOOP or FOREACH region on the dynamic chain, walking
   from dyn_top toward root.  Read-only; never sets PyErr.

   Callers: opcode/ITER_NEXT.c, kos_dyn_unwind_to_loop.

   :param es: eval state

   :return: the topmost LOOP or FOREACH region, or NULL if none found
 */
KosDynNode *kos_dyn_find_loop(KosEvalState *es);





/**
   Push a YIELD_FROM delegation node onto the context's dynamic chain.

   Does not update frame_top (the frame remains the topmost frame).
   Caller must have normalized the delegate and determined kind.

   :param ctx: execution context
   :param delegate: normalized delegate (new ref; node takes ownership)
   :param kind: delegate kind (KOS_YF_SYNC_ITER, … from this header)

   :return: new node, or NULL on error
*/
KosDynYieldFrom *kos_dyn_push_yield_from(
    KosEvalState *es,
    PyObject *delegate,
    uint8_t kind);


/**
   Pop a YIELD_FROM node. Caller must have closed the delegate if needed.
   node must be dyn_top.

   :param ctx: execution context
   :param node: YIELD_FROM node to pop (must be current dyn_top)
*/
int kos_dyn_pop_yield_from(KosEvalState *es, KosDynYieldFrom *node);


/**
   Unlink a YIELD_FROM node whether or not it is dyn_top. When it is top,
   delegates to kos_dyn_pop_yield_from; otherwise splices the chain and
   rebuilds live fields.

   :return: 0 on success, -1 on error (sets PyErr)
*/
int kos_dyn_unlink_yield_from(KosEvalState *es, KosDynYieldFrom *yf);


/* === Prompt lookup and continuation capture === */


/* === Context exception stash === */


/**
   Find the topmost CC (call/cc) block on the dynamic chain.

   :param es: eval state

   :return: topmost KosDynCC, or NULL if none
*/
KosDynCC *kos_dyn_find_cc(KosEvalState *es);


/**
   Capture a delimited continuation.

   Finds the prompt, validates capture, clones the chain segment,
   and creates a KosContinuation.

   :param es: eval state
   :param prompt_id: target prompt tag
   :param out_k: output for captured continuation

   :return: 0 on success, -1 on error
*/
int kos_shift_capture(
   KosEvalState *es,
   uint32_t prompt_id,
   uint32_t resume_pc,
   KosContinuation **out_k);


/** Sentinel prompt_id for undelimited (call/cc) continuations. */
#define KOS_PROMPT_ID_UNDELIMITED ((uint32_t)-1)


/**
   Capture the undelimited continuation up to the current CC block.

   The continuation resumes at the instruction after POP_CC in the
   frame that was current when PUSH_CC was executed. Multi-shot by
   default.

   :param es: eval state
   :param out_k: output for captured continuation

   :return: 0 on success, -1 on error
*/
int kos_capture_cc(KosEvalState *es, KosContinuation **out_k);


/**
   Free a captured continuation.

   Frees the cloned chain and releases the stack reference.

   :param k: continuation to free (may be NULL)
*/
void kos_continuation_free(KosContinuation *k);


/* === Continuation invocation === */


/**
   Invoke a captured continuation.

   Arms a transfer goal from the current position to the continuation's
   captured chain and schedules execution.

   :param es: execution/eval state
   :param k: continuation to invoke
   :param value: resume value
   :param is_throw: if nonzero, throw value as exception

   :return: 0 on success, -1 on error
*/
/**
   The lowest common ancestor of the live chain and a template segment
   (matched by node id from the roots): *out_live on the chain,
   *out_tmpl its counterpart; both NULL when the roots differ.
*/
int kos_chain_common_ancestor(
    KosEvalState *es,
    KosDynNode *tmpl,
    KosDynNode **out_live,
    KosDynNode **out_tmpl);

int kos_invoke_continuation(
   KosEvalState *es,
   KosContinuation *k,
   PyObject *value,
   int is_throw,
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
#define KosDynNode_HAS_CAP(n, cap)  (KosDynNode_CAPS(n) & (cap))

#define KosDynNode_IS_FRAME(n)   (KosDynNode_KIND(n) == KOS_DYN_FRAME)
#define KosDynNode_IS_PROMPT(n)  (KosDynNode_KIND(n) == KOS_DYN_PROMPT)
#define KosDynNode_IS_DELIM(n)   (KosDynNode_KIND(n) == KOS_DYN_DELIM)
#define KosDynNode_IS_WIND(n)    (KosDynNode_KIND(n) == KOS_DYN_WIND)
#define KosDynNode_IS_TRY(n)     (KosDynNode_KIND(n) == KOS_DYN_TRY)
#define KosDynNode_IS_FINALLY(n) (KosDynNode_KIND(n) == KOS_DYN_FINALLY)
#define KosDynNode_IS_LOOP(n)    (KosDynNode_KIND(n) == KOS_DYN_LOOP)
#define KosDynNode_IS_FOREACH(n) (KosDynNode_KIND(n) == KOS_DYN_FOREACH)
#define KosDynNode_IS_NS(n)      (KosDynNode_KIND(n) == KOS_DYN_NS)
#define KosDynNode_IS_COLLECT(n) (KosDynNode_KIND(n) == KOS_DYN_COLLECT)
#define KosDynNode_IS_CC(n)      (KosDynNode_KIND(n) == KOS_DYN_CC)
#define KosDynNode_IS_YIELD_FROM(n)  (KosDynNode_KIND(n) == KOS_DYN_YIELD_FROM)
#define KosDynNode_IS_PARAM(n)       (KosDynNode_KIND(n) == KOS_DYN_PARAM)
#define KosDynNode_IS_WITH(n)       (KosDynNode_KIND(n) == KOS_DYN_WITH)
#define KosDynNode_IS_SHIFT(n)    (KosDynNode_KIND(n) == KOS_DYN_SHIFT)
#define KosDynNode_IS_GEN(n)      (KosDynNode_KIND(n) == KOS_DYN_GEN)
#define KosDynNode_IS_NEXT(n)     (KosDynNode_KIND(n) == KOS_DYN_NEXT)

#define KosDynGen_OWNER(n)        (((KosDynNode *)(n))->u.gen.owner)
#define KosDynGen_ACTIVE(n)       (((KosDynNode *)(n))->u.gen.active)
#define KosDynGen_REFCOUNT(n)     (((KosDynNode *)(n))->u.gen.refcount)
#define KosDynGen_HOST(n)         (&((KosDynNode *)(n))->u.gen.host)
#define KosDynGen_FROZEN(n)       (&((KosDynNode *)(n))->u.gen.frozen)
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
/* The widest depth a frame can record, and so the ceiling on the
   per-context recursion limit whatever sys.setrecursionlimit says. */
#define KOS_FRAME_DEPTH_MAX       UINT16_MAX
#define KOS_FRAME_DEMAND          (1u << 0)  /* frame is a demand-sync root */
#define KOS_FRAME_PARK_TO_CACHE   (1u << 2)  /* a completing code-less
                                                frame: park_cache is the
                                                holder's cache slot
                                                (KosDynFrame **) and free
                                                offers the frame there
                                                (kos_dynframe_park_inert).
                                                Set only by
                                                KosNativeFrame_ReturnPark */
#define KosDynFrame_PARK_CACHE(f) (((KosDynNode *)(f))->u.frame.park_cache)
#define KosDynFrame_FN(f)         (((KosDynNode *)(f))->u.frame.fn)
#define KosDynFrame_BACK_FRAME(f) (((KosDynNode *)(f))->u.frame.back_frame)

#define KosDynFrame_RESUME_FN(f)    (((KosDynNode *)(f))->u.frame.resume_fn)
#define KosDynFrame_RESUME_STATE(f) (((KosDynNode *)(f))->u.frame.resume_state)
#define KosDynFrame_RESUME_NAME(f)  (((KosDynNode *)(f))->u.frame.resume_name)

#define KosDynPrompt_ID(p)               (((KosDynNode *)(p))->u.prompt.prompt_id)
#define KosDynPrompt_POP_PC(p)           (((KosDynNode *)(p))->u.prompt.pop_pc)

#define KOS_DYN_DELIM_MODE_NONE       0u
/* 1u retired: K_RETURN (pre-walk delim scan in the RETURN opcode).  The
   return walk now stops naturally at any DELIM with a live bookmark. */
#define KOS_DYN_DELIM_MODE_POP_PROMPT 2u
/* In-place (Path A) delimited return: chain stashes the detached invoke
   span; saved_stack/saved_pc hold the resume frame's displaced state. */
#define KOS_DYN_DELIM_MODE_INPLACE    3u

#define KosDynDelim_MODE(d)        (((KosDynNode *)(d))->u.delim.mode)
#define KosDynDelim_RETURN_CHAIN(d) (((KosDynNode *)(d))->u.delim.chain)
#define KosDynDelim_RETURN_PC(d)   (((KosDynNode *)(d))->u.delim.pc)
#define KosDynDelim_RETURN_FRAME_NODE(d) (((KosDynNode *)(d))->u.delim.frame)
#define KosDynDelim_RETURN_FRAME(d) ((KosDynFrame *) (KosDynDelim_RETURN_FRAME_NODE(d)))
#define KosDynDelim_RETURN_NODE(d)  (((KosDynNode *)(d))->u.delim.node)
#define KosDynDelim_SAVED_STACK(d) (((KosDynNode *)(d))->u.delim.saved_stack)
#define KosDynDelim_SAVED_PC(d)    (((KosDynNode *)(d))->u.delim.saved_pc)
#define KosDynDelim_SAVED_ACTING_PC(d) \
  (((KosDynNode *)(d))->u.delim.saved_acting_pc)

#define KosDynCC_RESUME_PC(c)     (((KosDynNode *)(c))->u.cc.resume_pc)

#define KosDynShift_PROMPT_NODE_ID(s) \
  (((KosDynNode *)(s))->u.shift.prompt_node_id)

#define KosDynWind_BEFORE_THUNK(w) (((KosDynNode *)(w))->u.wind.before_thunk)
#define KosDynWind_AFTER_THUNK(w)  (((KosDynNode *)(w))->u.wind.after_thunk)
#define KosDynWind_OB(w)           (&((KosDynNode *)(w))->u.wind.ob)
#define KosDynWind_ENTERED(w)      (((KosDynNode *)(w))->u.wind.entered)
#define KosDynWind_PHASE(w)        (((KosDynNode *)(w))->u.wind.phase)

#define KosDynTry_HANDLER_PC(r)      (((KosDynNode *)(r))->u.try_.handler_pc)
#define KosDynTry_ENABLED(r)         (((KosDynNode *)(r))->u.try_.enabled)
#define KosDynTry_EXC_SAVED(r)       (((KosDynNode *)(r))->u.try_.exc_saved)
#define KosDynTry_SAVED(r)           (&((KosDynNode *)(r))->u.try_.saved)
#define KosDynFinally_HANDLER_PC(r)  (((KosDynNode *)(r))->u.finally_.handler_pc)
#define KosDynFinally_ENABLED(r)     (((KosDynNode *)(r))->u.finally_.enabled)
#define KosDynFinally_HANDLING_RAISE(r) (((KosDynNode *)(r))->u.finally_.handling_raise)
#define KosDynFinally_HANDLING_RETURN(r) (((KosDynNode *)(r))->u.finally_.handling_return)
#define KosDynFinally_HANDLING_RESULT(r) (((KosDynNode *)(r))->u.finally_.handling_result)
#define KosDynFinally_EXC_SAVED(r)   (((KosDynNode *)(r))->u.finally_.exc_saved)
#define KosDynFinally_SAVED(r)       (&((KosDynNode *)(r))->u.finally_.saved)
#define KosDynFinally_HANDLING(r)    (&((KosDynNode *)(r))->u.finally_.handling)
#define KosDynLoop_BREAK_PC(r)       (((KosDynNode *)(r))->u.loop.break_pc)
#define KosDynLoop_CONTINUE_PC(r)    (((KosDynNode *)(r))->u.loop.continue_pc)
#define KosDynLoop_ENABLED(r)        (((KosDynNode *)(r))->u.loop.enabled)
#define KosDynLoop_RESULT(r)         (((KosDynNode *)(r))->u.loop.result)
#define KosDynForeach_BREAK_PC(r)    (((KosDynNode *)(r))->u.foreach.break_pc)
#define KosDynForeach_CONTINUE_PC(r) (((KosDynNode *)(r))->u.foreach.continue_pc)
#define KosDynForeach_ENABLED(r)     (((KosDynNode *)(r))->u.foreach.enabled)
#define KosDynForeach_RESULT(r)      (((KosDynNode *)(r))->u.foreach.result)
#define KosDynForeach_ITER(r)        (((KosDynNode *)(r))->u.foreach.iter)
#define KosDynForeach_EXHAUSTED(r)   (((KosDynNode *)(r))->u.foreach.exhausted)
#define KosDynForeach_ITER_NEXT_ELSE_PC(r) (((KosDynNode *)(r))->u.foreach.iter_next_else_pc)
#define KosDynForeach_REQ(r)         (&((KosDynNode *)(r))->u.foreach.req)

#define KosDynNext_DEFAULT_VAL(nd) (((KosDynNode *)(nd))->u.next.default_val)
#define KosDynNext_N(nd) (((KosDynNode *)(nd))->u.next.n)
#define KosDynNext_REQ(nd) (&((KosDynNode *)(nd))->u.next.req)

/**
   Yield-from delegate classification. Values stored in KosDynYieldFrom kind.
   Must stay aligned with yield_from_normalize in opcode/YIELD_FROM.c
*/
enum {
  KOS_YF_SYNC_ITER = 0,
  KOS_YF_SYNC_GEN = 1,
  KOS_YF_PY_ASYNC_ITER = 2,
  KOS_YF_PY_ASYNC_GEN = 3,
  KOS_YF_KOS_ITER = 4,
};


/**
   Pending action for yield-from resume inject. Must stay aligned with
   opcode/YIELD_FROM.c handler
*/
enum {
  KOS_YF_STEP = 0,
  KOS_YF_SEND = 1,
  KOS_YF_THROW = 2,
  KOS_YF_CLOSE = 3,
};


#define KosDynYieldFrom_DELEGATE(n)     (((KosDynNode *)(n))->u.yield_from.delegate)
#define KosDynYieldFrom_KIND(n)         (((KosDynNode *)(n))->u.yield_from.kind)
#define KosDynYieldFrom_PENDING_ACTION(n) (((KosDynNode *)(n))->u.yield_from.pending_action)
#define KosDynYieldFrom_PENDING_ARG(n) (((KosDynNode *)(n))->u.yield_from.pending_arg)
#define KosDynYieldFrom_PENDING_ARG_TYPE(n) (((KosDynNode *)(n))->u.yield_from.pending_arg_type)
#define KosDynYieldFrom_INFLIGHT(n)    (((KosDynNode *)(n))->u.yield_from.inflight)
#define KosDynYieldFrom_REQ(n)         (&((KosDynNode *)(n))->u.yield_from.req)
#define KosDynYieldFrom_DRIVE(n)       (((KosDynNode *)(n))->u.yield_from.drive)
#define KosDynYieldFrom_RESUME(n)      (((KosDynNode *)(n))->u.yield_from.resume)
#define KosDynYieldFrom_RESUME_EMIT(n) (((KosDynNode *)(n))->u.yield_from.resume_emit)

/**
   The iteration request record embedded in a receiver-capable node, or
   NULL for any other kind. The delivery machinery reads the stepped
   iterator and the in-flight mode from here and nowhere else.
*/
static inline KosIterRequest *kos_dyn_iter_request(KosDynNode *node) {

  switch (KosDynNode_KIND(node)) {
    case KOS_DYN_FOREACH:
      return KosDynForeach_REQ(node);
    case KOS_DYN_NEXT:
      return KosDynNext_REQ(node);
    case KOS_DYN_YIELD_FROM:
      return KosDynYieldFrom_REQ(node);
    default:
      return NULL;
  }
}


/**
   Set a iteration node's request mode. For FOREACH the
   KOS_DYN_CAP_ITER_RECEIVER capability stays in lockstep: the
   node is a receiving iteration node exactly while a step is in flight.
   NEXT and YIELD_FROM (KOS_ITER kind) carry the capability for their
   whole lifetime. All mode writes go through this.
*/
static inline void kos_dyn_iter_request_set_mode(
    KosDynNode *node, uint8_t mode) {

  KosIterRequest *req = kos_dyn_iter_request(node);

  req->mode = mode;
  if (KosDynNode_KIND(node) == KOS_DYN_FOREACH) {
    if (mode != KOS_IREQ_IDLE)
      KosDynNode_CAPS(node) |= KOS_DYN_CAP_ITER_RECEIVER;
    else
      KosDynNode_CAPS(node) &= ~KOS_DYN_CAP_ITER_RECEIVER;
  }
}


/**
   Arm a iteration node's request: record the stepped iterator (strong
   ref) and the in-flight mode
*/
static inline void kos_dyn_iter_request_arm(
    KosDynNode *node, PyObject *iter, uint8_t mode) {

  KosIterRequest *req = kos_dyn_iter_request(node);
  PyObject *prior = req->iter;

  Py_XINCREF(iter);
  kos_dyn_iter_request_bind_iter(req, iter);
  Py_XDECREF(prior);
  kos_dyn_iter_request_set_mode(node, mode);
}


#define KosDynParam_BINDINGS(n)    (((KosDynNode *)(n))->u.param.bindings)
#define KosDynParam_SAVED(n)       (((KosDynNode *)(n))->u.param.saved)


#define KOS_WITH_PHASE_IDLE                0
#define KOS_WITH_PHASE_PENDING_EXIT_AWAIT  1
#define KOS_WITH_PHASE_PENDING_ENTER_AWAIT 2
/** terminal: the extent has been left; every further leave is a no-op */
#define KOS_WITH_PHASE_LEFT                3

/** Multi-shot (dyn-with); participates in install wind */
#define KOS_DYN_WITH_SHOT_MULTI 0u
/** Single-shot; __exit__ not yet consumed for extent pairing */
#define KOS_DYN_WITH_SHOT_SINGLE_ACTIVE 1u
/** Single-shot; __exit__ already consumed; ignore further protocol */
#define KOS_DYN_WITH_SHOT_SINGLE_DONE 2u

/** enter is a plain function: call it with mgr as the first argument */
#define KOS_WITH_SELF_ENTER 1u
/** exit is a plain function: call it with mgr as the first argument */
#define KOS_WITH_SELF_EXIT  2u

#define KosDynWith_MGR(n)          (((KosDynNode *)(n))->u.with.mgr)
#define KosDynWith_ENTER(n)        (((KosDynNode *)(n))->u.with.enter)
#define KosDynWith_EXIT(n)         (((KosDynNode *)(n))->u.with.exit)
#define KosDynWith_SELF_BIND(n)    (((KosDynNode *)(n))->u.with.self_bind)
#define KosDynWith_ENTERED(n)      (((KosDynNode *)(n))->u.with.entered)
#define KosDynWith_PHASE(n)        (((KosDynNode *)(n))->u.with.phase)
#define KosDynWith_SHOT(n)         (((KosDynNode *)(n))->u.with.shot)
#define KosDynWith_EXIT_DONE(n)    (((KosDynNode *)(n))->u.with.exit_done)
#define KosDynWith_NAME(n)         (((KosDynNode *)(n))->u.with.name)
#define KosDynWith_RESUME_PC(n)    (((KosDynNode *)(n))->u.with.resume_pc)
#define KosDynWith_OB(n)           (&((KosDynNode *)(n))->u.with.ob)
#define KosDynWith_SWALLOWED(n)    (((KosDynNode *)(n))->u.with.swallowed)

/**
   True while __enter__ or __exit__ has been started but has not
   returned: it suspended on an awaitable, or it is a callable whose
   frame is installed and has yet to run. A pending phase means in flight
   and nothing else; kos_dyn_extent_settle advances it once the call has
   returned, kos_dyn_extent_abort once the call has died.
*/
#define KosDynWith_ENTER_PENDING(n) \
  (KosDynWith_PHASE(n) == KOS_WITH_PHASE_PENDING_ENTER_AWAIT)
#define KosDynWith_EXIT_PENDING(n) \
  (KosDynWith_PHASE(n) == KOS_WITH_PHASE_PENDING_EXIT_AWAIT)
#define KosDynWith_LEFT(n) \
  (KosDynWith_PHASE(n) == KOS_WITH_PHASE_LEFT)


/* === Unit lifecycle === */


int koskari_dynchain_init(PyObject *mod);
int koskari_dynchain_clear(PyObject *mod);


#endif


// The end.
