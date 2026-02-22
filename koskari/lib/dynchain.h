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
   :ai-assistant: claude-4.6-sonnet-high via Cursor
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
   KOS_DYN_WIND   - Dynamic-wind extent with before/after hooks
   KOS_DYN_REGION - Block-local control (try/finally/loop/with)
   KOS_DYN_PYCALL - Python/C boundary marker (non-resumable)
   KOS_DYN_NS     - Namespace scope (PUSH_NS/POP_NS)
   KOS_DYN_CC     - Undelimited continuation block (call/cc)
   KOS_DYN_YIELD_FROM - Yield-from delegation state
   KOS_DYN_PARAM     - Parameter bindings (parameterize extent)
   KOS_DYN_WITH      - Single-shot context manager (with)
   KOS_DYN_DYN_WITH  - Multi-shot context manager (dyn-with)
   KOS_DYN_SHIFT     - Shift handler block (push stack block; no frame/namespace)
   KOS_DYN_CC_RAISE  - Sentinel node holding a KoskariContinuationApply exception
                       for the foreign-token transfer path; its unwind re-raises the
                       stored exception so the eval loop exits with KOS_EV_RAISE
*/

typedef enum KosDynKind {
  KOS_DYN_FRAME  = 0,
  KOS_DYN_PROMPT = 1,
  KOS_DYN_WIND   = 2,
  KOS_DYN_REGION = 3,
  KOS_DYN_PYCALL = 4,
  KOS_DYN_NS     = 5,
  KOS_DYN_CC     = 6,
  KOS_DYN_YIELD_FROM = 7,
  KOS_DYN_PARAM  = 8,
  KOS_DYN_WITH   = 9,
  KOS_DYN_DYN_WITH   = 10,
  KOS_DYN_SHIFT  = 11,
  KOS_DYN_CC_RAISE   = 12,
} KosDynKind;


/* === DynNode flags === */


typedef enum KosDynFlags {
  KOS_DYNF_NONE     = 0x00,
  KOS_DYNF_CLONED   = 0x01,  /* node was cloned for continuation capture */
  KOS_DYNF_BOUNDARY = 0x02,  /* node marks a non-resumable boundary */
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
#define KOS_DYN_CAP_BLOCKS_CAPTURE           (1u << 5)
#define KOS_DYN_CAP_NEEDS_TRANSFER_TEARDOWN  (1u << 6)
#define KOS_DYN_CAP_BARRIER                  (1u << 7)
#define KOS_DYN_CAP_HANDLES_EXCEPTIONS       (1u << 8)

/* Alias for KOS_DYN_CAP_MAY_SWALLOW_EXCEPTION (unified API naming) */
#define KOS_DYN_CAP_CATCH KOS_DYN_CAP_MAY_SWALLOW_EXCEPTION


/* === KosDynTypeOps: per-type handler table for unified API === */


/**
   KosDynTypeOps provides type-specific handlers for install, pop,
   wind, unwind, teardown, unwind_on_exception, and clone. Generic code
   dispatches via kos_dyn_type_ops[kind]. NULL slots mean no-op or not
   applicable.

   unwind_on_exception is invoked during exception unwinding when the
   normal pop handler declines (e.g., a BARRIER node). The handler
   should perform any necessary cleanup, unlink the node from the chain,
   and return: 0 to continue unwinding, 1 if the exception was swallowed
   and execution should resume, KOS_DYN_UNWIND_ASYNC_STARTED if an async
   operation was started, or -1 on error.
*/
typedef struct KosDynTypeOps {
  int (*install)(KosContext *ctx, KosDynNode *node);
  int (*pop)(KosContext *ctx, KosDynNode *node, struct KosEvent *out_ev);
  int (*wind)(KosContext *ctx, KosDynNode *node);
  int (*unwind)(KosContext *ctx, KosDynNode *node, PyObject **out_awaitable);
  int (*teardown_after_unwind)(KosContext *ctx, KosDynNode *node);
  int (*unwind_on_exception)(KosContext *ctx, KosDynNode *node, struct KosEvent *out_ev);
  KosDynNode *(*clone)(KosDynNode *node);
  uint32_t caps;
} KosDynTypeOps;


extern const KosDynTypeOps kos_dyn_type_ops[];


/* === KosRegionStyle enum === */


/**
   KosRegionStyle identifies the type of block-local control region.
   This replaces the old KosBlockStyle enum that was on KosStackBlock.
*/

typedef enum KosRegionStyle {
  KOS_REGION_TRY      = 0,
  KOS_REGION_FINALLY  = 1,
  KOS_REGION_LOOP     = 2,
  KOS_REGION_WITH     = 3,
  KOS_REGION_FOREACH  = 4,
  KOS_REGION_AWAIT_WITH     = 5,
  KOS_REGION_AWAIT_FOREACH  = 6,
  KOS_REGION_NS       = 7,
  KOS_REGION_BEGIN    = 8,
} KosRegionStyle;


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

  union {
    struct {
      KosCode *code;
      uint32_t pc;        /* next instruction */
      uint32_t acting_pc; /* instruction currently being executed */
      KosStackBlock *stack;
      PyObject *ns;
      uint32_t frame_flags;
    } frame;
    struct {
      uint32_t prompt_id;
      KosStackBlock *stack;  /* prompt-owned operand stack; NULL if not used */
    } prompt;
    struct {
      uint32_t resume_pc;
    } cc;
    struct {
      KosCode *before_code;
      uint32_t before_pc;
      KosCode *after_code;
      uint32_t after_pc;
      uint8_t entered;
    } wind;
    struct {
      KosRegionStyle style;
      uint32_t break_pc;
      uint32_t continue_pc;
      uint8_t enabled;
      PyObject *result;
      PyObject *iter;
      uint8_t exhausted;
      uint8_t handling_raise;  /* FINALLY only: 1 when entered due to exception */
      uint32_t iter_next_else_pc;  /* FOREACH only: else PC when ITER_NEXT gen exhausts; 0 = not set */
    } region;
    struct {
      PyObject *saved_ns;
    } ns;
    struct {
      /* pycall: no extra fields */
    } pycall;
    struct {
      PyObject *delegate;
      uint8_t kind;
      uint8_t pending_action;
      PyObject *pending_arg;
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
      PyObject *name;      /* binding name or NULL (owned) */
      uint32_t resume_pc;  /* PC to jump to after with body (pop_pc + 1) */
    } with;
    struct {
      /* shift: no extra fields; install pushes stack block, teardown drops it */
    } shift;
    struct {
      PyObject *exc_val;  /* borrowed-then-owned KoskariContinuationApply instance */
    } cc_raise;
  } u;
};

