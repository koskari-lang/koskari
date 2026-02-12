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
   WIND, REGION) that form a unified parent chain, replacing the
   separate frame and handler chains.

   :author: Christopher O'Brien <obriencj@preoccupied.net>
   :license: GNU General Public License v3
   :ai-assistant: Claude Opus 4.5 via Cursor
*/


#ifndef KOSKARI_DYNCHAIN_H
#define KOSKARI_DYNCHAIN_H


#include <Python.h>
#include <stdint.h>


/* === Forward declarations === */

struct KosCode;
typedef struct KosCode KosCode;

struct KosStackBlock;
typedef struct KosStackBlock KosStackBlock;

struct KosContext;
typedef struct KosContext KosContext;

struct KosDynNode;
typedef struct KosDynNode KosDynNode;

struct KosContinuation;
typedef struct KosContinuation KosContinuation;

struct KosTransferPlan;
typedef struct KosTransferPlan KosTransferPlan;

struct KosHookRunner;
typedef struct KosHookRunner KosHookRunner;


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
*/

typedef enum KosDynKind {
  KOS_DYN_FRAME  = 0,
  KOS_DYN_PROMPT = 1,
  KOS_DYN_WIND   = 2,
  KOS_DYN_REGION = 3,
  KOS_DYN_PYCALL = 4,
  KOS_DYN_NS     = 5,
  KOS_DYN_CC     = 6,
} KosDynKind;


/* === DynNode flags === */


typedef enum KosDynFlags {
  KOS_DYNF_NONE     = 0x00,
  KOS_DYNF_CLONED   = 0x01,  /* node was cloned for continuation capture */
  KOS_DYNF_BOUNDARY = 0x02,  /* node marks a non-resumable boundary */
} KosDynFlags;


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

  union {
    struct {
      KosCode *code;
      uint32_t pc;
      KosStackBlock *stack;
      PyObject *ns;
      uint32_t frame_flags;
    } frame;
    struct {
      uint32_t prompt_id;
      KosCode *handler_code;
      uint32_t handler_pc;
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
  } u;
};

typedef struct KosDynNode KosDynFrame;
typedef struct KosDynNode KosDynPrompt;
typedef struct KosDynNode KosDynWind;
typedef struct KosDynNode KosDynRegion;
typedef struct KosDynNode KosDynNs;
typedef struct KosDynNode KosDynCC;


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
  uint8_t one_shot;             /* 0 = multi-shot, 1 = one-shot */
  PyObject *token_ref;          /* KosEvaluatorToken (ownership marker) */
};


/* === KosTransferStep enum === */


/**
   KosTransferStep identifies the type of action in a transfer plan.

   KOS_XFER_RUN_HOOK  - Execute a wind/unwind hook block
   KOS_XFER_INSTALL   - Install a chain segment as current
   KOS_XFER_INJECT    - Push resume value onto stack (or throw)
*/

typedef enum KosTransferStep {
  KOS_XFER_RUN_HOOK = 0,
  KOS_XFER_INSTALL  = 1,
  KOS_XFER_INJECT   = 2,
} KosTransferStep;


/* === KosTransferAction: single transfer step === */


/**
   KosTransferAction represents one step in a transfer plan.
   The step field determines which union member is active.
*/

typedef struct KosTransferAction {
  KosTransferStep step;

  union {
    struct {
      KosCode *code;
      uint32_t pc;
    } hook;

    struct {
      KosDynNode *segment;
    } install;

    struct {
      PyObject *value;
      int is_throw;
    } inject;
  } data;

} KosTransferAction;


/* === KosTransferPlan: continuation transfer execution plan === */


/**
   KosTransferPlan holds the action list for a continuation transfer.

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

  KosTransferAction *actions;   /* action array */
  size_t action_count;          /* total actions */
  size_t action_index;          /* next action index */

  KosDynNode *original_target;  /* target for chain-change detection */
  uint32_t chain_version;       /* version at plan creation */
  uint32_t resume_pc;           /* UINT32_MAX = not set; set for invocation */
  KosDynFrame *resume_frame;    /* frame to resume into (invocation only) */
  KosStackBlock *resume_stack;  /* stack state at capture (invocation only) */
  PyObject *continuation_ref;   /* keep continuation alive until plan is freed */
  KosContinuation *invoke_clone;  /* when set, release then free when plan freed */
};


