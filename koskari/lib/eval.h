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
   koskari/lib/eval.h

   Evaluator core for Koskari: delimited multi-shot continuations,
   dynamic wind/unwind, and flattened async execution.

   This version uses the DynNode chain infrastructure from dynchain.h
   for unified dynamic extent tracking.

   :author: Christopher O'Brien <obriencj@preoccupied.net>
   :license: GNU General Public License v3
   :ai-assistant: Claude Opus 4.5 via Cursor
*/


#ifndef KOSKARI_EVAL_H
#define KOSKARI_EVAL_H


#include <Python.h>
#include <stdint.h>

#include "dynchain.h"


/* === KosEvaluatorToken === */


/**
   KosEvaluatorToken is a refcounted PyObject that identifies a koskari_eval
   domain/instance.  It tracks whether the evaluator is currently active on
   this thread (live_depth) and serves as an ownership marker for
   Continuations and DelimitedContinuations.
*/

struct KosEvaluatorToken {
  PyObject_HEAD
  uint64_t cookie;      /* unique identity */
  int live_depth;       /* incremented on koskari_eval entry, decremented on exit */
};

extern PyTypeObject KosEvaluatorTokenType;

KosEvaluatorToken *KosEvaluatorToken_New(void);
void KosEvaluatorToken_IncrementLiveDepth(KosEvaluatorToken *token);
void KosEvaluatorToken_DecrementLiveDepth(KosEvaluatorToken *token);


/* === Context flags === */


typedef enum KosContextFlags {
  KOS_CTX_RUNNING         = 0x01,
  KOS_CTX_FROZEN          = 0x02,
  KOS_CTX_DEAD            = 0x04,
  KOS_CTX_IN_HOOK         = 0x08,  /* executing a hook block */
  KOS_CTX_IN_TRANSFER     = 0x10,  /* executing a transfer plan */
} KosContextFlags;


/* === KosFrame: alias for KosDynFrame === */


/**
   KosFrame is an alias for KosDynFrame for backward compatibility.
   See dynchain.h for the actual structure definition.

   The frame represents a function activation in the dynamic chain.
   It holds the bytecode, program counter (pc = next instruction,
   acting_pc = instruction currently being executed), operand stack,
   and namespace.
*/

typedef KosDynFrame KosFrame;

#define KosFrame_CODE(f)       KosDynFrame_CODE(f)
#define KosFrame_PC(f)         KosDynFrame_PC(f)
#define KosFrame_ACTING_PC(f)  KosDynFrame_ACTING_PC(f)
#define KosFrame_STACK(f)      KosDynFrame_STACK(f)
#define KosFrame_NS(f)         KosDynFrame_NS(f)


/* === KosEvent: boundary events === */

/**
   KosEventKind enumerates the boundary conditions that can cause
   koskari_eval to suspend and return control to the caller.

   These events are produced only when the current frame has no parent
   (top-level completion). When the frame has a parent (e.g. callee or
   attached async gen), YIELD/RETURN are handled in-loop by detaching
   and pushing onto the parent.

   KOS_EV_RETURN - normal completion with a value
   KOS_EV_RAISE  - exception propagation
   KOS_EV_YIELD  - generator yield
   KOS_EV_AWAIT  - suspension on Python awaitable
*/

typedef enum KosEventKind {
  KOS_EV_RETURN = 0,
  KOS_EV_RAISE,
  KOS_EV_YIELD,
  KOS_EV_AWAIT,
} KosEventKind;


/**
   KosSuspensionEvent records why the evaluator last suspended.
   Set when koskari_eval returns with KOS_EV_AWAIT, KOS_EV_YIELD, or
   YIELD_FROM boundary; cleared when execution starts or context is not suspended.
   Used at entry to choose revive path (revive_plan, revive_await, revive_yield,
   revive_yield_from).
*/
typedef enum KosSuspensionEvent {
  KOS_SUSP_NONE      = 0,
  KOS_SUSP_AWAIT     = 1,
  KOS_SUSP_YIELD     = 2,
  KOS_SUSP_YIELD_FROM = 3,
} KosSuspensionEvent;


/**
   KosEvalMode is the result of entry/revive and drives the mode-handler loop.
   NORMAL = run next insn or check completion; TRANSFER = run process_plan;
   EXCEPTION = run process_raise; RAISE/AWAIT/YIELD/YIELD_FROM = return with event;
   RETURN = normal completion with value.
*/
typedef enum KosEvalMode {
  KOS_MODE_NORMAL    = 0,
  KOS_MODE_TRANSFER  = 1,
  KOS_MODE_EXCEPTION = 2,
  KOS_MODE_RAISE     = 3,
  KOS_MODE_AWAIT     = 4,
  KOS_MODE_YIELD     = 5,
  KOS_MODE_YIELD_FROM = 6,
  KOS_MODE_RETURN    = 7,
} KosEvalMode;


