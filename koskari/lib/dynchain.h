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
   WIND, REGION, CC_RAISE, and others) that form a unified parent
   chain, replacing the separate frame and handler chains.

   :author: Christopher O'Brien <obriencj@preoccupied.net>
   :license: GNU General Public License v3
*/


#ifndef KOSKARI_DYNCHAIN_H
#define KOSKARI_DYNCHAIN_H


#include <Python.h>
#include <stdint.h>


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
   KOS_DYN_SHIFT     - Shift handler block (push stack block; no frame/namespace)
   KOS_DYN_CC_RAISE  - Sentinel node holding a KoskariContinuationApply exception
                       for the foreign-token transfer path; its unwind re-raises the
                       stored exception so the eval loop exits with KOS_RES_RAISE
   KOS_DYN_GEN       - Generator boundary; intercepts handle(YIELD/RETURN/RAISE),
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
  KOS_DYN_CC_RAISE,
  KOS_DYN_GEN,
  /**
   KOS_DYN_NEXT - Iterator boundary for standalone NEXT opcode over Koskari
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
  KOS_DYNF_BOUNDARY    = 0x02,  /* node marks a non-resumable boundary */
  KOS_DYNF_GEN_KEEPALIVE = 0x04,  /* GEN: holds a counted ref on its KPAG owner while active */
  KOS_DYNF_INVOCATION  = 0x08,  /* PROMPT: instance created by a k-invocation
                                   (graft instantiation or in-place invoke);
                                   no POP_PROMPT instruction pairs with it, so
                                   delivery routes through the paired DELIM */
  KOS_DYNF_LCA_VISITED = (int) 0x80000000u,  /* transient: set during kos_compute_lca */
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
#define KOS_DYN_CAP_BARRIER                  (1u << 7)
#define KOS_DYN_CAP_HANDLES_EXCEPTIONS       (1u << 8)
/** Iterator yield/return boundary (KOS_DYN_NEXT, extended FOREACH stepping) */
#define KOS_DYN_CAP_RECEIVES_ITER_BOUNDARY     (1u << 9)
/** Node participates in raise-walk dispatch (handle/restitch); the fast
   first check before kos_dyn_handle. Distinct from HANDLES_EXCEPTIONS,
   which marks failed-pop recovery (kos_dyn_exception_unwind). */
#define KOS_DYN_CAP_INTERCEPTS_RAISE         (1u << 10)

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


/** FOREACH: ITER_NEXT in-flight step (phase gating for boundary events):
    a flattened Koskari async-gen step, or a suspended python async
    iterator step being driven through the pending context await */
#define KOS_FOREACH_ITER_STEP_IDLE               0u
#define KOS_FOREACH_ITER_STEP_AWAITING_KOS_AGEN  1u
#define KOS_FOREACH_ITER_STEP_AWAITING_PY        2u


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
} KosXferEvent;


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
  KosStackBlock *stack; /* owned operand-stack extent; NULL if none (inherit ancestor) */
  PyObject *ns;         /* owned namespace extent; NULL if none (inherit ancestor) */

  union {
    struct {
      KosCode *code;      /* strong reference; NULL only for placeholder frames */
      uint32_t pc;        /* next instruction */
      uint32_t acting_pc; /* instruction currently being executed */
      uint32_t frame_flags;
      /* Native frame evaluator: KosNativeHandlerFn (see eval.h); void * to
         avoid the include-order constraint of naming KosOpResult here. */
      void *native_handler;
    } frame;
    struct {
      uint32_t prompt_id;
      /* pc of the PUSH_PROMPT that created this node, in its own frame's
         code. A cross-frame POP_SHIFT resolves the PROMPT extent with
         this: the frame's live acting pc has moved past the extent by
         the time the shift body runs. */
      uint32_t push_pc;
      PyObject *pending_shift_wrapper;  /* KosPyDelimitedContinuation when PUSH_SHIFT plan completed; owned */
    } prompt;
    struct {
      uint8_t mode;       /* KOS_DYN_DELIM_MODE_* */
      KosDynNode *chain;  /* owned when mode == INPLACE */
      uint32_t pc;
      KosDynNode *frame;
      KosDynNode *node;
      /* INPLACE only: displaced resume-frame state (stack owned) and
         the invoke-site acting pc for extent lookups after restitch */
      KosStackBlock *saved_stack;
      uint32_t saved_pc;
      uint32_t saved_acting_pc;
    } delim;
    struct {
      uint32_t resume_pc;
    } cc;
    struct {
      PyObject *before_thunk;  /* callable (owned) or NULL */
      PyObject *after_thunk;   /* callable (owned) or NULL */
      PyObject *body_result;   /* TOS at POP_WIND (owned) or NULL; error/bypass path */
      uint8_t entered;
      uint8_t phase;          /* KosDynWindPhase; WIND_IDLE until thunk invoked */
    } wind;
    struct { uint32_t handler_pc; uint8_t enabled; } try_;
    struct {
      uint32_t handler_pc;
      uint8_t enabled;
      uint8_t handling_raise;
      uint8_t handling_return;
      PyObject *handling_result;
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
      uint8_t iter_step_phase;  /* KOS_FOREACH_ITER_STEP_* */
    } foreach;
    struct {
      PyObject *delegate;
      uint8_t kind;
      uint8_t pending_action;
      uint8_t phase;  /* KosYfPhase: DRIVING, EXPECT_SEND, EXPECT_AWAIT */
      PyObject *pending_arg;
      PyObject *pending_arg_type;  /* for KOS_YF_THROW: exception type */
      PyObject *inflight;
    } yield_from;
    struct {
      PyObject *bindings;  /* PyDict: key -> value (what we bind; keys from ParamRef) */
      PyObject *saved;     /* PyDict: key -> value or SENTINEL_UNSET (filled at enter, used at restore; NULL if not yet entered) */
    } param;
    struct {
      PyObject *mgr;       /* context manager (owned) */
      PyObject *enter;     /* resolved enter callable (owned; NULL if not cached) */
      PyObject *exit;      /* resolved exit callable (owned) */
      uint8_t entered;     /* 1 after enter succeeded */
      uint8_t phase;       /* KOS_WITH_PHASE_IDLE, PENDING_EXIT_AWAIT, or PENDING_ENTER_AWAIT */
      uint8_t shot;        /* KOS_DYN_WITH_SHOT_*; internal to dyn_with only */
      PyObject *exit_done; /* KosRef shared across clones (owned); non-empty
                              once the single-shot __exit__ has run. NULL for
                              multi-shot */
      PyObject *name;      /* binding name or NULL (owned) */
      uint32_t resume_pc;  /* PC to jump to after with body (pop_pc + 1) */
      PyObject *exc_typ;   /* owned; only when phase == PENDING_EXIT_AWAIT */
      PyObject *exc_val;
      PyObject *exc_tb;
      int normal_exit;     /* 1 = exit(None,None,None); only when phase == PENDING_EXIT_AWAIT */
    } with;
    struct {
      /* shift: marker; operands stay on the frame (PROMPT install has no
         stack). prompt_node_id is the instance id of the PROMPT this shift
         captured against; POP_SHIFT resolves its delimiter by this, never
         by chain position. */
      uint32_t prompt_node_id;
    } shift;
    struct {
      PyObject *exc_type;  /* exception type from PyErr_Fetch */
      PyObject *exc_val;   /* exception value */
    } cc_raise;
    struct {
      PyObject *owner;     /* owning generator object (borrowed) or NULL */
      struct KosDynNode *active; /* suspended frame when released; NULL when running */
      uintptr_t refcount;  /* reference count; init 1; release decrements; free when 0 */
    } gen;
    struct {
      PyObject *agen_obj;   /* KosPyAsyncGen, strong ref */
      PyObject *default_val; /* owned; 2-arg NEXT */
      int n;                 /* 1 or 2 */
    } next;
  } u;
};