typedef struct KosDynNode KosDynFrame;
typedef struct KosDynNode KosDynPrompt;
typedef struct KosDynNode KosDynWind;
typedef struct KosDynNode KosDynRegion;
typedef struct KosDynNode KosDynNs;
typedef struct KosDynNode KosDynCC;
typedef struct KosDynNode KosDynYieldFrom;
typedef struct KosDynNode KosDynParam;
typedef struct KosDynNode KosDynWith;
typedef struct KosDynNode KosDynShift;
typedef struct KosDynNode KosDynCCRaise;


/* === KosContinuation: captured delimited continuation === */


/**
   KosContinuation holds a captured delimited continuation.

   The captured_chain is a cloned segment of the dynamic chain
   from the capture point up to (but not including) the prompt.

   The resume_frame points to the frame where execution resumes.
   The resume_pc is the program counter to resume at.
   The resume_stack holds the stack state at capture.

   For multi-shot continuations, the chain/stack are cloned on
   each invocation. For one-shot, they are consumed directly.
*/

struct KosContinuation {
  KosDynNode *captured_chain;   /* cloned chain segment */
  KosDynFrame *resume_frame;    /* frame to resume into */
  uint32_t resume_pc;           /* pc to resume at */
  KosStackBlock *resume_stack;  /* stack state (COW reference) */
  uint32_t prompt_id;           /* target prompt tag */
  uint32_t graft_point_id;      /* node_id of graft point for ID fallback; 0 = none */
  uint8_t one_shot;             /* 0 = multi-shot, 1 = one-shot */
  PyObject *token_ref;          /* KosEvaluatorToken (ownership marker) */
};


#define KosContinuation_CAPTURED_CHAIN(k)  (((KosContinuation *)(k))->captured_chain)
#define KosContinuation_RESUME_FRAME(k)    (((KosContinuation *)(k))->resume_frame)
#define KosContinuation_RESUME_PC(k)       (((KosContinuation *)(k))->resume_pc)
#define KosContinuation_RESUME_STACK(k)    (((KosContinuation *)(k))->resume_stack)
#define KosContinuation_PROMPT_ID(k)       (((KosContinuation *)(k))->prompt_id)
#define KosContinuation_GRAPT_POINT_ID(k)  (((KosContinuation *)(k))->graft_point_id)
#define KosContinuation_ONE_SHOT(k)        (((KosContinuation *)(k))->one_shot)
#define KosContinuation_TOKEN_REF(k)       (((KosContinuation *)(k))->token_ref)


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
   Step set py_await_iter and plan_suspended_await; driver resumes then
   feeds inject and runs plan again.
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
      int is_throw;
    } inject;

    struct {
      KosDynNode *node;
      PyObject *value;
      int is_throw;
    } truncate_and_inject;
  } data;

} KosTransferAction;


#define KosTransferAction_STEP(a)              (((KosTransferAction *)(a))->step)
#define KosTransferAction_EXTENT_NODE(a)       (((KosTransferAction *)(a))->data.extent_action.node)
#define KosTransferAction_EXTENT_PHASE(a)      (((KosTransferAction *)(a))->data.extent_action.phase)
#define KosTransferAction_INSTALL_SEGMENT(a)   (((KosTransferAction *)(a))->data.install.segment)
#define KosTransferAction_INJECT_VALUE(a)      (((KosTransferAction *)(a))->data.inject.value)
#define KosTransferAction_INJECT_IS_THROW(a)   (((KosTransferAction *)(a))->data.inject.is_throw)
#define KosTransferAction_TRUNCATE_NODE(a)     (((KosTransferAction *)(a))->data.truncate_and_inject.node)
#define KosTransferAction_TRUNCATE_VALUE(a)    (((KosTransferAction *)(a))->data.truncate_and_inject.value)
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

   The original_target and chain_version are used to detect if
   the dynamic chain has changed during nested transfers. If so,
   the remaining actions must be recomputed.
*/

struct KosTransferPlan {
  KosTransferPlan *suspended;   /* outer plan if nested */

  size_t action_count;          /* total actions */
  size_t action_index;          /* next action index */
  size_t action_capacity;       /* slots allocated (>= action_count) */