/**
   KosEvent carries the result of a koskari_eval call.

   The kind field indicates what boundary was reached.
   The payload field holds the associated value (return value,
   exception, yielded value, or awaitable).
*/

typedef struct KosEvent {
  KosEventKind kind;
  PyObject *payload;       /* new reference; caller owns it */
} KosEvent;


#define KosEvent_KIND(ev)    (((KosEvent *) (ev))->kind)
#define KosEvent_PAYLOAD(ev) (((KosEvent *) (ev))->payload)


/* === KosContext: VM execution context === */

/**
   KosContext is the top-level execution state for the Koskari VM.

   This version uses the DynNode chain for dynamic extent tracking.
   The dyn_top field points to the top of the chain, which may be
   any node type (FRAME, PROMPT, WIND, REGION, PYCALL).

   The frame_top field is a cached pointer to the topmost FRAME node
   for fast access during normal execution.

   Hook execution state is tracked via the hook_runner field.
   Transfer plan execution is tracked via the plan/plan_stack fields.

   Ownership and lifecycle:
   - A context is created via kos_context_new and freed via kos_context_free.
   - The context owns its dynamic chain and associated state.
   - While RUNNING, exactly one Python wrapper (KosPyCoroutine) may own it.

   Suspension and resumption:
   - When koskari_eval returns KOS_EV_AWAIT, the context is suspended.
   - py_await_iter holds the iterator from __await__() on the Python awaitable.
   - Resumption injects the result back into the VM via the stack.
*/

struct KosContext {
  KosDynNode *dyn_top;       /* top of dynamic chain */
  KosDynFrame *frame_top;    /* cached topmost frame (for fast access) */
  KosDynFrame *globals_frame;     /* cached outermost frame (for op_GLOBALS) */
  uint32_t globals_cache_version; /* chain_version when globals_frame was set */

  KosEvaluatorToken *token; /* evaluator identity; ownership marker for continuations */
  PyObject *domain;         /* KosDomain; ambient dynamic context anchor */

  /* Hook execution state */
  KosHookRunner hook_runner;
  KosDynFrame *unwind_target_frame;

  /* When unwind returns ASYNC_STARTED (wind after-hook), RETURN stashes the
     value here; END_WIND delivers it when unwind completes.  If
     pending_return_to_parent is set, push onto parent and detach; else
     set KOS_EV_RETURN. */
  PyObject *pending_return_value;
  int pending_return_to_parent;

  /* Transfer plan state */
  KosTransferPlan *plan;        /* active transfer plan (or NULL) */
  KosTransferPlan *plan_stack;  /* suspended plans stack */
  PyObject *push_shift_pending; /* wrapper to push after push_shift unwind (owned) */
  uint32_t chain_version;       /* incremented on chain modifications */
  int plan_suspended_await;     /* 1 when plan step returned awaitable and we suspended */
  PyObject *plan_resume_inject; /* inject value for suspended step on resume (owned) */

  /* Delimited continuation: top of installed segment when inside invoked k */
  KosDynNode *invocation_segment_top;

  /* Delimited k(value) return path: when set, RETURN from continuation body
   * runs a transfer plan back to the invocation site instead of normal return. */
  KosDynNode *return_target_chain;  /* cloned invocation-site chain (owned) */
  uint32_t return_target_pc;       /* pc to resume at (UINT32_MAX = not set) */

  /* Delimited push model: frame to return to when continuation hits POP_PROMPT */
  KosDynFrame *delimited_return_frame;  /* invocation site frame (owned ref) */
  uint32_t delimited_return_pc;         /* pc to resume at (UINT32_MAX = not set) */
  KosDynNode *delimited_return_node;    /* unwind stop (e.g. SHIFT) so POP_SHIFT can run */

  /* --- Python bridge: await/yield suspend and resume --- */
  PyObject *py_await_iter;   /* await-iterator from __await__() */
  PyObject *py_last_yielded; /* last yielded object (debug/optional) */
  PyObject *py_resume_inject; /* value to push on next koskari_eval when resuming */
  uint8_t py_resume_kind;    /* 0=none, 1=await, 2=yield */