typedef struct KosDynNode KosDynFrame;


/**
   KosTransitionKind selects which existing dynchain walk or transfer
   kos_dyn_transition dispatches to.  Payload and target fields in
   KosTransitionReq are interpreted per kind.

   Continuation transfer plans (kos_run_transfer_step,
   run_transfer_plan_to_completion, continuation return) remain orchestrated
   from eval.c; they are
   not yet modeled as transition kinds.  A future phase may map install or
   resume steps to dedicated request values without changing plan semantics.
*/
typedef enum KosTransitionKind {
  KOS_TRANS_WALK_RETURN_TO_FRAME = 0,
  KOS_TRANS_UNWIND_TO_FRAME,
  KOS_TRANS_UNWIND_TO_NODE,
  KOS_TRANS_WALK_HANDLE_YIELD,
  KOS_TRANS_TRANSFER_TO_LOOP,
} KosTransitionKind;


/**
   Normalized outcome from kos_dyn_transition for callers that should not
   branch on raw helper return codes.
*/
typedef enum KosTransitionStatus {
  KOS_TRANS_STATUS_OK = 0,
  KOS_TRANS_STATUS_ERROR,
  KOS_TRANS_STATUS_ASYNC_UNWIND,
  KOS_TRANS_STATUS_FINALLY_TAKING_RETURN,
} KosTransitionStatus;


/**
   Request for kos_dyn_transition.  Unused pointer fields must be NULL.

   :param kind: which operation to run
   :param xfer_event: for UNWIND_TO_FRAME, UNWIND_TO_NODE, TRANSFER_TO_LOOP
   :param target_frame: WALK_RETURN_TO_FRAME, UNWIND_TO_FRAME
   :param target_node: UNWIND_TO_NODE
   :param payload: borrowed reference for WALK_RETURN_TO_FRAME (return
     value); TRANSFER_TO_LOOP steals or uses per kos_dyn_transfer_to_loop
*/
typedef struct KosTransitionReq {
  KosTransitionKind kind;
  KosXferEvent xfer_event;
  KosDynFrame *target_frame;
  KosDynNode *target_node;
  PyObject *payload;
} KosTransitionReq;


/**
   Result from kos_dyn_transition.  :c:member:`raw` holds the legacy int
   return from the underlying helper when useful for debugging.
*/
typedef struct KosTransitionResult {
  KosTransitionStatus status;
  int raw;
} KosTransitionResult;


/**
   TRY/FINALLY handler walk used by process_raise (the unified RAISE
   ingress in eval.c). Implemented in dynchain.c.
*/

KosOpResult kos_dyn_try_local_raise_body(KosEvalState *es);


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
typedef struct KosDynNode KosDynCCRaise;
typedef struct KosDynNode KosDynGen;
typedef struct KosDynNode KosDynNext;


/* === KosContinuation: captured delimited continuation === */


/**
   KosContinuation holds a captured delimited continuation.

   The captured_chain is a cloned segment of the dynamic chain from the
   capture point down through the prompt and its owning FRAME node
   (delimited), or from above the CC block down to root (undelimited).

   The resume_frame points to the frame where execution resumes.
   The resume_pc is the program counter to resume at.
   The resume_stack points at the operand stack owned by resume_frame
   (or the innermost region with a stack on captured_chain); it is an
   alias for that node-owned block, not separately released here.

   For multi-shot continuations, the chain/stack are cloned on
   each invocation. For one-shot, they are consumed directly.
*/

struct KosContinuation {
  KosDynNode *captured_chain;   /* cloned chain segment */
  KosDynFrame *resume_frame;    /* frame to resume into */
  uint32_t resume_pc;           /* pc to resume at */
  KosStackBlock *resume_stack;  /* aliases node-owned stack on captured_chain */
  uint32_t prompt_id;           /* target prompt tag */
  uint32_t graft_point_id;      /* node_id of graft point for ID fallback; 0 = none */
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
#define KosContinuation_RESUME_FRAME(k)    (((KosContinuation *)(k))->resume_frame)
#define KosContinuation_RESUME_PC(k)       (((KosContinuation *)(k))->resume_pc)
#define KosContinuation_RESUME_STACK(k)    (((KosContinuation *)(k))->resume_stack)
#define KosContinuation_PROMPT_ID(k)       (((KosContinuation *)(k))->prompt_id)
#define KosContinuation_GRAFT_POINT_ID(k)  (((KosContinuation *)(k))->graft_point_id)
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


/* === KosTransferStep enum === */


/**
   KosTransferStep identifies the type of action in a transfer plan.

   KOS_XFER_EXTENT_ACTION - Run extent wind (enter) or unwind (exit) for a
   node.  Stores (node, phase).  Replaces the former RUN_HOOK and
   NODE_UNWIND step types.

   KOS_XFER_INSTALL       - Install a chain segment as current
   KOS_XFER_INJECT        - Push resume value onto stack (or throw)
   KOS_XFER_POP_ONE       - Run unwind for current top, unlink and free
   KOS_XFER_PUSH          - Append segment to current chain (graft bottom to
                            current dyn_top).  Used for delimited continuation
                            invoke; caller stays on chain.
   KOS_XFER_TRUNCATE_AND_INJECT - Free from current top through node
                            (inclusive), set dyn_top to node's parent, push value.
*/

#define KOS_XFER_EXTENT_PHASE_WIND   0
#define KOS_XFER_EXTENT_PHASE_UNWIND 1

typedef enum KosTransferStep {
  KOS_XFER_EXTENT_ACTION = 0,
  KOS_XFER_INSTALL       = 1,
  KOS_XFER_INJECT        = 2,
  KOS_XFER_POP_ONE       = 3,
  KOS_XFER_PUSH          = 4,
  KOS_XFER_TRUNCATE_AND_INJECT = 5,
} KosTransferStep;


/**
   Return value from kos_run_transfer_step: plan suspended on await.
   Step set step_awaitable on plan; driver resumes with send_val, which
   revive_plan stores in step_resume_inject.
*/
#define KOS_XFER_SUSPENDED_AWAIT 2


/* === KosTransferAction: single transfer step === */


/**
   KosTransferAction represents one step in a transfer plan.
   The step field determines which union member is active.
*/

typedef struct KosTransferAction {
  KosTransferStep step;

  union {
    struct {
      KosDynNode *node;
      int phase;  /* KOS_XFER_EXTENT_PHASE_WIND or KOS_XFER_EXTENT_PHASE_UNWIND */
    } extent_action;

    struct {
      KosDynNode *segment;
    } install;

    struct {
      PyObject *value;
      PyObject *exc_type;  /* for is_throw: exception type */
      int is_throw;
    } inject;

    struct {
      KosDynNode *node;
      PyObject *value;
      PyObject *exc_type;  /* for is_throw: exception type */
      int is_throw;
    } truncate_and_inject;
  } data;

} KosTransferAction;


#define KosTransferAction_STEP(a)              (((KosTransferAction *)(a))->step)
#define KosTransferAction_EXTENT_NODE(a)       (((KosTransferAction *)(a))->data.extent_action.node)
#define KosTransferAction_EXTENT_PHASE(a)      (((KosTransferAction *)(a))->data.extent_action.phase)
#define KosTransferAction_INSTALL_SEGMENT(a)   (((KosTransferAction *)(a))->data.install.segment)
#define KosTransferAction_INJECT_VALUE(a)      (((KosTransferAction *)(a))->data.inject.value)
#define KosTransferAction_INJECT_TYPE(a)       (((KosTransferAction *)(a))->data.inject.exc_type)
#define KosTransferAction_INJECT_IS_THROW(a)   (((KosTransferAction *)(a))->data.inject.is_throw)
#define KosTransferAction_TRUNCATE_NODE(a)     (((KosTransferAction *)(a))->data.truncate_and_inject.node)
#define KosTransferAction_TRUNCATE_VALUE(a)    (((KosTransferAction *)(a))->data.truncate_and_inject.value)
#define KosTransferAction_TRUNCATE_TYPE(a)     (((KosTransferAction *)(a))->data.truncate_and_inject.exc_type)
#define KosTransferAction_TRUNCATE_IS_THROW(a) (((KosTransferAction *)(a))->data.truncate_and_inject.is_throw)


/* === KosTransferPlan: continuation transfer execution plan === */


#define KOS_XFER_PLAN_DEFAULT_ACTIONS 32


/**
   KosTransferPlan holds the action list for a continuation transfer.

   Plan and actions are a single allocation. Default capacity is 32;
   realloc the whole block when more are needed. One cached plan is
   kept for reuse.

   Actions are executed in order from index 0 to action_count-1.
   The action_index tracks the next action to execute.

   If a hook invokes another continuation, the current plan is
   suspended and pushed onto the suspended stack. On completion,
   the suspended plan is resumed.

   The original_target is retained for future replanning when nested
   transfers change the dynamic chain during a suspended plan.
*/

struct KosTransferPlan {
  KosTransferPlan *suspended;   /* outer plan if nested */