  KosDynNode *original_target;  /* target for chain-change detection */
  uint32_t chain_version;       /* version at plan creation */
  uint32_t resume_pc;           /* UINT32_MAX = not set; set for invocation */
  KosDynFrame *resume_frame;    /* frame to resume into (invocation only) */
  KosStackBlock *resume_stack;  /* stack state at capture (invocation only) */
  PyObject *continuation_ref;   /* keep continuation alive until plan is freed */
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
#define KosTransferPlan_CHAIN_VERSION(p)   (((KosTransferPlan *)(p))->chain_version)
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
   Signature for running one hook (or one chunk). Run the hook in ctx; fill
   out_ev on suspend. Return KOS_HOOK_DONE when the hook finished, or
   KOS_HOOK_IN_PROGRESS when it suspended on await (caller will resume later
   with send_val/throw_exc). hook_state is opaque state for the runner.
*/
typedef int (*KosHookInvokeFn)(struct KosContext *ctx, struct KosEvent *out_ev, void *hook_state);


/* === KosHookRunner: hook execution state === */


/**
   KosHookRunner holds the state for executing a hook block.

   Hooks are bytecode blocks that run with an isolated scratch
   stack. They terminate when the END_WIND opcode is reached.

   The active flag indicates whether hook execution is in progress.
   When active, the VM dispatches from the hook code/pc instead of
   the normal frame. pc = next instruction; acting_pc = instruction
   currently being executed.
*/

struct KosHookRunner {
  KosCode *code;          /* hook bytecode (borrowed) */
  uint32_t pc;            /* next instruction */
  uint32_t acting_pc;     /* instruction currently being executed */
  KosStackBlock *scratch; /* scratch operand stack (owned) */
  int active;             /* nonzero if hook is running */
};


#define KosHookRunner_CODE(r)       (((KosHookRunner *)(r))->code)
#define KosHookRunner_PC(r)         (((KosHookRunner *)(r))->pc)
#define KosHookRunner_ACTING_PC(r)  (((KosHookRunner *)(r))->acting_pc)
#define KosHookRunner_SCRATCH(r) (((KosHookRunner *)(r))->scratch)
#define KosHookRunner_ACTIVE(r)  (((KosHookRunner *)(r))->active)


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

   Clones the frame's stack using COW semantics. Does NOT clone
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
   Allocate a new wind node.

   :param parent: parent node in dynamic chain
   :param before_code: before hook code (borrowed, or NULL)
   :param before_pc: before hook entry point
   :param after_code: after hook code (borrowed, or NULL)
   :param after_pc: after hook entry point

   :return: new wind node, or NULL on error
*/
KosDynWind *kos_dynwind_new(
   KosDynNode *parent,
   KosCode *before_code,
   uint32_t before_pc,
   KosCode *after_code,
   uint32_t after_pc);


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
KosDynRegion *kos_dynregion_new(
   KosDynNode *parent,
   KosRegionStyle style,
   uint32_t break_pc,
   uint32_t continue_pc);


/**
   Clone a region node for continuation capture.

   :param region: region to clone

   :return: cloned region, or NULL on error
*/
KosDynRegion *kos_dynregion_clone(KosDynRegion *region);


/**
   Free a region node.

   :param region: region to free (may be NULL)
*/
void kos_dynregion_free(KosDynRegion *region);


/**
   Allocate a Python call boundary marker.

   :param parent: parent node in dynamic chain

   :return: new boundary node, or NULL on error
*/
KosDynNode *kos_dynpycall_new(KosDynNode *parent);


/**
   Free a boundary marker node.

   :param node: node to free (may be NULL)
*/
void kos_dynpycall_free(KosDynNode *node);


/**
   Free a single DynNode of any kind.

   Dispatches to the appropriate type-specific free function.
   Does NOT free the parent chain.

   :param node: node to free (may be NULL)
*/
void kos_dynnode_free(KosDynNode *node);


/**
   Clone a single DynNode of any kind.

   Dispatches to the appropriate type-specific clone function.
   Does NOT clone the parent chain.

   :param node: node to clone

   :return: cloned node, or NULL on error
*/
KosDynNode *kos_dynnode_clone(KosDynNode *node);


/**
   Free an entire chain of DynNodes from node down to (but not
   including) stop.

   :param node: top of chain to free
   :param stop: stop at this node (not freed), or NULL to free all
*/
void kos_dynchain_free(KosDynNode *node, KosDynNode *stop);


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
   the type has one.  Updates dyn_top and chain_version.

   :param ctx: execution context
   :param node: node to push (parent already set)