/* === KosHookRunner: hook execution state === */


/**
   KosHookRunner holds the state for executing a hook block.

   Hooks are bytecode blocks that run with an isolated scratch
   stack. They terminate when the END_WIND opcode is reached.

   The active flag indicates whether hook execution is in progress.
   When active, the VM dispatches from the hook code/pc instead of
   the normal frame.
*/

struct KosHookRunner {
  KosCode *code;          /* hook bytecode (borrowed) */
  uint32_t pc;            /* hook program counter */
  KosStackBlock *scratch; /* scratch operand stack (owned) */
  int active;             /* nonzero if hook is running */
};


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
   Pop the top node of the dynamic chain. Dispatches on node kind.
   For NS, restores frame_top->ns from the node's saved_ns before popping.

   :param ctx: execution context

   :return: 0 on success, -1 on error or if top is PROMPT/PYCALL
*/
int kos_dyn_pop_top(KosContext *ctx);


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

   :param a: first chain position
   :param b: second chain position

   :return: LCA node, or NULL if chains don't share an ancestor
*/
KosDynNode *kos_compute_lca(KosDynNode *a, KosDynNode *b);


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
   :param out_plan: output for transfer plan

   :return: 0 on success, -1 on error
*/
int kos_build_transfer_plan(
   KosContext *ctx,
   KosDynNode *target,
   PyObject *value,
   int is_throw,
   KosTransferPlan **out_plan);


/**
   Execute the next step of the active transfer plan.

   If the step is RUN_HOOK, begins hook execution and returns.
   If the step is INSTALL, splices the target chain.
   If the step is INJECT, pushes the resume value.

   :param ctx: execution context

   :return: 1 if plan complete, 0 if more steps remain, -1 on error
*/
int kos_run_transfer_step(KosContext *ctx);


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

#define KosDynNode_IS_FRAME(n)   (KosDynNode_KIND(n) == KOS_DYN_FRAME)
#define KosDynNode_IS_PROMPT(n)  (KosDynNode_KIND(n) == KOS_DYN_PROMPT)
#define KosDynNode_IS_WIND(n)    (KosDynNode_KIND(n) == KOS_DYN_WIND)
#define KosDynNode_IS_REGION(n)  (KosDynNode_KIND(n) == KOS_DYN_REGION)
#define KosDynNode_IS_NS(n)      (KosDynNode_KIND(n) == KOS_DYN_NS)
#define KosDynNode_IS_PYCALL(n)  (KosDynNode_KIND(n) == KOS_DYN_PYCALL)
#define KosDynNode_IS_CC(n)      (KosDynNode_KIND(n) == KOS_DYN_CC)

#define KosDynFrame_CODE(f)       (((KosDynNode *)(f))->u.frame.code)
#define KosDynFrame_PC(f)         (((KosDynNode *)(f))->u.frame.pc)
#define KosDynFrame_STACK(f)      (((KosDynNode *)(f))->u.frame.stack)
#define KosDynFrame_NS(f)         (((KosDynNode *)(f))->u.frame.ns)
#define KosDynFrame_FLAGS(f)      (((KosDynNode *)(f))->u.frame.frame_flags)

#define KosDynPrompt_ID(p)        (((KosDynNode *)(p))->u.prompt.prompt_id)
#define KosDynPrompt_HANDLER_CODE(p)  (((KosDynNode *)(p))->u.prompt.handler_code)
#define KosDynPrompt_HANDLER_PC(p)    (((KosDynNode *)(p))->u.prompt.handler_pc)

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


/* === Unit lifecycle === */


int koskari_dynchain_init(PyObject *mod);
int koskari_dynchain_clear(PyObject *mod);


#endif


// The end.