  size_t action_count;          /* total actions */
  size_t action_index;          /* next action index */
  size_t action_capacity;       /* slots allocated (>= action_count) */

  KosDynNode *original_target;  /* target for chain-change detection */
  uint32_t graft_point_id;      /* node_id for LCA fallback when replanning */
  uint32_t resume_pc;           /* UINT32_MAX = not set; set for invocation */
  KosDynFrame *resume_frame;    /* frame to resume into (invocation only) */
  KosStackBlock *resume_stack;  /* stack state at capture (invocation only) */
  PyObject *continuation_ref;   /* strong ref (Py_INCREF) until plan freed or moved to prompt */
  KosContinuation *invoke_clone;  /* when set, release then free when plan freed */

  /* Stateful step: when current step suspended on await, store awaitable and resume state */
  PyObject *step_awaitable;     /* awaitable when step suspended (owned) */
  PyObject *step_resume_inject; /* value or exception to inject when resuming step (owned) */

  KosTransferAction actions[];  /* flexible array; size = action_capacity */
};


#define KosTransferPlan_SUSPENDED(p)        (((KosTransferPlan *)(p))->suspended)
#define KosTransferPlan_ACTIONS(p)          (((KosTransferPlan *)(p))->actions)
#define KosTransferPlan_ACTION_COUNT(p)     (((KosTransferPlan *)(p))->action_count)
#define KosTransferPlan_ACTION_INDEX(p)     (((KosTransferPlan *)(p))->action_index)
#define KosTransferPlan_ACTION_CAPACITY(p)  (((KosTransferPlan *)(p))->action_capacity)
#define KosTransferPlan_ORIGINAL_TARGET(p)  (((KosTransferPlan *)(p))->original_target)
#define KosTransferPlan_GRAFT_POINT_ID(p)   (((KosTransferPlan *)(p))->graft_point_id)
#define KosTransferPlan_RESUME_PC(p)       (((KosTransferPlan *)(p))->resume_pc)
#define KosTransferPlan_RESUME_FRAME(p)    (((KosTransferPlan *)(p))->resume_frame)
#define KosTransferPlan_RESUME_STACK(p)    (((KosTransferPlan *)(p))->resume_stack)
#define KosTransferPlan_CONTINUATION_REF(p) (((KosTransferPlan *)(p))->continuation_ref)
#define KosTransferPlan_INVOKE_CLONE(p)    (((KosTransferPlan *)(p))->invoke_clone)
#define KosTransferPlan_STEP_AWAITABLE(p)  (((KosTransferPlan *)(p))->step_awaitable)
#define KosTransferPlan_STEP_RESUME_INJECT(p) (((KosTransferPlan *)(p))->step_resume_inject)


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
   :param stack: operand stack (ownership transferred)

   :return: new frame node, or NULL on error
*/
KosDynFrame *kos_dynframe_new(
   KosDynNode *parent,
   KosCode *code,
   PyObject *ns,
   KosStackBlock *stack);


/**
   Clone a frame node for continuation capture.

   Clones the frame's stack using DeepCopy. Does NOT clone
   the parent chain; use kos_clone_chain_segment for that.

   :param frame: frame to clone

   :return: cloned frame, or NULL on error
*/
KosDynFrame *kos_dynframe_clone(KosDynFrame *frame);


/**
   Free a frame node and release its resources.

   Releases the stack blocks, DECREFs the namespace.
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
KosDynNode *kos_clone_chain_segment(
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
   kos_dyn_retarget_after_pop (incremental parent pop or full rebuild).
   Ordinary pushes must use kos_dyn_push or kos_dyn_push_noinstall instead.
*/
void kos_dyn_commit_dyn_top(KosEvalState *es, KosDynNode *new_top);


/**
   Full chain walk: recompute live frame, block, and namespace on es from
   dyn_top, then snapshot onto ctx.

   Call from outside the dynchain TU when eval lifecycle or opcodes must refresh
   live fields after a non-standard chain edit (KosContext_New bootstrap, tail
   koscall, stack assigned on dyn_top after push). Inside dynchain.c, xfer
   INSTALL/PUSH, gen install fallback, and kos_dyn_retarget_after_pop when the
   edit is not a single-head parent pop use this instead of incremental retarget.
   Do not call on every opcode; ordinary pushes and pops go through
   kos_dyn_push and kos_dyn_commit_dyn_top.
*/
void kos_dyn_rebuild_live_from_chain(KosEvalState *es);


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


void kos_dynframe_reset_for_tail(KosDynFrame *frame);


/**
   Create a generator boundary node.  The generator object owns the node;
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
int kos_dyn_gen_unwind_exhausted(KosEvalState *es);


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

   :return: new prompt, or NULL on error
*/
KosDynPrompt *kos_dyn_push_prompt(KosEvalState *es, uint32_t prompt_id);


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
   Complete a delimited POP_PROMPT return after kos_dyn_transition succeeds
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
   Build a transfer plan that unwinds all frames in the context to the root
   (NULL frame), running WITH/finally teardown along the way, then re-raises
   the stored KoskariContinuationApply exception via a KosDynCCRaise sentinel
   node inserted at the bottom of the chain.