   :return: 0 on success, -1 on error
*/
int kos_dyn_push(KosContext *ctx, KosDynNode *node);


/**
   Pop a node from the context's dynamic chain.  Node must be dyn_top.
   For BARRIER nodes (PROMPT, PYCALL) returns -1.  Otherwise runs the
   type's pop handler (if any) then unlinks and frees.  Returns
   KOS_DYN_UNWIND_ASYNC_STARTED when async extent action started.

   :param ctx: execution context
   :param node: node to pop (must be dyn_top)
   :param out_ev: event output for async suspend

   :return: 0 on success, -1 on error or BARRIER, KOS_DYN_UNWIND_ASYNC_STARTED
     when async
*/
int kos_dyn_pop(KosContext *ctx, KosDynNode *node, struct KosEvent *out_ev);


/**
   Clone a node and push onto the chain.  Fails for types that cannot
   clone (e.g. returns error for types that must not cross capture).

   :param ctx: execution context
   :param node: node to clone

   :return: 0 on success, -1 on error
*/
int kos_dyn_push_clone(KosContext *ctx, KosDynNode *node);


/* === Dynamic chain context operations === */


/**
   Push a new frame onto the context's dynamic chain.

   Creates a new frame and sets it as dyn_top. Updates frame_top cache.
   Increments chain_version.

   :param ctx: execution context
   :param code: bytecode object (borrowed)
   :param ns: namespace (reference is stolen)

   :return: new frame, or NULL on error
*/
KosDynFrame *kos_dyn_push_frame(KosContext *ctx, KosCode *code, PyObject *ns);


/**
   Pop the top frame from the context's dynamic chain.

   Frees the frame and updates dyn_top to the frame's parent.
   Updates frame_top cache to find the new topmost frame.
   Increments chain_version.

   :param ctx: execution context

   :return: 0 on success, -1 on error
*/
int kos_dyn_pop_frame(KosContext *ctx);


/**
   Attach an existing frame to the context's dynamic chain (no allocation).

   :param ctx: execution context
   :param frame: existing frame (not freed by this module)

   :return: 0 on success, -1 on error
*/
int kos_dyn_attach_frame(KosContext *ctx, KosDynFrame *frame);


/**
   Detach the top frame without freeing it. Clears the detached frame's
   parent link so it is fully disconnected from the chain. Caller receives
   the frame.

   :param ctx: execution context
   :param frame_out: output; set to the detached frame (may be NULL)

   :return: 0 on success, -1 if top is not a frame
*/
int kos_dyn_detach_top_frame(KosContext *ctx, KosDynFrame **frame_out);


/**
   Push a prompt delimiter onto the dynamic chain.

   :param ctx: execution context
   :param prompt_id: tag for matching (0 = default)

   :return: new prompt, or NULL on error
*/
KosDynPrompt *kos_dyn_push_prompt(KosContext *ctx, uint32_t prompt_id);


/**
   Pop a prompt from the dynamic chain.

   Frees the prompt and updates dyn_top.
   Increments chain_version.

   :param ctx: execution context
   :param prompt: prompt to pop (must be dyn_top)
*/
void kos_dyn_pop_prompt(KosContext *ctx, KosDynPrompt *prompt);


/**
   Push a CC (call/cc) block onto the dynamic chain.

   :param ctx: execution context
   :param resume_pc: instruction index to resume at when continuation is invoked

   :return: the new CC node, or NULL on error
*/
KosDynCC *kos_dyn_push_cc(KosContext *ctx, uint32_t resume_pc);


/**
   Pop a CC block from the dynamic chain.

   :param ctx: execution context
   :param cc: the CC node to pop (must be at dyn_top)
*/
void kos_dyn_pop_cc(KosContext *ctx, KosDynCC *cc);


/**
   Push a SHIFT block onto the dynamic chain.  A shift node pushes a stack
   block for its body (no frame, no namespace).  Used by PUSH_SHIFT after
   unwinding to the prompt.

   :param ctx: execution context

   :return: 0 on success, -1 on error
*/
int kos_dyn_push_shift(KosContext *ctx);


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
    KosContext *ctx,
    PyObject *exc_val,
    KosTransferPlan **out_plan);


/**
   Push a wind extent onto the dynamic chain.

   :param ctx: execution context
   :param before_code: before hook code (borrowed, or NULL)
   :param before_pc: before hook entry point
   :param after_code: after hook code (borrowed, or NULL)
   :param after_pc: after hook entry point

   :return: new wind node, or NULL on error
*/
KosDynWind *kos_dyn_push_wind(
   KosContext *ctx,
   KosCode *before_code,
   uint32_t before_pc,
   KosCode *after_code,
   uint32_t after_pc);


/**
   Pop a wind extent from the dynamic chain.

   This does NOT automatically run the after hook; the caller must
   handle that via the transfer plan or explicit hook execution.

   :param ctx: execution context
   :param wind: wind to pop (must be dyn_top)

   :return: 0 on success, -1 on error
*/
int kos_dyn_pop_wind(KosContext *ctx, KosDynWind *wind);


/**
   Push a region onto the dynamic chain.

   :param ctx: execution context
   :param style: region type
   :param break_pc: break/exit jump target
   :param continue_pc: continue jump target (or 0)

   :return: new region, or NULL on error
*/
KosDynRegion *kos_dyn_push_region(
   KosContext *ctx,
   KosRegionStyle style,
   uint32_t break_pc,
   uint32_t continue_pc);


/**
   Pop a region from the dynamic chain.

   :param ctx: execution context
   :param region: region to pop (must be dyn_top)
*/
void kos_dyn_pop_region(KosContext *ctx, KosDynRegion *region);


/**
   Push a namespace scope node (for PUSH_NS).  Does not modify frame ns;
   caller must set frame->ns to the new namespace and later restore from
   saved_ns on pop.

   :param ctx: execution context
   :param saved_ns: prior frame namespace to restore on POP_NS (new ref)

   :return: new KosDynNs node, or NULL on error
*/
KosDynNs *kos_dyn_push_ns(KosContext *ctx, PyObject *saved_ns);


/**
   Pop a namespace scope node.  Caller must restore frame->ns from the
   node's saved_ns before calling this.

   :param ctx: execution context
   :param node: NS node to pop (must be dyn_top)
*/
void kos_dyn_pop_ns(KosContext *ctx, KosDynNs *node);


/**
   Push a parameter extent onto the dynamic chain from a bindings dict.

   Creates a PARAM node, runs enter (save domain, set values), and pushes.
   Caller gives ownership of bindings (dict name -> value); node takes it.

   :param ctx: execution context
   :param bindings: PyDict mapping names to values (ownership transferred)

   :return: new KosDynParam node, or NULL on error
*/
KosDynParam *kos_dyn_push_param_dict(KosContext *ctx, PyObject *bindings);