  /* --- Exception injection (set by coroutine_throw or dynchain inject) --- */
  int inject_mode;       /* 0=none, 1=pending_throw */
  PyObject *inject_value;  /* exception object when inject_mode==1 */

  /* --- Suspension: why we last suspended (set on return AWAIT/YIELD; cleared when running) --- */
  uint8_t suspension_event;  /* KosSuspensionEvent */

  /* --- Pending async gen: NEXT and ITER_NEXT (Koskari gen) --- */
  void *pending_agen;        /* KosPyAsyncGen * when set (avoid func.h in eval.h) */
  PyObject *pending_agen_default;  /* default value for 2-arg NEXT (or NULL) */
  int pending_agen_n;       /* 1 or 2 for NEXT; 0 for ITER_NEXT (ITER_NEXT exhausted path reads from region) */

  /* Pending WITH: async enter/exit suspend (mgr != NULL or with_node != NULL when set) */
  struct {
    PyObject *mgr;
    PyObject *enter;
    PyObject *exit;
    PyObject *name;
    uint32_t resume_pc;
    int is_dyn_with;   /* 1 for PUSH_DYN_WITH, 0 for PUSH_WITH */
  } pending_with_enter;
  struct {
    KosDynWith *with_node;
    PyObject *exc_typ;
    PyObject *exc_val;
    PyObject *exc_tb;
    int normal_exit;  /* 1 = exit(None,None,None), 0 = exception */
  } pending_with_exit;

  uint32_t flags;            /* KosContextFlags */

  struct KosContext *context_freelist_next;  /* link when on context freelist only */
};


/* === KosContext accessor macros === */

#define KosContext_DYN_TOP(c)                (((KosContext *)(c))->dyn_top)
#define KosContext_FRAME_TOP(c)              (((KosContext *)(c))->frame_top)
#define KosContext_GLOBALS_FRAME(c)          (((KosContext *)(c))->globals_frame)
#define KosContext_GLOBALS_CACHE_VERSION(c)  (((KosContext *)(c))->globals_cache_version)
#define KosContext_TOKEN(c)                  (((KosContext *)(c))->token)
#define KosContext_DOMAIN(c)                 (((KosContext *)(c))->domain)
#define KosContext_HOOK_RUNNER(c)            (&((KosContext *)(c))->hook_runner)
#define KosContext_HOOK_ACTIVE(c)            KosHookRunner_ACTIVE(KosContext_HOOK_RUNNER(c))
#define KosContext_UNWIND_TARGET_FRAME(c)    (((KosContext *)(c))->unwind_target_frame)
#define KosContext_UNWIND_TARGET_FRAME(c)    (((KosContext *)(c))->unwind_target_frame)
#define KosContext_PENDING_RETURN_VALUE(c)   (((KosContext *)(c))->pending_return_value)
#define KosContext_PENDING_RETURN_TO_PARENT(c) (((KosContext *)(c))->pending_return_to_parent)
#define KosContext_PLAN(c)                   (((KosContext *)(c))->plan)
#define KosContext_PLAN_STACK(c)             (((KosContext *)(c))->plan_stack)
#define KosContext_PUSH_SHIFT_PENDING(c)     (((KosContext *)(c))->push_shift_pending)
#define KosContext_CHAIN_VERSION(c)          (((KosContext *)(c))->chain_version)
#define KosContext_PLAN_SUSPENDED_AWAIT(c)  (((KosContext *)(c))->plan_suspended_await)
#define KosContext_PLAN_RESUME_INJECT(c)     (((KosContext *)(c))->plan_resume_inject)
#define KosContext_INVOCATION_SEGMENT_TOP(c) (((KosContext *)(c))->invocation_segment_top)
#define KosContext_RETURN_TARGET_CHAIN(c)   (((KosContext *)(c))->return_target_chain)
#define KosContext_RETURN_TARGET_PC(c)      (((KosContext *)(c))->return_target_pc)
#define KosContext_DELIMITED_RETURN_FRAME(c) (((KosContext *)(c))->delimited_return_frame)
#define KosContext_DELIMITED_RETURN_PC(c)   (((KosContext *)(c))->delimited_return_pc)
#define KosContext_DELIMITED_RETURN_NODE(c) (((KosContext *)(c))->delimited_return_node)
#define KosContext_PY_AWAIT_ITER(c)          (((KosContext *)(c))->py_await_iter)
#define KosContext_PY_LAST_YIELDED(c)        (((KosContext *)(c))->py_last_yielded)
#define KosContext_PY_RESUME_INJECT(c)       (((KosContext *)(c))->py_resume_inject)
#define KosContext_PY_RESUME_KIND(c)         (((KosContext *)(c))->py_resume_kind)