   This is the foreign-token path for cross-context continuation invocation:
   the current eval context cannot apply the continuation directly, so it
   performs a segmented transfer by unwinding its dynamic chain completely and
   exiting with the original exception so it can propagate further upward.

   Inserts a KosDynCCRaise node at the root of the chain (below all existing
   nodes) before building the plan.  The node's unwind callback re-raises
   exc_val after all WITH/finally teardowns complete.

   :param ctx: execution context
   :param exc_val: the KoskariContinuationApply exception value (new ref stolen)
   :param out_plan: output for the constructed transfer plan

   :return: 0 on success, -1 on error
*/
int kos_build_cc_unwind_plan(
    KosEvalState *es,
    PyObject *exc_type,
    PyObject *exc_val,
    KosTransferPlan **out_plan);


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
   Pop a wind extent from the dynamic chain.

   This does NOT automatically run the after hook; the caller must
   handle that via the transfer plan or explicit hook execution.

   :param ctx: execution context
   :param wind: wind to pop (must be dyn_top)

   :return: 0 on success, -1 on error
*/
int kos_dyn_pop_wind(KosEvalState *es, KosDynWind *wind);


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


int kos_dyn_push_next_boundary(
    KosEvalState *es, PyObject *iter_obj, PyObject *default_val, int n);

void kos_dyn_next_abort_after_failed_attach(KosEvalState *es);

void kos_dyn_unlink_next_node(KosEvalState *es, KosDynNext *node);

int kos_dyn_next_deliver_yield(
    KosEvalState *es, KosDynNext *next, PyObject *val);

int kos_dyn_next_deliver_return_node(
    KosEvalState *es,
    KosDynNext *next,
    PyObject *val);

int kos_dyn_next_deliver_exhausted(
    KosEvalState *es,
    KosDynNext *next,
    PyObject *val);

int kos_dyn_foreach_deliver_yield_kos_agen(
    KosEvalState *es, KosDynForeach *fe, PyObject *val);

int kos_dyn_foreach_deliver_return_kos_agen(
    KosEvalState *es, KosDynForeach *fe, PyObject *val);

int kos_dyn_foreach_deliver_exhausted(
    KosEvalState *es, KosDynForeach *fe, PyObject *val);

int kos_dyn_yield_from_deliver_inner_yield(
    KosEvalState *es, KosDynYieldFrom *yf,
    PyObject *inner_val);

int kos_dyn_yield_from_deliver_inner_return(
    KosEvalState *es, KosDynYieldFrom *yf, PyObject *val);

int kos_dyn_yield_from_deliver_exhausted(
    KosEvalState *es, KosDynYieldFrom *yf, PyObject *val);

KosDynNext *kos_dyn_next_clone(KosDynNext *node);


/* === Generic iterator-boundary dispatch (dyn_boundary.c) === */


/* Result codes for kos_dyn_iter_boundary_deliver_return: how the eval
   loop should proceed after a delegate-return delivery. */
#define KOS_ITER_BOUNDARY_DELIVERED  0   /* delivered; REPROLOGUE */
#define KOS_ITER_BOUNDARY_ERROR    (-1)  /* hard failure; PyErr set */
#define KOS_ITER_BOUNDARY_RAISE      1   /* PyErr set; route to raise ingress */

/**
   Locate the nearest receiving iterator boundary from active chain state.

   A node is a receiving boundary iff it carries
   KOS_DYN_CAP_RECEIVES_ITER_BOUNDARY -- the capability is the single
   routing key; no node-kind checks.  NEXT and flattened-delegate
   YIELD_FROM nodes carry it for their whole lifetime; FOREACH toggles it
   with the ITER_NEXT step phase (kos_dyn_foreach_set_iter_step_phase).
   Future receiver kinds (e.g. a GEN-carried yield-from passthru field)
   join by carrying the capability.

   dyn_top is preferred: after a yield freeze the boundary that was
   directly driving the generator sits at dyn_top.  Otherwise walk
   outward from the running frame and return the nearest receiver.

   :return: borrowed node, or NULL when no receiver is active
*/
KosDynNode *kos_dyn_find_iter_boundary(KosEvalState *es);

/**
   Deliver a boundary YIELD value to a receiver located by
   kos_dyn_find_iter_boundary.  val ownership transfers to the consumer.

   :return: 0 on delivery (caller should REPROLOGUE); -1 on error
*/
int kos_dyn_iter_boundary_deliver_yield(
    KosEvalState *es, KosDynNode *node, PyObject *val);

/**
   Deliver a delegate-return (exhaustion) to the receiving boundary that
   sits directly above the returning GEN.  val consumed.

   :return: KOS_ITER_BOUNDARY_DELIVERED, _ERROR, or _RAISE
*/
int kos_dyn_iter_boundary_deliver_return(
    KosEvalState *es, KosDynNode *node, PyObject *val);

/**
   Deliver exhaustion to a receiving boundary whose flattened generator
   was already finished (raise-walk StopIteration conversion): the
   node-specific exhaustion tail without the GEN unwind. val consumed.

   :return: 0 delivered; -1 with PyErr set when the boundary re-emits
       (e.g. 1-arg NEXT); -1 without PyErr on internal failure
*/
int kos_dyn_iter_boundary_deliver_exhausted(
    KosEvalState *es, KosDynNode *node, PyObject *val);

/**
   The exception type that ends this boundary's in-flight step:
   StopIteration for a flattened Koskari agen step, StopAsyncIteration
   for a suspended python async iterator step.
*/
PyObject *kos_dyn_iter_boundary_exhaustion_exc(KosDynNode *node);

/**
   True when the boundary's in-flight step is a suspended python async
   iterator being driven through the pending context await.
*/
int kos_dyn_iter_boundary_awaiting_py(KosDynNode *node);

/**
   The Koskari async generator object a receiving boundary is driving.

   :return: borrowed KosPyAsyncGen reference as PyObject*
*/
PyObject *kos_dyn_iter_boundary_agen(KosDynNode *node);

/**
   Clear a receiving boundary after its driven generator was torn down on
   an error path: unlink NEXT / YIELD_FROM nodes, idle a FOREACH step.
*/
void kos_dyn_iter_boundary_abort(KosEvalState *es, KosDynNode *node);


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
   step (e.g. KosStackBlock_PushBlock) fails. region must be the pointer
   returned from the corresponding push.

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
   Push a with (context manager) extent onto the dynamic chain after sync
   enter. :c:member:`shot` is KOS_DYN_WITH_SHOT_MULTI or
   KOS_DYN_WITH_SHOT_SINGLE_ACTIVE.