/**
   Push a parameter extent onto the dynamic chain.

   Creates a PARAM node, runs enter (save domain, set values), and pushes.
   Caller passes arrays of params (ParamRef) and values; these are INCREF'd.

   :param ctx: execution context
   :param count: number of (param, value) pairs
   :param params: array of ParamRef objects (new ref for each)
   :param values: array of values to bind (new ref for each)

   :return: new KosDynParam node, or NULL on error
*/
KosDynParam *kos_dyn_push_param(
    KosContext *ctx,
    uint32_t count,
    PyObject **params,
    PyObject **values);


/**
   Pop a parameter extent from the dynamic chain.

   Restores domain, updates dyn_top, and frees the node.

   :param ctx: execution context
   :param node: PARAM node to pop (must be dyn_top)
*/
void kos_dyn_pop_param(KosContext *ctx, KosDynParam *node);


/**
   Push a single-shot with (context manager) extent onto the dynamic chain.

   Caller must have called enter(mgr) and have the result; the node stores
   mgr, enter, exit, and optional name. Node takes ownership of mgr, enter,
   exit, and name (name may be NULL).

   :param ctx: execution context
   :param mgr: context manager object (ownership transferred)
   :param enter: resolved enter callable (ownership transferred)
   :param exit: resolved exit callable (ownership transferred)
   :param name: binding name or NULL (ownership transferred)
   :param resume_pc: PC to resume at after with body (extent pop_pc + 1)

   :return: new KosDynWith node, or NULL on error
*/
KosDynWith *kos_dyn_push_with(
    KosContext *ctx,
    PyObject *mgr,
    PyObject *enter,
    PyObject *exit,
    PyObject *name,
    uint32_t resume_pc);


/**
   Push a dyn-with (multi-shot context manager) extent onto the dynamic chain.
   Same as kos_dyn_push_with but installs KOS_DYN_DYN_WITH for transfer-plan
   participation.

   :param ctx: execution context
   :param mgr: context manager object (ownership transferred)
   :param enter: resolved enter callable (ownership transferred)
   :param exit: resolved exit callable (ownership transferred)
   :param name: binding name or NULL (ownership transferred)
   :param resume_pc: PC to resume at after dyn-with body (extent pop_pc + 1)

   :return: new KosDynWith node (kind KOS_DYN_DYN_WITH), or NULL on error
*/
KosDynWith *kos_dyn_push_dyn_with(
    KosContext *ctx,
    PyObject *mgr,
    PyObject *enter,
    PyObject *exit,
    PyObject *name,
    uint32_t resume_pc);


/**
   Clone a KOS_DYN_DYN_WITH node for continuation capture.
   Copies mgr, enter, exit, name with new references.

   :param src: DYN_WITH node to clone

   :return: cloned node, or NULL on error
*/
KosDynWith *kos_dynwith_clone(KosDynWith *src);
KosDynWith *kos_dyndynwith_clone(KosDynWith *src);


/**
   Pop a with extent from the dynamic chain.

   :param ctx: execution context
   :param node: WITH node to pop (must be dyn_top)
*/
void kos_dyn_pop_with(KosContext *ctx, KosDynWith *node);


/**
   Unwind helper for WITH: call exit(mgr, exc_type, exc_val, tb) with
   current exception info. Sync only; returns 0 when done or -1 on error.

   :param ctx: execution context
   :param node: WITH node being unwound

   :param out_awaitable: if non-NULL and exit returns an awaitable,
     set to the awaitable (new ref) and return KOS_DYN_UNWIND_ASYNC_STARTED

   :return: 0 on success, -1 on error, KOS_DYN_UNWIND_ASYNC_STARTED if
     exit returned awaitable (and *out_awaitable set)
*/
int kos_dyn_with_unwind(KosContext *ctx, KosDynWith *node,
                        PyObject **out_awaitable);


/**
   Pop the top node of the dynamic chain. Dispatches on node kind.
   For NS, restores frame_top->ns from the node's saved_ns before popping.

   :param ctx: execution context

   :param out_ev: event output for suspend (required when WITH may have async exit)

   :return: 0 on success, -1 on error or if top is PROMPT/PYCALL,
     KOS_DYN_UNWIND_ASYNC_STARTED when async extent action started (resume later)
*/
int kos_dyn_pop_top(KosContext *ctx, struct KosEvent *out_ev);


int kos_dyn_unwind_to_frame(KosContext *ctx, KosDynFrame *frame,
                           struct KosEvent *out_ev);


int kos_dyn_unwind_to_node(KosContext *ctx, KosDynNode *target,
                           struct KosEvent *out_ev);


/**
   Return value from extent wind/unwind helpers when they start async
   exit code (e.g. wind after hook, WITH __exit__ returning awaitable).
   Caller must not pop the node yet; the evaluator runs the hook or
   awaits the exit then resumes unwinding.
*/
#define KOS_DYN_UNWIND_ASYNC_STARTED 1


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
int kos_dyn_param_wind(KosContext *ctx, KosDynParam *node);
void kos_dyn_param_unwind(KosContext *ctx, KosDynParam *node);

int kos_dyn_wind_wind(KosContext *ctx, KosDynWind *node);
int kos_dyn_wind_unwind(KosContext *ctx, KosDynWind *node);