#define KOS_RESUME_NONE  0
#define KOS_RESUME_AWAIT 1
#define KOS_RESUME_YIELD 2
#define KosContext_INJECT_MODE(c)            (((KosContext *)(c))->inject_mode)
#define KosContext_INJECT_VALUE(c)           (((KosContext *)(c))->inject_value)

#define KOS_INJECT_NONE   0
#define KOS_INJECT_THROW  1
#define KosContext_SUSPENSION_EVENT(c)      (((KosContext *)(c))->suspension_event)
#define KosContext_PENDING_AGEN(c)           (((KosContext *)(c))->pending_agen)
#define KosContext_PENDING_AGEN_DEFAULT(c)   (((KosContext *)(c))->pending_agen_default)
#define KosContext_PENDING_AGEN_N(c)         (((KosContext *)(c))->pending_agen_n)

#define KosContext_PENDING_WITH_ENTER_MGR(c)   (((KosContext *)(c))->pending_with_enter.mgr)
#define KosContext_PENDING_WITH_ENTER_ENTER(c) (((KosContext *)(c))->pending_with_enter.enter)
#define KosContext_PENDING_WITH_ENTER_EXIT(c)  (((KosContext *)(c))->pending_with_enter.exit)
#define KosContext_PENDING_WITH_ENTER_NAME(c)  (((KosContext *)(c))->pending_with_enter.name)
#define KosContext_PENDING_WITH_ENTER_RESUME_PC(c) (((KosContext *)(c))->pending_with_enter.resume_pc)
#define KosContext_PENDING_WITH_ENTER_IS_DYN(c)   (((KosContext *)(c))->pending_with_enter.is_dyn_with)

#define KosContext_PENDING_WITH_EXIT_NODE(c)   (((KosContext *)(c))->pending_with_exit.with_node)
#define KosContext_PENDING_WITH_EXIT_TYP(c)    (((KosContext *)(c))->pending_with_exit.exc_typ)
#define KosContext_PENDING_WITH_EXIT_VAL(c)    (((KosContext *)(c))->pending_with_exit.exc_val)
#define KosContext_PENDING_WITH_EXIT_TB(c)     (((KosContext *)(c))->pending_with_exit.exc_tb)
#define KosContext_PENDING_WITH_EXIT_NORMAL(c) (((KosContext *)(c))->pending_with_exit.normal_exit)

#define KosContext_FLAGS(c)                  (((KosContext *)(c))->flags)
#define KosContext_FREELIST_NEXT(c)          (((KosContext *)(c))->context_freelist_next)


/* === Context lifecycle === */


/**
   Create a new execution context with the given initial frame.

   :param top: initial frame (ownership transferred to context)

   :return: new context, or NULL on error
*/
KosContext *kos_context_new(KosDynFrame *top);


/**
   Free an execution context and all its resources.

   Frees the entire dynamic chain, any active transfer plan, and
   the hook runner scratch stack.

   :param ctx: context to free (may be NULL)
*/
void kos_context_free(KosContext *ctx);


/* === Frame management === */


/**
   Create a new frame.

   This is a convenience wrapper around kos_dynframe_new.

   :param code: bytecode object (borrowed)
   :param ns: namespace (reference is stolen)
   :param stack: operand stack (ownership transferred), or NULL
   :param parent: parent node in dynamic chain (or NULL)

   :return: new frame, or NULL on error
*/
KosFrame *kos_frame_new(
    KosCode *code,
    PyObject *ns,
    KosStackBlock *stack,
    KosDynNode *parent);


/**
   Clone a frame for continuation capture.

   Clones the frame's stack using COW semantics.

   :param frame: frame to clone

   :return: cloned frame, or NULL on error
*/
KosFrame *kos_frame_clone(KosFrame *frame);


/**
   Free a frame and its resources.

   :param frame: frame to free (may be NULL)
*/
void kos_frame_free(KosFrame *frame);


/* === Koskari call operations === */

/**
   Push a new frame for the given code object and namespace.

   This is a non-tail call: the current frame becomes the parent
   of the new frame.

   :return: 0 on success, -1 on error
*/
int kos_koscall(KosContext *ctx, KosCode *code, PyObject *ns);


/**
   Replace the current frame with a new frame for the given code.

   This is a tail call: the current frame is discarded and replaced
   by the new frame, preserving the parent chain.

   :return: 0 on success, -1 on error
*/
int kos_tail_koscall(KosContext *ctx, KosCode *code, PyObject *ns);