   :return: new KosDynWith node, or NULL on error
*/
KosDynWith *kos_dyn_push_with(
    KosEvalState *es,
    PyObject *mgr,
    PyObject *enter,
    PyObject *exit,
    PyObject *name,
    uint32_t resume_pc,
    uint8_t shot);


/**
   Push a dyn-with (multi-shot) extent; same as kos_dyn_push_with with
   KOS_DYN_WITH_SHOT_MULTI.
*/
KosDynWith *kos_dyn_push_dyn_with(
    KosEvalState *es,
    PyObject *mgr,
    PyObject *enter,
    PyObject *exit,
    PyObject *name,
    uint32_t resume_pc);


/**
   Push a provisional WITH node (phase PENDING_ENTER_AWAIT) before calling
   enter. :c:member:`shot` selects multi vs single-shot caps.
*/
KosDynWith *kos_dyn_push_with_provisional(
    KosEvalState *es,
    PyObject *mgr,
    PyObject *enter,
    PyObject *exit,
    PyObject *name,
    uint32_t resume_pc,
    uint8_t shot);


/**
   Push provisional dyn-with; same as kos_dyn_push_with_provisional with
   KOS_DYN_WITH_SHOT_MULTI.
*/
KosDynWith *kos_dyn_push_dyn_with_provisional(
    KosEvalState *es,
    PyObject *mgr,
    PyObject *enter,
    PyObject *exit,
    PyObject *name,
    uint32_t resume_pc);


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
   Unwind helper for WITH: call exit(mgr, exc_type, exc_val, tb) with
   current exception info. Sync only; returns 0 when done or -1 on error.

   :param ctx: execution context
   :param node: WITH node being unwound
   :param out_ev: event output for suspend (required when exit may be async)

   Uses KosEvalState_CallArgs for exit. When exit returns an awaitable, suspend
   happens inside KosEvalState_CallArgs; pending state is stashed and this returns 1.

   EVAL_AWAIT when __exit__ returned an awaitable (payload in .val).
   EVAL_REPROLOGUE when exception was swallowed (resume_pc updated; caller must
     not pop the WITH node).
   EVAL_NORMAL on success.
   EVAL_RAISE on error.
*/
KosOpResult kos_dyn_with_unwind(KosEvalState *es, KosDynWith *node);


/**
  Pop the top node of the dynamic chain with non-barrier policy.
  Namespace ownership is node-local; popping reveals next owner by chain walk.

  :param es: eval state
  :param event: transfer event; KOS_XFER_EVENT_NONE if unknown

  :return: EVAL_NORMAL on success, EVAL_RAISE on error or if top is a barrier,
    EVAL_AWAIT or EVAL_REPROLOGUE when async extent action started
*/
KosOpResult kos_dyn_pop_nonbarrier_top(
    KosEvalState *es,
    KosXferEvent event);


/**
  Pop the top node of the dynamic chain allowing barrier nodes.

  Internal prompt-crossing unwind/transfer paths use this variant.
*/
KosOpResult kos_dyn_pop_top_allow_barrier(
    KosEvalState *es,
    KosXferEvent event);


/**
  Non-barrier policy pop returning KosOpResult.
  EVAL_REPROLOGUE when a wind-thunk frame was started (AWAIT val dropped),
  EVAL_NORMAL on sync success, EVAL_RAISE on error.
*/
KosOpResult kos_dyn_pop_top(KosEvalState *es, KosXferEvent event);


int kos_dyn_unwind_to_frame(KosEvalState *es, KosDynFrame *frame,
                            KosXferEvent event);


/**
   Walk RETURN to frame, offering handle(RETURN) to each node. When FINALLY
   consumes, deliver (push value to block, set PC to finally), set
   handling_return, and return KOS_DYN_UNWIND_FINALLY_TAKING_RETURN. Caller
   must return 0; eval continues at finally. When finally does return,
   inner RETURN completes; when finally falls through, POP_FINALLY promotes
   value and bytecode continues (typically RETURN pops it).

   When the walk's top is a DELIM carrying a live invoke bookmark
   (RETURN_FRAME + RETURN_PC), the return has reached a reset boundary:
   the walk stops with the DELIM at dyn_top and returns
   KOS_DYN_UNWIND_DELIM_TAKING_RETURN; the caller routes val to the
   k-invocation site.

   :param val: return value; ownership transferred when FINALLY_TAKING
   :return: 0 (walk done), KOS_DYN_UNWIND_ASYNC_STARTED, -1,
     KOS_DYN_UNWIND_FINALLY_TAKING_RETURN, or
     KOS_DYN_UNWIND_DELIM_TAKING_RETURN
*/
int kos_dyn_walk_return_to_frame(KosEvalState *es, KosDynFrame *frame,
                                  PyObject *val);


int kos_dyn_unwind_to_node(KosEvalState *es, const KosDynNode *target,
                           KosXferEvent event);


/**
   Internal xfer-layer status: extent wind/unwind started async work (e.g.
   wind after hook, WITH __exit__ returning awaitable).  Caller must not
   pop the node yet; the evaluator runs the hook or awaits the exit then
   resumes unwinding.  Eval boundaries map this to EVAL_AWAIT(awaitable)
   or EVAL_REPROLOGUE() via KosEvalState_TransitionOp / kos_dyn_pop_top.
*/
#define KOS_DYN_UNWIND_ASYNC_STARTED 1

/** When exception swallowed: unwind did full cleanup (DropBlock, pop_with,
    push None); caller must return 0 without further action. */
#define KOS_DYN_UNWIND_SWALLOWED 2

/** When FINALLY consumed handle(RETURN): delivered to finally block, value
    pushed to block. Caller must return 0 (eval continues at finally). */
#define KOS_DYN_UNWIND_FINALLY_TAKING_RETURN 3

/** WITH skipped __exit__ (single-shot already consumed); not an error */
#define KOS_DYN_UNWIND_PROTOCOL_SKIPPED 4

/** When the return walk stopped at a DELIM with a live invoke bookmark
    (left at dyn_top).  Caller routes the return value to the
    k-invocation site; the value payload is NOT consumed by the walk. */
#define KOS_DYN_UNWIND_DELIM_TAKING_RETURN 5


/**
   Extent wind (enter) and unwind (exit) helpers.  Each dynamic extent
   kind has optional wind and unwind; the same helpers are used by the
   simple pop path and by the transfer plan.  Helpers perform only
   semantic work; the caller unlinks and frees the node.

   wind(ctx, node): enter the extent.  Return 0 on success, -1 on
   error, or KOS_DYN_UNWIND_ASYNC_STARTED if async (e.g. started
   before hook).

   unwind(ctx, node): exit the extent.  Return 0 when teardown
   complete, -1 on error, or KOS_DYN_UNWIND_ASYNC_STARTED if async
   exit code was started (caller must not pop until hook completes).
*/
int kos_dyn_param_wind(KosEvalState *es, KosDynParam *node);
void kos_dyn_param_unwind(KosEvalState *es, KosDynParam *node);

int kos_dyn_wind_wind(KosEvalState *es, KosDynWind *node);
int kos_dyn_wind_unwind(KosEvalState *es, KosDynWind *node);

KosOpResult kos_dynnode_on_control_returned(KosEvalState *es, KosDynNode *node);

KosDynNode *kos_dyn_find_pending_exit(KosDynNode *node);
int kos_dyn_complete_pending_exit(KosEvalState *es, int do_pop_node);


/**
   Generic dispatch functions for the seven dynchain operations.  Each
   switches on the node's kind and invokes the appropriate per-type
   implementation directly, allowing the compiler to emit a jump table
   with inlined handlers rather than an indirect function-pointer call.

   kos_dyn_install: run install-phase setup when a node is pushed.
   kos_dyn_wind: run wind (enter) for extent nodes during continuation transfer.
     Returns KosOpResult: AWAIT for async WITH enter, REPROLOGUE when a WIND
     before-thunk frame was started, NORMAL on sync success, RAISE on error.
   kos_dyn_unwind: run unwind (exit) for extent nodes; may start async.
     Returns KosOpResult: AWAIT when WITH __exit__ returned awaitable,
     REPROLOGUE when WITH swallowed the exception or WIND started async,
     NORMAL on success, RAISE on error.
   kos_dyn_exception_unwind: called when exception propagation reaches a
     HANDLES_EXCEPTIONS node. Returns EVAL_NORMAL to continue unwinding,
     EVAL_REPROLOGUE if handler applied, EVAL_AWAIT if async started,
     EVAL_RAISE on error.
   kos_dyn_clone: allocate a copy of the node; NULL return on error or if
     the type forbids cloning.
*/
int kos_dyn_install(KosEvalState *es, KosDynNode *node);
KosOpResult kos_dyn_wind(KosEvalState *es, KosDynNode *node);
KosOpResult kos_dyn_unwind(KosEvalState *es, KosDynNode *node,
                            KosXferEvent event);
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
   Walk the dynamic chain from DYN_TOP upward, offering handle(YIELD) to
   each node. Stops when a node returns CONSUMED (generator boundary).
   Used for YIELD transit: freezes the GEN node when found.