/**
   Unified extent wind (enter) and unwind (exit) API.  Dispatches by
   node kind to the appropriate helper.  Single call site for
   "run this node's wind/unwind".

   kos_extent_run_wind(ctx, node): enter the extent.  Return 0 on
   success, -1 on error, or KOS_DYN_UNWIND_ASYNC_STARTED if async.

   kos_extent_run_unwind(ctx, node, out_awaitable): exit the extent.
   Return 0 when complete, -1 on error, or KOS_DYN_UNWIND_ASYNC_STARTED
   if async exit started.  For WITH/DYN_WITH, when async, stores the
   awaitable in *out_awaitable (caller may pass NULL if not needed).
*/
int kos_extent_run_wind(KosContext *ctx, KosDynNode *node);
int kos_extent_run_unwind(KosContext *ctx, KosDynNode *node,
                         PyObject **out_awaitable);

void kos_dyn_ns_unwind(KosContext *ctx, KosDynNs *node);

void kos_dyn_region_unwind(KosContext *ctx, KosDynRegion *node);

void kos_dyn_yieldfrom_unwind(KosContext *ctx, KosDynYieldFrom *node);


/**
   Find the first TRY or FINALLY region on the dynamic chain that is
   enabled, walking from dyn_top toward root. Used for exception
   handling: when an exception occurs, this returns the region whose
   handler (a_label) should receive control.

   :param ctx: execution context

   :return: the topmost enabled TRY or FINALLY region, or NULL
*/
KosDynRegion *kos_find_region_for_raise(KosContext *ctx);


/**
   Find the innermost LOOP or FOREACH region on the dynamic chain,
   walking from dyn_top toward root. Used for break/continue.

   :param ctx: execution context

   :return: the topmost LOOP or FOREACH region, or NULL if none found
 */
KosDynRegion *kos_find_region_for_loop(KosContext *ctx);


/**
   Push a Python call boundary marker.

   This marks the point where Python code called into Koskari.
   Continuation capture will fail if it would cross this boundary.

   :param ctx: execution context

   :return: boundary node, or NULL on error
*/
KosDynNode *kos_dyn_push_pycall(KosContext *ctx);


/**
   Pop a Python call boundary marker.

   :param ctx: execution context
   :param marker: boundary node to pop (must be dyn_top)
*/
void kos_dyn_pop_pycall(KosContext *ctx, KosDynNode *marker);


/**
   Push a YIELD_FROM delegation node onto the context's dynamic chain.

   Does not update frame_top (the frame remains the topmost frame).
   Caller must have normalized the delegate and determined kind.

   :param ctx: execution context
   :param delegate: normalized delegate (new ref; node takes ownership)
   :param kind: delegate kind (KOS_YF_* from eval.c)

   :return: new node, or NULL on error
*/
KosDynYieldFrom *kos_dyn_push_yield_from(
    KosContext *ctx,
    PyObject *delegate,
    uint8_t kind);


/**
   Pop a YIELD_FROM node. Caller must have closed the delegate if needed.
   node must be dyn_top.

   :param ctx: execution context
   :param node: YIELD_FROM node to pop (must be current dyn_top)
*/
void kos_dyn_pop_yield_from(KosContext *ctx, KosDynYieldFrom *node);


/* === Prompt lookup and continuation capture === */


/**
   Find the nearest prompt with the given id.

   Walks the dynamic chain from dyn_top toward root, looking for
   a PROMPT node with matching prompt_id.

   :param ctx: execution context
   :param prompt_id: tag to match (0 = default)

   :return: matching prompt, or NULL if not found
*/
KosDynPrompt *kos_find_prompt(KosContext *ctx, uint32_t prompt_id);


/**
   Find the topmost CC (call/cc) block on the dynamic chain.

   :param ctx: execution context

   :return: topmost KosDynCC, or NULL if none
*/
KosDynCC *kos_find_cc(KosContext *ctx);


/**
   Validate that continuation capture is allowed up to the prompt.

   Returns error if any PYCALL boundary nodes exist between dyn_top
   and the prompt.

   :param ctx: execution context
   :param prompt: target prompt

   :return: 0 if capture allowed, -1 if not (sets exception)
*/
int kos_validate_capture(KosContext *ctx, KosDynPrompt *prompt);