/* === Drive loop === */

/**
   Execute the VM until a boundary event occurs.

   This is the main entry point for driving execution. The function
   runs opcodes from ctx->frame_top until:
   - RETURN: normal completion
   - RAISE: exception propagation
   - YIELD: generator yield
   - AWAIT: Python awaitable encountered

   On return, out_ev is populated with the event kind and payload.
   The caller owns the reference in out_ev->payload.

   :param ctx: the execution context
   :param out_ev: output event structure
   :param send_val: resume value when resuming from AWAIT/YIELD/YIELD_FROM; NULL on first call or when N/A
   :param throw_exc: exception to inject when resuming with throw (e.g. coroutine.throw()); NULL when resuming with value or on first call

   :return: 0 on success, -1 on internal error
*/
int koskari_eval(KosContext *ctx, KosEvent *out_ev, PyObject *send_val, PyObject *throw_exc);


/**
   Return the active Koskari context during bytecode execution, or NULL
   when not inside koskari_eval.

   Callable from native Python object methods (e.g. ParamRef resolution).
*/
KosContext *kos_ctx_current(void);


/**
   Return the active KosDomain during bytecode execution, or NULL when
   not inside koskari_eval. Borrowed reference.
*/
PyObject *kos_domain_current(void);


/* === GC helpers for Python wrappers === */

/**
   Traverse a KosStackBlock chain for GC.

   This is used by Python wrapper types (KosCoroutine, etc.) to
   implement tp_traverse. It visits all PyObject* values held in
   the stack blocks.

   :param block: the head of the block chain
   :param visit: the visitproc callback
   :param arg: the opaque arg for visit

   :return: 0 on success, non-zero if visit returns non-zero
*/
int kos_stackblock_traverse(
    KosStackBlock *block,
    visitproc visit,
    void *arg);


/**
   Traverse a KosFrame for GC.

   :param frame: frame to traverse
   :param visit: visitproc callback
   :param arg: opaque arg for visit

   :return: 0 on success, non-zero if visit returns non-zero
*/
int kos_frame_traverse(KosFrame *frame, visitproc visit, void *arg);


/**
   Traverse a KosContext for GC.

   Visits all Python objects reachable from the context.

   :return: 0 on success, non-zero if visit returns non-zero
*/
int kos_context_traverse(KosContext *ctx, visitproc visit, void *arg);


/**
   Close the delegate of a YIELD_FROM dyn node (on unwind/error).
   Called from dynchain.c when popping KOS_DYN_YIELD_FROM. Does not free the node.
*/
void kos_yieldfrom_close_delegate(KosDynYieldFrom *node);


/**
   WITH enter: if result is awaitable stash and push_or_await (return 1 or -1);
   else bind name, push block, push node (return 0). Consumes result and
   takes ownership of mgr, enter, exit, name.

   :param use_dyn_with: 1 for dyn-with (KOS_DYN_DYN_WITH), 0 for with (KOS_DYN_WITH)
*/
int kos_push_with_after_enter(
    KosContext *ctx,
    KosEvent *out_ev,
    PyObject *result,
    PyObject *mgr,
    PyObject *enter,
    PyObject *exit,
    PyObject *name,
    uint32_t resume_pc,
    int use_dyn_with);


/**
   Complete pending WITH enter after await (TOS is the enter result).
   Returns 0 if no pending state or on success, -1 on error.
*/
int kos_complete_pending_with_enter(KosContext *ctx, KosEvent *out_ev);

/**
   Run dyn-with enter (wind): call enter, bind result to name if present.
   Used by transfer plan INSTALL when re-entering a captured dyn-with extent.
   Sync only.
*/
int kos_dyn_dyn_with_wind(KosContext *ctx, KosDynWith *node);

int kos_with_exit_start_await(KosContext *ctx, KosEvent *out_ev, PyObject *awaitable);

int kos_complete_pending_with_exit(KosContext *ctx);
int kos_complete_pending_with_exit_semantic(KosContext *ctx);
void kos_clear_pending_with_exit(KosContext *ctx);

/**
   Clear pending WITH enter stash. No-op if not set.
*/
void kos_clear_pending_with_enter(KosContext *ctx);


/* === Exception types === */


extern PyObject *KoskariContinuationDead;
extern PyObject *KoskariContinuationApply;
extern PyObject *KoskariContinuationError;


/* === Unit lifecycle === */


int koskari_eval_init(PyObject *mod);
int koskari_eval_clear(PyObject *mod);


#endif


// The end.