   :return: 0 when GEN consumed (valid yield; caller may return),
     -1 on error or if no generator boundary found
*/
int kos_dyn_walk_handle_yield(KosEvalState *es);

KosDynNode *kos_dyn_clone(KosDynNode *node);

void kos_dyn_ns_unwind(KosEvalState *es, KosDynNs *node);

void kos_dyn_yieldfrom_unwind(KosEvalState *es, KosDynYieldFrom *node);


/**
   Close the delegate of a YIELD_FROM dyn node (on unwind or error).
   Does not free the node. Callers: kos_dyn_yieldfrom_unwind, opcode
   YIELD_FROM.

   :param node: YIELD_FROM node, or NULL (no-op)
*/
void kos_yieldfrom_close_delegate(KosDynYieldFrom *node);


/**
   Find the innermost LOOP or FOREACH region on the dynamic chain,
   walking from dyn_top toward root. Used for break/continue.

   :param es: eval state

   :return: the topmost LOOP or FOREACH region, or NULL if none found
 */
KosDynNode *kos_find_node_for_loop(KosEvalState *es);


/**
   Transfer control to the innermost loop for BREAK or CONTINUE.
   Pops nodes from dyn_top toward the loop with unwind(event), delivers
   the event to the loop, and resumes at break_pc (BREAK) or
   continue_pc (CONTINUE).

   :param ctx: execution context
   :param event: KOS_XFER_EVENT_BREAK or KOS_XFER_EVENT_CONTINUE
   :param result: loop result value (transferred; caller loses ownership)

   :return: 0 on success, -1 on error (break/continue outside loop,
     same-frame check failed), KOS_DYN_UNWIND_ASYNC_STARTED when async
     unwind started
*/
int kos_dyn_transfer_to_loop(KosEvalState *es, KosXferEvent event,
                             PyObject *result);


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


/**
   Find the nearest prompt with the given id.

   Walks the dynamic chain from dyn_top toward root, looking for
   a PROMPT node with matching prompt_id.

   :param es: eval state
   :param prompt_id: tag to match (0 = default)

   :return: matching prompt, or NULL if not found
*/
KosDynPrompt *kos_find_prompt(KosEvalState *es, uint32_t prompt_id);


/**
   Find the nearest prompt from dyn_top toward the root (first PROMPT on
   the chain). Unlike kos_find_prompt, does not filter by prompt_id.

   :param es: eval state

   :return: innermost prompt, or NULL if none
*/
KosDynPrompt *kos_find_nearest_prompt(KosEvalState *es);


/* === Context exception stash === */


/**
   Park an exception triple on the context's LIFO exception stash, tagged
   with the parking node's id. Normalizes and attaches the traceback to
   the instance; consumes the triple.

   :return: 0 on success, -1 on error
*/
int kos_ctx_exc_stash_push(
   KosEvalState *es,
   uint32_t node_id,
   PyObject *typ,
   PyObject *val,
   PyObject *tb);


/**
   True when the stash top entry was parked by node_id.
*/
int kos_ctx_exc_stash_top_is(KosEvalState *es, uint32_t node_id);


/**
   Pop the top parked exception and restore it into PyErr to continue
   propagation.

   :return: 0 on success, -1 when the stash is empty
*/
int kos_ctx_exc_stash_reraise(KosEvalState *es, uint32_t node_id);


/**
   Pop the top parked exception and chain it as __context__ of the
   superseding triple (caller-owned, normalized in place). No-op on an
   empty stash.

   :return: 0 on success
*/
int kos_ctx_exc_stash_chain_top(
   KosEvalState *es,
   uint32_t node_id,
   PyObject **typ,
   PyObject **val,
   PyObject **tb);


/**
   Find the topmost CC (call/cc) block on the dynamic chain.

   :param es: eval state

   :return: topmost KosDynCC, or NULL if none
*/
KosDynCC *kos_find_cc(KosEvalState *es);


/**
   Validate that the prompt is reachable from dyn_top on the chain.

   :param es: eval state
   :param prompt: target prompt

   :return: 0 if capture allowed, -1 if not (sets exception)
*/
int kos_validate_capture(KosEvalState *es, KosDynPrompt *prompt);


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


/**
   Clone a continuation for multi-shot invocation.

   :param k: continuation to clone

   :return: cloned continuation, or NULL on error
*/
KosContinuation *kos_continuation_clone(KosContinuation *k);


/* === Transfer planning === */


/**
   Compute the lowest common ancestor between two chain positions.

   Tries pointer equality first; when chains are disjoint (e.g. template vs
   current), uses graft_point_id to find the graft point in chain a by node_id.

   :param a: first chain position (typically current)
   :param b: second chain position (typically target)
   :param graft_point_id: node_id of graft point for ID fallback; 0 = none

   :return: LCA node, or NULL if chains don't share an ancestor
*/
KosDynNode *kos_compute_lca(KosDynNode *a, KosDynNode *b, uint32_t graft_point_id);


/**
   Build a transfer plan from current position to target.

   The plan contains:
   1. RUN_HOOK actions for after hooks from current to LCA (inner-to-outer)
   2. INSTALL action to splice target chain
   3. RUN_HOOK actions for before hooks from LCA to target (outer-to-inner)
   4. INJECT action to push resume value

   :param es: execution/eval state (borrowed ctx)
   :param target: target chain position
   :param value: resume value (borrowed, may be NULL)
   :param is_throw: if nonzero, throw value as exception
   :param graft_point_id: node_id for LCA fallback when chains disjoint; 0 = none
   :param out_plan: output for transfer plan

   :return: 0 on success, -1 on error
*/
int kos_build_transfer_plan(
   KosEvalState *es,
   KosDynNode *target,
   PyObject *value,
   int is_throw,
   uint32_t graft_point_id,
   KosTransferPlan **out_plan);


/**
   Build a transfer plan with no unwind (INSTALL + wind + INJECT only).
   Use when the current and target chains are disjoint, such as after a
   nested transfer interrupted an outer plan's wind thunk.
   Caller sets RESUME_PC, RESUME_FRAME, RESUME_STACK after return.

   :param graft_point_id: node_id for LCA fallback when chains disjoint; 0 = none

   :return: 0 on success, -1 on error
*/
int kos_build_direct_install_plan(
   KosEvalState *es,
   KosDynNode *target,
   PyObject *value,
   int is_throw,
   uint32_t graft_point_id,
   KosTransferPlan **out_plan);


/**
   Build a PUSH plan for delimited continuation invoke.  Appends continuation
   frames (excluding prompt) to the current chain.  Plan has: EXTENT_ACTION
   (wind) for PARAM nodes in the segment if any, PUSH, INJECT.  Caller must
   set RESUME_PC, RESUME_FRAME, RESUME_STACK after return.
*/
int kos_build_delimited_push_plan(
   KosEvalState *es,
   KosDynNode *segment,
   PyObject *value,
   int is_throw,
   KosTransferPlan **out_plan);


/**
   Build a plan with only INSTALL and wind-before (no INJECT).
   Caller pushes frame and value after running the plan.

   :param graft_point_id: node_id for LCA fallback when chains disjoint; 0 = none
*/
int kos_build_install_only_plan(
   KosEvalState *es,
   KosDynNode *target,
   uint32_t graft_point_id,
   KosTransferPlan **out_plan);


/**
   Execute the next step of the active transfer plan.

   If the step is RUN_HOOK, begins hook execution and returns.
   If the step is INSTALL, splices the target chain.
   If the step is INJECT, pushes the resume value.
   :return: 1 if plan complete, 0 if more steps remain, -1 on error,
     KOS_XFER_SUSPENDED_AWAIT when async
*/
int kos_run_transfer_step(KosEvalState *es);


/**
   Build a plan for POP_SHIFT: unwind from current to prompt (exclusive),
   then TRUNCATE_AND_INJECT (free through prompt, push value).  Caller
   must set RESUME_PC on the plan for the first instruction after POP_PROMPT.

   :return: 0 on success, -1 on error
*/
int kos_dyn_build_pop_shift_plan(KosEvalState *es, KosDynPrompt *prompt,
                                 PyObject *value, KosTransferPlan **out_plan);


/**
   Free a transfer plan and its actions.

   :param plan: plan to free (may be NULL)
*/
void kos_free_transfer_plan(KosTransferPlan *plan);


/**
   Suspend current transfer plan for a nested transfer.  Called by
   kos_invoke_continuation when a continuation is invoked while another
   transfer is in progress.  Pushes active plan onto plan_stack, clears it.
*/
void kos_suspend_transfer_plan(KosEvalState *es);


/**
   Resume a suspended transfer plan after the nested transfer completes.
   Pops from plan_stack and restores it as the active plan (no-op when
   nothing is suspended).  Pairs with kos_suspend_transfer_plan.
*/
void kos_resume_transfer_plan(KosEvalState *es);


/**
   Free all suspended transfer plans on plan_stack.  Used when abandoning
   a nested transfer nest after a fatal error.
*/
void kos_free_transfer_plan_stack(KosEvalState *es);


/**
   Disarm the active transfer plan and free any suspended outers on
   plan_stack.  Does not clear PyErr or mutate dyn_top / frame_top / stack.
*/
void kos_abandon_transfer_nest(KosEvalState *es);


/**
   True when plan includes a KOS_XFER_PUSH step (delimited invoke plan).
*/
int kos_xfer_plan_is_delimited_push(const KosTransferPlan *plan);


/* === Continuation invocation === */


/**
   Invoke a captured continuation.

   Builds a transfer plan from current position to the continuation's
   captured chain and schedules execution.

   :param es: execution/eval state
   :param k: continuation to invoke
   :param value: resume value
   :param is_throw: if nonzero, throw value as exception

   :return: 0 on success, -1 on error
*/
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
   Traverse a KosTransferPlan for GC.