/**
   Capture a delimited continuation.

   Finds the prompt, validates capture, clones the chain segment,
   and creates a KosContinuation.

   :param ctx: execution context
   :param prompt_id: target prompt tag
   :param out_k: output for captured continuation

   :return: 0 on success, -1 on error
*/
int kos_shift_capture(
   KosContext *ctx,
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

   :param ctx: execution context
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


/**
   Free a continuation; when releasing its resume_stack, do not DECREF
   any slot equal to exclude_from_stack (used from the Python wrapper's
   dealloc so the wrapper is not decref'd while being deallocated).
   exclude_from_stack may be NULL.
*/
void kos_continuation_free_exclude(KosContinuation *k, PyObject *exclude_from_stack);


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

   :param ctx: execution context
   :param target: target chain position
   :param value: resume value (borrowed, may be NULL)
   :param is_throw: if nonzero, throw value as exception
   :param graft_point_id: node_id for LCA fallback when chains disjoint; 0 = none
   :param out_plan: output for transfer plan

   :return: 0 on success, -1 on error
*/
int kos_build_transfer_plan(
   KosContext *ctx,
   KosDynNode *target,
   PyObject *value,
   int is_throw,
   uint32_t graft_point_id,
   KosTransferPlan **out_plan);


/**
   Build a transfer plan with no unwind actions (direct install).
   Use when the path crosses a barrier (e.g. PROMPT) that cannot be
   popped.  Caller sets RESUME_PC etc. after return.

   :param graft_point_id: node_id for LCA fallback when chains disjoint; 0 = none

   :return: 0 on success, -1 on error
*/
int kos_build_direct_install_plan(
   KosContext *ctx,
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
   KosContext *ctx,
   KosDynNode *segment,
   PyObject *value,
   int is_throw,
   KosTransferPlan **out_plan);


/**
   Build a minimal plan with only INSTALL and INJECT (no wind, no LCA).
   Use for delimited k(value) return path when invocation-site chain is
   disjoint from current.  Caller must set RESUME_PC.
*/
int kos_build_return_plan(
   KosContext *ctx,
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
   KosContext *ctx,
   KosDynNode *target,
   uint32_t graft_point_id,
   KosTransferPlan **out_plan);


/**
   Execute the next step of the active transfer plan.

   If the step is RUN_HOOK, begins hook execution and returns.
   If the step is INSTALL, splices the target chain.
   If the step is INJECT, pushes the resume value.
   POP_ONE and TRUNCATE_AND_INJECT use out_ev for async suspend.

   :param ctx: execution context
   :param out_ev: event output for async suspend (required for POP_ONE)

   :return: 1 if plan complete, 0 if more steps remain, -1 on error,
     KOS_XFER_SUSPENDED_AWAIT when async
*/
int kos_run_transfer_step(KosContext *ctx, struct KosEvent *out_ev);


/**
   Build a plan for POP_SHIFT: unwind from current to prompt (exclusive),
   then TRUNCATE_AND_INJECT (free through prompt, push value).  Caller
   must set RESUME_PC on the plan for the first instruction after POP_PROMPT.

   :return: 0 on success, -1 on error
*/
int kos_dyn_build_pop_shift_plan(KosContext *ctx, KosDynPrompt *prompt,
                                 PyObject *value, KosTransferPlan **out_plan);


/**
   Build a plan for PUSH_SHIFT unwind: POP_ONE for each node from current
   top down to (but not including) the prompt.  After completion,
   dyn_top == prompt.

   :return: 0 on success, -1 on error
*/
int kos_dyn_build_push_shift_unwind_plan(KosContext *ctx, KosDynPrompt *prompt,
                                         KosTransferPlan **out_plan);


/**
   Free a transfer plan and its actions.

   :param plan: plan to free (may be NULL)
*/
void kos_free_transfer_plan(KosTransferPlan *plan);


/**
   Suspend the current transfer plan for nested transfer.

   Pushes the active plan onto the suspended stack and clears it.

   :param ctx: execution context
*/
void kos_suspend_transfer_plan(KosContext *ctx);


/**
   Resume a suspended transfer plan.

   Pops from the suspended stack and checks for chain changes.
   If the chain has changed, replans the remaining actions.

   :param ctx: execution context

   :return: 0 on success, -1 on error
*/
int kos_resume_transfer_plan(KosContext *ctx);


/* === Hook execution === */


/**
   Begin executing a hook block.

   Sets up the hook runner with the given code/pc and allocates
   a scratch stack.

   :param ctx: execution context
   :param code: hook bytecode
   :param pc: hook entry point

   :return: 0 on success, -1 on error
*/
int kos_begin_hook(KosContext *ctx, KosCode *code, uint32_t pc);


/**
   Check if hook execution is active.

   :param ctx: execution context

   :return: nonzero if hook is running
*/
int kos_hook_active(KosContext *ctx);


/**
   End hook execution (called when END_WIND opcode is hit).

   Cleans up the hook runner and returns control to the transfer
   plan (if any).

   :param ctx: execution context

   :return: 0 on success, -1 on error
*/
int kos_end_hook(KosContext *ctx);


/* === Continuation invocation === */


/**
   Invoke a captured continuation.

   Builds a transfer plan from current position to the continuation's
   captured chain and schedules execution.

   :param ctx: execution context
   :param k: continuation to invoke
   :param value: resume value
   :param is_throw: if nonzero, throw value as exception

   :return: 0 on success, -1 on error
*/
int kos_invoke_continuation(
   KosContext *ctx,
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
   KosDynNode *stop,
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
   Traverse a KosHookRunner for GC.

   :param runner: hook runner to traverse
   :param visit: visitproc callback
   :param arg: opaque arg for visit

   :return: 0 on success, non-zero if visit returns non-zero
*/
int kos_hookrunner_traverse(
   KosHookRunner *runner,
   visitproc visit,
   void *arg);


/* === Utility macros === */


#define KosDynNode_KIND(n)    (((KosDynNode *)(n))->kind)
#define KosDynNode_PARENT(n)  (((KosDynNode *)(n))->parent)
#define KosDynNode_FLAGS(n)   (((KosDynNode *)(n))->flags)
#define KosDynNode_CAPS(n)    (((KosDynNode *)(n))->caps)
#define KosDynNode_NODE_ID(n) (((KosDynNode *)(n))->node_id)
#define KosDynNode_HAS_CAP(n, cap)  (KosDynNode_CAPS(n) & (cap))

#define KosDynNode_IS_FRAME(n)   (KosDynNode_KIND(n) == KOS_DYN_FRAME)
#define KosDynNode_IS_PROMPT(n)  (KosDynNode_KIND(n) == KOS_DYN_PROMPT)
#define KosDynNode_IS_WIND(n)    (KosDynNode_KIND(n) == KOS_DYN_WIND)
#define KosDynNode_IS_REGION(n)  (KosDynNode_KIND(n) == KOS_DYN_REGION)
#define KosDynNode_IS_NS(n)      (KosDynNode_KIND(n) == KOS_DYN_NS)
#define KosDynNode_IS_PYCALL(n)  (KosDynNode_KIND(n) == KOS_DYN_PYCALL)
#define KosDynNode_IS_CC(n)      (KosDynNode_KIND(n) == KOS_DYN_CC)
#define KosDynNode_IS_YIELD_FROM(n)  (KosDynNode_KIND(n) == KOS_DYN_YIELD_FROM)
#define KosDynNode_IS_PARAM(n)       (KosDynNode_KIND(n) == KOS_DYN_PARAM)
#define KosDynNode_IS_WITH(n)       (KosDynNode_KIND(n) == KOS_DYN_WITH)
#define KosDynNode_IS_DYN_WITH(n)   (KosDynNode_KIND(n) == KOS_DYN_DYN_WITH)
#define KosDynNode_IS_WITH_OR_DYN_WITH(n) \
  (KosDynNode_IS_WITH(n) || KosDynNode_IS_DYN_WITH(n))
#define KosDynNode_IS_SHIFT(n)    (KosDynNode_KIND(n) == KOS_DYN_SHIFT)
#define KosDynNode_IS_CC_RAISE(n) (KosDynNode_KIND(n) == KOS_DYN_CC_RAISE)

#define KosDynFrame_CODE(f)       (((KosDynNode *)(f))->u.frame.code)
#define KosDynFrame_PC(f)         (((KosDynNode *)(f))->u.frame.pc)
#define KosDynFrame_ACTING_PC(f)  (((KosDynNode *)(f))->u.frame.acting_pc)
#define KosDynFrame_STACK(f)      (((KosDynNode *)(f))->u.frame.stack)
#define KosDynFrame_NS(f)         (((KosDynNode *)(f))->u.frame.ns)
#define KosDynFrame_FLAGS(f)      (((KosDynNode *)(f))->u.frame.frame_flags)

#define KosDynPrompt_ID(p)        (((KosDynNode *)(p))->u.prompt.prompt_id)
#define KosDynPrompt_STACK(p)     (((KosDynNode *)(p))->u.prompt.stack)

#define KosDynCC_RESUME_PC(c)     (((KosDynNode *)(c))->u.cc.resume_pc)

#define KosDynWind_BEFORE_CODE(w) (((KosDynNode *)(w))->u.wind.before_code)
#define KosDynWind_BEFORE_PC(w)   (((KosDynNode *)(w))->u.wind.before_pc)
#define KosDynWind_AFTER_CODE(w)  (((KosDynNode *)(w))->u.wind.after_code)
#define KosDynWind_AFTER_PC(w)    (((KosDynNode *)(w))->u.wind.after_pc)
#define KosDynWind_ENTERED(w)     (((KosDynNode *)(w))->u.wind.entered)

#define KosDynRegion_STYLE(r)      (((KosDynNode *)(r))->u.region.style)
#define KosDynRegion_BREAK_PC(r)   (((KosDynNode *)(r))->u.region.break_pc)
#define KosDynRegion_CONTINUE_PC(r) (((KosDynNode *)(r))->u.region.continue_pc)
#define KosDynRegion_ENABLED(r)    (((KosDynNode *)(r))->u.region.enabled)
#define KosDynRegion_RESULT(r)     (((KosDynNode *)(r))->u.region.result)
#define KosDynRegion_ITER(r)       (((KosDynNode *)(r))->u.region.iter)
#define KosDynRegion_EXHAUSTED(r)  (((KosDynNode *)(r))->u.region.exhausted)
#define KosDynRegion_HANDLING_RAISE(r) (((KosDynNode *)(r))->u.region.handling_raise)
/* For FINALLY regions: result holds handling_result (stashed return value) */
#define KosDynRegion_HANDLING_RESULT(r) KosDynRegion_RESULT(r)
#define KosDynRegion_ITER_NEXT_ELSE_PC(r) (((KosDynNode *)(r))->u.region.iter_next_else_pc)

#define KosDynNs_SAVED_NS(n)      (((KosDynNode *)(n))->u.ns.saved_ns)

#define KosDynYieldFrom_DELEGATE(n)     (((KosDynNode *)(n))->u.yield_from.delegate)
#define KosDynYieldFrom_KIND(n)         (((KosDynNode *)(n))->u.yield_from.kind)
#define KosDynYieldFrom_PENDING_ACTION(n) (((KosDynNode *)(n))->u.yield_from.pending_action)
#define KosDynYieldFrom_PENDING_ARG(n) (((KosDynNode *)(n))->u.yield_from.pending_arg)
#define KosDynYieldFrom_INFLIGHT(n)    (((KosDynNode *)(n))->u.yield_from.inflight)

#define KosDynParam_BINDINGS(n)    (((KosDynNode *)(n))->u.param.bindings)
#define KosDynParam_SAVED(n)       (((KosDynNode *)(n))->u.param.saved)

#define KosDynCCRaise_EXC_VAL(n)   (((KosDynNode *)(n))->u.cc_raise.exc_val)

#define KosDynWith_MGR(n)          (((KosDynNode *)(n))->u.with.mgr)
#define KosDynWith_ENTER(n)        (((KosDynNode *)(n))->u.with.enter)
#define KosDynWith_EXIT(n)         (((KosDynNode *)(n))->u.with.exit)
#define KosDynWith_ENTERED(n)      (((KosDynNode *)(n))->u.with.entered)
#define KosDynWith_NAME(n)         (((KosDynNode *)(n))->u.with.name)
#define KosDynWith_RESUME_PC(n)    (((KosDynNode *)(n))->u.with.resume_pc)


/* === Unit lifecycle === */


int koskari_dynchain_init(PyObject *mod);
int koskari_dynchain_clear(PyObject *mod);


#endif


// The end.