   :param plan: plan to traverse
   :param visit: visitproc callback
   :param arg: opaque arg for visit

   :return: 0 on success, non-zero if visit returns non-zero
*/
int kos_transferplan_traverse(
   KosTransferPlan *plan,
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
#define KosDynNode_IS_CC_RAISE(n) (KosDynNode_KIND(n) == KOS_DYN_CC_RAISE)
#define KosDynNode_IS_GEN(n)      (KosDynNode_KIND(n) == KOS_DYN_GEN)
#define KosDynNode_IS_NEXT(n)     (KosDynNode_KIND(n) == KOS_DYN_NEXT)

#define KosDynGen_OWNER(n)        (((KosDynNode *)(n))->u.gen.owner)
#define KosDynGen_ACTIVE(n)       (((KosDynNode *)(n))->u.gen.active)
#define KosDynGen_REFCOUNT(n)     (((KosDynNode *)(n))->u.gen.refcount)

#define KosDynFrame_CODE(f)       (((KosDynNode *)(f))->u.frame.code)
#define KosDynFrame_PC(f)         (((KosDynNode *)(f))->u.frame.pc)
#define KosDynFrame_ACTING_PC(f)  (((KosDynNode *)(f))->u.frame.acting_pc)
#define KosDynNode_STACK(n)       (((KosDynNode *)(n))->stack)
#define KosDynNode_NS(n)          (((KosDynNode *)(n))->ns)
#define KosDynFrame_STACK(f)      KosDynNode_STACK((KosDynNode *)(f))
#define KosDynFrame_NS(f)         KosDynNode_NS((KosDynNode *)(f))

#define KosDynFrame_FLAGS(f)      (((KosDynNode *)(f))->u.frame.frame_flags)
#define KOS_FRAME_DEMAND          (1u << 0)  /* frame is a demand-sync root */
#define KosDynFrame_NATIVE_HANDLER(f) (((KosDynNode *)(f))->u.frame.native_handler)

#define KosDynPrompt_ID(p)               (((KosDynNode *)(p))->u.prompt.prompt_id)
#define KosDynPrompt_PUSH_PC(p)          (((KosDynNode *)(p))->u.prompt.push_pc)
#define KosDynPrompt_PENDING_SHIFT_WRAPPER(p) (((KosDynNode *)(p))->u.prompt.pending_shift_wrapper)

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
#define KosDynWind_BODY_RESULT(w)  (((KosDynNode *)(w))->u.wind.body_result)
#define KosDynWind_ENTERED(w)      (((KosDynNode *)(w))->u.wind.entered)
#define KosDynWind_PHASE(w)        (((KosDynNode *)(w))->u.wind.phase)

#define KosDynTry_HANDLER_PC(r)      (((KosDynNode *)(r))->u.try_.handler_pc)
#define KosDynTry_ENABLED(r)         (((KosDynNode *)(r))->u.try_.enabled)
#define KosDynFinally_HANDLER_PC(r)  (((KosDynNode *)(r))->u.finally_.handler_pc)
#define KosDynFinally_ENABLED(r)     (((KosDynNode *)(r))->u.finally_.enabled)
#define KosDynFinally_HANDLING_RAISE(r) (((KosDynNode *)(r))->u.finally_.handling_raise)
#define KosDynFinally_HANDLING_RETURN(r) (((KosDynNode *)(r))->u.finally_.handling_return)
#define KosDynFinally_HANDLING_RESULT(r) (((KosDynNode *)(r))->u.finally_.handling_result)
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
#define KosDynForeach_ITER_STEP_PHASE(r) (((KosDynNode *)(r))->u.foreach.iter_step_phase)

/**
   Set a FOREACH node's ITER_NEXT step phase, keeping the
   KOS_DYN_CAP_RECEIVES_ITER_BOUNDARY capability in lockstep: the node is
   a receiving iterator boundary exactly while a step is in flight
   (flattened Koskari agen or suspended python async iterator).  All
   phase writes must go through this so the capability stays the single
   routing key for boundary dispatch.
*/
static inline void kos_dyn_foreach_set_iter_step_phase(
    KosDynForeach *fe, uint8_t phase) {

  KosDynForeach_ITER_STEP_PHASE(fe) = phase;
  if (phase != KOS_FOREACH_ITER_STEP_IDLE)
    KosDynNode_CAPS((KosDynNode *) fe) |= KOS_DYN_CAP_RECEIVES_ITER_BOUNDARY;
  else
    KosDynNode_CAPS((KosDynNode *) fe) &= ~KOS_DYN_CAP_RECEIVES_ITER_BOUNDARY;
}

#define KosDynNext_AGEN_OBJ(nd) (((KosDynNode *)(nd))->u.next.agen_obj)
#define KosDynNext_DEFAULT_VAL(nd) (((KosDynNode *)(nd))->u.next.default_val)
#define KosDynNext_N(nd) (((KosDynNode *)(nd))->u.next.n)

/* KosYfPhase: state machine for stack-based resume inject consumption */
typedef enum KosYfPhase {
  KOS_YF_PHASE_DRIVING = 0,
  KOS_YF_PHASE_EXPECT_SEND,
  KOS_YF_PHASE_EXPECT_AWAIT,
  /* A flattened koskari-gen delegate handed us a value at the iterator
     boundary; the next handler dispatch re-emits it as this generator's own
     yield (see kos_dyn_yield_from_deliver_inner_yield). */
  KOS_YF_PHASE_EMIT,
} KosYfPhase;


/**
   Yield-from delegate classification. Values stored in KosDynYieldFrom kind.
   Must stay aligned with yield_from_normalize in opcode/YIELD_FROM.c
*/
enum {
  KOS_YF_SYNC_ITER = 0,
  KOS_YF_SYNC_GEN = 1,
  KOS_YF_PY_ASYNC_ITER = 2,
  KOS_YF_PY_ASYNC_GEN = 3,
  KOS_YF_KOS_ASYNC_GEN = 4,
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
#define KosDynYieldFrom_PHASE(n)        (((KosDynNode *)(n))->u.yield_from.phase)
#define KosDynYieldFrom_PHASE_SET(n, v) (((KosDynNode *)(n))->u.yield_from.phase = (v))
#define KosDynYieldFrom_PENDING_ACTION(n) (((KosDynNode *)(n))->u.yield_from.pending_action)
#define KosDynYieldFrom_PENDING_ARG(n) (((KosDynNode *)(n))->u.yield_from.pending_arg)
#define KosDynYieldFrom_PENDING_ARG_TYPE(n) (((KosDynNode *)(n))->u.yield_from.pending_arg_type)
#define KosDynYieldFrom_INFLIGHT(n)    (((KosDynNode *)(n))->u.yield_from.inflight)

#define KosDynParam_BINDINGS(n)    (((KosDynNode *)(n))->u.param.bindings)
#define KosDynParam_SAVED(n)       (((KosDynNode *)(n))->u.param.saved)

#define KosDynCCRaise_EXC_TYPE(n)  (((KosDynNode *)(n))->u.cc_raise.exc_type)
#define KosDynCCRaise_EXC_VAL(n)   (((KosDynNode *)(n))->u.cc_raise.exc_val)

#define KOS_WITH_PHASE_IDLE                0
#define KOS_WITH_PHASE_PENDING_EXIT_AWAIT  1
#define KOS_WITH_PHASE_PENDING_ENTER_AWAIT 2

/** Multi-shot (dyn-with); participates in install wind */
#define KOS_DYN_WITH_SHOT_MULTI 0u
/** Single-shot; __exit__ not yet consumed for extent pairing */
#define KOS_DYN_WITH_SHOT_SINGLE_ACTIVE 1u
/** Single-shot; __exit__ already consumed; ignore further protocol */
#define KOS_DYN_WITH_SHOT_SINGLE_DONE 2u

#define KosDynWith_MGR(n)          (((KosDynNode *)(n))->u.with.mgr)
#define KosDynWith_ENTER(n)        (((KosDynNode *)(n))->u.with.enter)
#define KosDynWith_EXIT(n)         (((KosDynNode *)(n))->u.with.exit)
#define KosDynWith_ENTERED(n)      (((KosDynNode *)(n))->u.with.entered)
#define KosDynWith_PHASE(n)        (((KosDynNode *)(n))->u.with.phase)
#define KosDynWith_SHOT(n)         (((KosDynNode *)(n))->u.with.shot)
#define KosDynWith_EXIT_DONE(n)    (((KosDynNode *)(n))->u.with.exit_done)
#define KosDynWith_NAME(n)         (((KosDynNode *)(n))->u.with.name)
#define KosDynWith_RESUME_PC(n)    (((KosDynNode *)(n))->u.with.resume_pc)
#define KosDynWith_EXC_TYP(n)      (((KosDynNode *)(n))->u.with.exc_typ)
#define KosDynWith_EXC_VAL(n)      (((KosDynNode *)(n))->u.with.exc_val)
#define KosDynWith_EXC_TB(n)       (((KosDynNode *)(n))->u.with.exc_tb)
#define KosDynWith_NORMAL_EXIT(n)  (((KosDynNode *)(n))->u.with.normal_exit)


/* === Unit lifecycle === */


int koskari_dynchain_init(PyObject *mod);
int koskari_dynchain_clear(PyObject *mod);


#endif


// The end.
