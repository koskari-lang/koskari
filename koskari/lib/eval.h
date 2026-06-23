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
*/


#ifndef KOSKARI_EVAL_H
#define KOSKARI_EVAL_H


#include <Python.h>
#include <stdint.h>
#include <string.h>

#include "dynchain.h"
#include "tracing.h"


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

void KosEvaluatorToken_IncrementLiveDepth(KosEvaluatorToken *token);
void KosEvaluatorToken_DecrementLiveDepth(KosEvaluatorToken *token);


/* === Context flags === */


typedef enum KosContextFlags {
  KOS_CTX_RUNNING         = 0x01,
  KOS_CTX_FROZEN          = 0x02,
  KOS_CTX_DEAD            = 0x04,
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
#define KosFrame_NATIVE_HANDLER(f) KosDynFrame_NATIVE_HANDLER(f)


/* === KosEvent: boundary events === */

/**
  KosEventKind tags what kind of boundary the current instruction reported
  in the eval out_ev buffer.

  Opcode handlers set kind and payload when an instruction must signal
  return, yield, await, or raise.  The main loop and eval_phase_post_opcode
  then either stop the eval and return to the koskari_eval caller
  (KOS_STEP_STOP_EVENT), restart the full dispatch prologue
  (KOS_STEP_REPROLOGUE), or handle the boundary inside the loop without
  leaving (detach and deliver to a parent frame, push yielded values for
  flattened async iteration, run unwind handlers, and similar).  Nested
  calls and non-top frames use the same kinds; framing is not limited to
  top-level completion.

  KOS_EV_RETURN - normal completion with a value
  KOS_EV_RAISE  - exception propagation
  KOS_EV_YIELD  - generator yield
  KOS_EV_AWAIT  - suspension on Python awaitable
  KOS_EV_POST_OPCODE - eval-internal only: deferred second pass of
                   eval_phase_post_opcode; payload is PyLong holding op_res
                   (drained at dispatch prologue, never returned to drivers)
  KOS_EV_NONE   - out_ev cleared after an in-loop consume; not a driver-facing
                   boundary
*/
typedef enum KosEventKind {
  KOS_EV_NONE = 0,  // no event, or event cleared
  KOS_EV_RETURN,
  KOS_EV_RAISE,
  KOS_EV_YIELD,
  KOS_EV_AWAIT,
  KOS_EV_POST_OPCODE,
} KosEventKind;


/**
  KosSuspensionEvent records why the evaluator last suspended.
  Set when koskari_eval returns with KOS_EV_AWAIT, KOS_EV_YIELD, or
  YIELD_FROM boundary; cleared when execution starts or context is not suspended.
  Used at entry to choose revive path (revive_plan, revive_await, revive_yield).
*/
typedef enum KosSuspensionEvent {
  KOS_SUSP_NONE   = 0,
  KOS_SUSP_AWAIT  = 1,
  KOS_SUSP_YIELD  = 2,
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
  KosEvalStep is the return type of each internal phase helper inside
  koskari_eval.  CONTINUE means the phase completed normally and the next
  prologue step (or prepare/dispatch) should run in order.  REPROLOGUE means
  restart from the top of the dispatch prologue (transfer drain, pending
  exception, native frame) before the next opcode.  STOP_EVENT means out_ev
  has been filled and the evaluator should return to its caller.
  FATAL_ERROR means a PyErr has been set and koskari_eval must return -1.
*/
typedef enum KosEvalStep {
  KOS_STEP_CONTINUE,
  KOS_STEP_REPROLOGUE,
  KOS_STEP_STOP_EVENT,
  KOS_STEP_FATAL_ERROR,
} KosEvalStep;


/**
  Result of op_get_next_from_iter and low-level iter helpers.
*/
typedef enum KosGetNextResult {
  GET_NEXT_VALUE,
  GET_NEXT_EXHAUSTED,
  GET_NEXT_SUSPEND,
  GET_NEXT_ERROR
} KosGetNextResult;


/**
  KosLoopOpMode is the opcode-to-loop signal: what the main eval loop should
  do after each instruction.  Distinct from KosEvalMode (entry/revive only).

  KOS_LOP_RUN — continue with the next instruction without running the
  expensive prologue (transfer drain, pending-exception check, native
  frame branch).

  KOS_LOP_PROLOGUE — next cycle must run the full prologue before dispatch
  (e.g. PyErr set by RAISE opcode, new transfer plan, native handler installed).

  KOS_LOP_POST — run eval_phase_post_opcode with kos_op_post_tag_to_res(tag).

  KOS_LOP_FAIL — hard evaluator fault: koskari_eval returns -1 immediately;
  no post_opcode (differs from Python exception path, which uses PROLOGUE or
  POST with ERR tag and PyErr).
*/
typedef enum KosLoopOpMode {
  KOS_LOP_RUN = 0,
  KOS_LOP_PROLOGUE,
  KOS_LOP_POST,
  KOS_LOP_FAIL,
} KosLoopOpMode;


/**
  Post-opcode tags for KOS_LOP_POST only; passed to eval_phase_post_opcode as
  the legacy int (1 = suspend/async, 2 = control returned, -1 = error path,
  4 = parent return delivery after pop).
*/
#define KOS_OP_POST_TAG_SUSPEND 1
#define KOS_OP_POST_TAG_CONTROL_RETURNED 2
#define KOS_OP_POST_TAG_ERR 3
#define KOS_OP_POST_TAG_PARENT_RETURN 4


typedef struct KosOpOutcome {
  KosLoopOpMode mode;
  uint8_t post_tag;
  PyObject *value;  /* reserved for future use; must be NULL unless documented */
} KosOpOutcome;


#define KOS_OP_RUN() ((KosOpOutcome){ KOS_LOP_RUN, 0, NULL })
#define KOS_OP_PROLOGUE() ((KosOpOutcome){ KOS_LOP_PROLOGUE, 0, NULL })
#define KOS_OP_FAIL() ((KosOpOutcome){ KOS_LOP_FAIL, 0, NULL })
#define KOS_OP_POST_SUSPEND() \
  ((KosOpOutcome){ KOS_LOP_POST, KOS_OP_POST_TAG_SUSPEND, NULL })
#define KOS_OP_POST_CONTROL() \
  ((KosOpOutcome){ KOS_LOP_POST, KOS_OP_POST_TAG_CONTROL_RETURNED, NULL })
#define KOS_OP_POST_ERR() \
  ((KosOpOutcome){ KOS_LOP_POST, KOS_OP_POST_TAG_ERR, NULL })
#define KOS_OP_POST_PARENT_RETURN() \
  ((KosOpOutcome){ KOS_LOP_POST, KOS_OP_POST_TAG_PARENT_RETURN, NULL })


/**
  Opcode handler return values (legacy KosEvalState_Call / helpers).  0 = normal;
  1 = suspend or WITH async rollback; 2 = control returned to awaiting dyn node;
  4 = parent-frame return delivery staged in eval (post-pop).
*/
#define KOS_OP_RES_CONTROL_RETURNED 2
#define KOS_OP_RES_PARENT_RETURN_DELIVER 4


/**
  KosOpResultKind encodes what the opcode handler wants the eval dispatch loop
  to do after the handler returns.  NORMAL means "continue with the next
  instruction"; all other values name an event or control transfer.
*/
typedef enum KosOpResultKind {
  KOS_RES_NORMAL = 0,      /* continue to next opcode */
  KOS_RES_REPROLOGUE,      /* restart prologue (PyErr set, or plan armed) */
  KOS_RES_YIELD,           /* generator yielded val */
  KOS_RES_AWAIT,           /* suspend on awaitable val */
  KOS_RES_RETURN,          /* return val (needs walk+pop via eval_event_return) */
  KOS_RES_RAISE,           /* PyErr is set; run exception handler */
  KOS_RES_CONTROL_RETURNED,/* dyn-node control-returned hook */
  KOS_RES_PARENT_RETURN,   /* return already popped; deliver val to parent */
  KOS_RES_FATAL,           /* hard fault; koskari_eval returns -1 immediately */
} KosOpResultKind;


typedef struct KosOpResult {
  KosOpResultKind kind;
  PyObject *val;  /* strong ref where documented; NULL otherwise */
} KosOpResult;


#define EVAL_NORMAL()           ((KosOpResult){ KOS_RES_NORMAL, NULL })
#define EVAL_REPROLOGUE()       ((KosOpResult){ KOS_RES_REPROLOGUE, NULL })
#define EVAL_RAISE()            ((KosOpResult){ KOS_RES_RAISE, NULL })
#define EVAL_YIELD(v)           ((KosOpResult){ KOS_RES_YIELD, (v) })
#define EVAL_AWAIT(v)           ((KosOpResult){ KOS_RES_AWAIT, (v) })
#define EVAL_RETURN(v)          ((KosOpResult){ KOS_RES_RETURN, (v) })
#define EVAL_CONTROL_RETURNED() ((KosOpResult){ KOS_RES_CONTROL_RETURNED, NULL })
#define EVAL_PARENT_RETURN(v)   ((KosOpResult){ KOS_RES_PARENT_RETURN, (v) })
#define EVAL_FATAL()            ((KosOpResult){ KOS_RES_FATAL, NULL })


/**
  KosEvent is the out_ev buffer passed into koskari_eval for the whole run.

  Handlers and post_opcode write kind, payload, and exc_type as each
  instruction reports a boundary.  The same struct holds in-flight and
  in-loop state; not every kind/payload pair becomes the final reason the
  outer eval returns.  When evaluation stops with a boundary, the caller
  reads the fields by kind.

  payload holds the return value, yielded value, awaitable, or exception
  value as appropriate.  For KOS_EV_RAISE, exc_type is the exception type
  (from PyErr_Fetch).  payload and exc_type are owned references where set.

  Invariant: any code that assigns payload (or exc_type for RAISE) must set
  kind in the same boundary; do not leave a non-NULL payload with a stale or
  unspecified kind.

  Stack-local buffers: use KosEvent ev = {0}; aggregate init zeros the struct
  (kind is KOS_EV_NONE, pointers NULL).
*/

typedef struct KosEvent {
  KosEventKind kind;
  PyObject *payload;       /* new reference; caller owns it */
  PyObject *exc_type;      /* for KOS_EV_RAISE: exception type (new ref) */
} KosEvent;


#define KosEvent_KIND(ev)    (((KosEvent *) (ev))->kind)
#define KosEvent_PAYLOAD(ev) (((KosEvent *) (ev))->payload)
#define KosEvent_EXC_TYPE(ev) (((KosEvent *) (ev))->exc_type)


/**
  KosEvalResultKind is the driver-visible completion of koskari_eval when
  stopping with a value boundary.  KOS_ERES_RAISE uses PyErr as authority;
  value is always NULL for RAISE (out_ev may still carry exc_type when
  materialized from PyErr elsewhere).
*/
typedef enum KosEvalResultKind {
  KOS_ERES_RETURN = 0,
  KOS_ERES_RAISE,
  KOS_ERES_YIELD,
  KOS_ERES_AWAIT,
} KosEvalResultKind;


typedef struct KosEvalResult {
  KosEvalResultKind kind;
  PyObject *value;  /* strong ref; NULL for RAISE */
} KosEvalResult;


#define kos_eval_result_clear(r) Py_CLEAR((r)->value)


/* === KosContext: VM execution context === */

/**
  KosContext is the top-level execution state for the Koskari VM.

  This version uses the DynNode chain for dynamic extent tracking.
  The dyn_top field points to the top of the chain, which may be
  any node type (FRAME, PROMPT, WIND, REGION).

  The frame_top field is a cached pointer to the topmost FRAME node
  for fast access during normal execution.

  Transfer plan execution is tracked via the plan/plan_stack fields.

  Ownership and lifecycle:
  - A context is created via KosContext_New and destroyed via KosContext_Free.
  - The context owns its dynamic chain and associated state.
  - While RUNNING, exactly one Python wrapper (KosPyCoroutine) may own it.

  Suspension and resumption:
  - When koskari_eval returns KOS_EV_AWAIT, the context is suspended.
  - py_await_iter holds in-flight Python await machinery
    (iterator from __await__(), defer payload). Frozen GEN resume uses
    KosPyAsyncGen::gen plus kos_dyn_gen_install at the driver before
    re-entering koskari_eval.
  - Resumption injects the result back into the VM via the stack.

  Between koskari_eval drives, holds dyn_top, plan, plan_stack, and derived
  revive snapshots. During RUN those fields are NULL on ctx (stolen onto
  KosEvalState); see KosEvalState_ThawFromContext and KosEvalState_FreezeToContext.
*/

struct KosContext {
  KosDynNode *dyn_top;       /* top of dynamic chain */
  KosDynFrame *frame_top;    /* cached topmost frame (for fast access) */

  KosStackBlock **block;     /* non-owning: &innermost dynnode stack; frozen snapshot */
  PyObject *namespace;       /* borrowed: active namespace object */

  KosEvaluatorToken *token; /* evaluator identity; ownership marker for continuations */
  PyObject *domain;         /* KosDomain; ambient dynamic context anchor */

  KosDynFrame *unwind_target_frame;

  /* When unwind returns ASYNC_STARTED (wind after-thunk), RETURN stashes the
     value here and resumes unwinding when the thunk returns.  If
     pending_return_to_parent is set, push onto parent and detach; else
     set KOS_EV_RETURN. */
  PyObject *pending_return_value;
  int pending_return_to_parent;

  /* Transfer plan state */
  KosTransferPlan *plan;        /* active transfer plan (or NULL) */
  KosTransferPlan *plan_stack;  /* suspended plans stack */

  /* --- Python bridge: await/yield suspend and resume --- */
  PyObject *py_resume_inject;  /* value to push on next koskari_eval when resuming */
  uint8_t py_resume_kind;      /* 0=none, 1=await, 2=yield */
  PyObject *py_await_iter;     /* iterator from __await__() when suspended on AWAIT */

  /* --- Exception injection (set by coroutine_throw or dynchain inject) --- */
  int inject_mode;         /* 0=none, 1=pending_throw */
  PyObject *inject_type;   /* exception type when inject_mode==KOS_INJECT_THROW */
  PyObject *inject_value;  /* exception object when inject_mode==1 */

  /* Last eval boundary reason for revive */
  uint8_t suspension_event;  /* KosSuspensionEvent */

  uint32_t flags;            /* KosContextFlags */

  struct KosContext *context_freelist_next;  /* link when on context freelist only */
};


/* === KosContext accessor macros === */

#define KosContext_DYN_TOP(c)                (((KosContext *)(c))->dyn_top)
#define KosContext_FRAME_TOP(c)              (((KosContext *)(c))->frame_top)
#define KosContext_BLOCK(c)                  (((KosContext *)(c))->block)
#define KosContext_NAMESPACE(c)              (((KosContext *)(c))->namespace)
#define KosContext_TOKEN(c)                  (((KosContext *)(c))->token)
#define KosContext_DOMAIN(c)                 (((KosContext *)(c))->domain)
#define KosContext_UNWIND_TARGET_FRAME(c)    (((KosContext *)(c))->unwind_target_frame)
#define KosContext_PENDING_RETURN_VALUE(c)   (((KosContext *)(c))->pending_return_value)
#define KosContext_PENDING_RETURN_TO_PARENT(c) (((KosContext *)(c))->pending_return_to_parent)
#define KosContext_PLAN(c)                   (((KosContext *)(c))->plan)
#define KosContext_PLAN_STACK(c)             (((KosContext *)(c))->plan_stack)
#define KosContext_PY_AWAIT_ITER(c)          (((KosContext *)(c))->py_await_iter)
#define KosContext_PY_RESUME_INJECT(c)       (((KosContext *)(c))->py_resume_inject)
#define KosContext_PY_RESUME_KIND(c)         (((KosContext *)(c))->py_resume_kind)

#define KOS_RESUME_NONE  0
#define KOS_RESUME_AWAIT 1
#define KOS_RESUME_YIELD 2
#define KosContext_INJECT_MODE(c)            (((KosContext *)(c))->inject_mode)
#define KosContext_INJECT_TYPE(c)            (((KosContext *)(c))->inject_type)
#define KosContext_INJECT_VALUE(c)           (((KosContext *)(c))->inject_value)

#define KOS_INJECT_NONE   0
#define KOS_INJECT_THROW  1
#define KosContext_SUSPENSION_EVENT(c)      (((KosContext *)(c))->suspension_event)

#define KosContext_FLAGS(c)                  (((KosContext *)(c))->flags)
#define KosContext_FREELIST_NEXT(c)          (((KosContext *)(c))->context_freelist_next)


/* === KosEvalState: C-local execution snapshot (API scaffolding) === */

/**
  KosEvalState holds a borrowed pointer to the heap KosContext plus the live
  frame, code, operand-stack block pointer, and namespace for the active eval
  slice.

  See design/interpreter/freeze-thaw-context-segments.md and
  design/interpreter/eval-state-live-authority.md.

  KosEvalState_Init only binds ctx and clears es. koskari_eval calls
  KosEvalState_ThawFromContext to move dyn_top, plan, plan_stack, and derived
  snapshots from ctx onto es (NULL on ctx). While RUN, es owns chain and plan;
  KosEvalState_DYN_TOP and KosEvalState_PLAN never read ctx. Freeze restores
  onto ctx and NULLs es. Chain shape changes refresh live fields via
  kos_dyn_retarget_after_push/pop or kos_dyn_rebuild_live_from_chain.

  With KOS_TRACE_EVAL_SYNC, call counts and total wall time per sync API are
  recorded for kos_trace_report / get_trace_report under eval_state_sync.
*/

struct KosEvalState {
  KosContext *ctx;
  KosDynNode *dyn_top;       /* RUN: top of dynamic chain (stolen from ctx) */
  KosTransferPlan *plan;     /* RUN: active transfer plan */
  KosTransferPlan *plan_stack; /* RUN: suspended plans (via plan->suspended) */
  KosDynFrame *frame; /* live top frame; thaw and kos_dyn_* retarget */
  KosCode *code;      /* live code from frame */
  KosStackBlock **block; /* live innermost stack slot; non-owning */
  PyObject *namespace;   /* live namespace; borrowed */
  uint32_t pc;        /* live next-instruction PC (freeze copies to frame) */
  uint32_t acting_pc; /* live acting PC (freeze copies to frame) */
};


#define KosEvalState_CONTEXT(es) ((es)->ctx)
#define KosEvalState_DYN_TOP(es)     ((es)->dyn_top)
#define KosEvalState_PLAN(es)        ((es)->plan)
#define KosEvalState_PLAN_STACK(es)  ((es)->plan_stack)
#define KosEvalState_FRAME(es) ((es)->frame)
#define KosEvalState_CODE(es) ((es)->code)
#define KosEvalState_BLOCK(es) ((es)->block)
#define KosEvalState_NAMESPACE(es) ((es)->namespace)

/** Live next-instruction PC on es only; use KosEvalState_SET_PC to assign. */
#define KosEvalState_PC(es) ((es)->pc)
/** Instruction currently executing (last fetch); live on es only. */
#define KosEvalState_ACTING_PC(es) ((es)->acting_pc)
#define KosEvalState_SET_PC(es, v) ((void)((es)->pc = (uint32_t)(v)))


/**
  Operand stack head block for logging; NULL when no stack.
*/
#define KosEvalState_StackHead(es) \
  ((KosEvalState_BLOCK(es) && *(KosEvalState_BLOCK(es))) \
   ? (*(KosEvalState_BLOCK(es))) \
   : (KosStackBlock *) NULL)


#define KosEvalState_CurrentNode(es) \
  KosEvalState_DYN_TOP(es)

#define KosEvalState_CurrentCode(es) KosEvalState_CODE(es)

#define KosEvalState_CurrentNamespace(es) KosEvalState_NAMESPACE(es)

#define KosEvalState_StackPushRef(es, val) \
  KosStackBlock_PushRef(KosEvalState_BLOCK(es), (val))

#define KosEvalState_StackPUSH(es, val) \
  KosStackBlock_PUSH(KosEvalState_BLOCK(es), (val))

#define KosEvalState_StackPopRef(es) \
  KosStackBlock_PopRef(KosEvalState_BLOCK(es))

#define KosEvalState_GetFast(es, index) \
  KosNamespace_FastDeref(KosEvalState_CurrentNamespace(es), (index))

#define KosEvalState_SetFast(es, index, value) \
  KosNamespace_FastStore(KosEvalState_CurrentNamespace(es), (index), (value))

#define KosEvalState_GetVar(es, key) \
  KosNamespace_Deref(KosEvalState_CurrentNamespace(es), (key))

#define KosEvalState_StackSteal_Tail(es, s, count) \
  KosStackSteal_Tail((s), (count), KosEvalState_BLOCK(es))

#define KosEvalState_StackSteal_TailPrepend(es, s, prefix, count) \
  KosStackSteal_TailPrepend((s), (prefix), (count), KosEvalState_BLOCK(es))

#define KosEvalState_StackPopTail(es, count) \
  KosStackBlock_PopTail(KosEvalState_BLOCK(es), (count))

#define KosEvalState_StackTailPtr(es, count) \
  KosStackBlock_TailPtr(*KosEvalState_BLOCK(es), (count))

#define KosEvalState_StackLen(es) \
  KosStackBlock_LEN(*KosEvalState_BLOCK(es))

#define KosEvalState_StackDrop(es, count) \
  KosStackBlock_Drop(KosEvalState_BLOCK(es), (count))

#define KosEvalState_StackDrop1(es) \
  KosStackBlock_Drop1(KosEvalState_BLOCK(es))

#define KosEvalState_StackDropNoErr(es, count) \
  KosStackBlock_DropNoErr(KosEvalState_BLOCK(es), (count))

#define KosEvalState_StackClearBlock(es) \
  KosStackBlock_ClearBlock(KosEvalState_BLOCK(es))

#define KosEvalState_StackDuplicate(es, count) \
  KosStackBlock_Duplicate(KosEvalState_BLOCK(es), (count))

#define KosEvalState_StackPopTailDict(es, count) \
  KosStackBlock_PopTailDict(KosEvalState_BLOCK(es), (count))

#define KosEvalState_StackPopTailDictNames(es, count) \
  KosStackBlock_PopTailDictNames(KosEvalState_BLOCK(es), (count))

#define KosEvalState_StackPopTailDictFromNames(es, names) \
  KosStackBlock_PopTailDictFromNames(KosEvalState_BLOCK(es), (names))

#define KosEvalState_StackPeek(es) \
  KosStackBlock_Peek(*KosEvalState_BLOCK(es))

#define KosEvalState_StackPeekAt(es, index) \
  KosStackBlock_PeekAt(*KosEvalState_BLOCK(es), (index))

#define KosEvalState_StackPopTailPtr(es, count, dest) \
  KosStackBlock_PopTailPtr(KosEvalState_BLOCK(es), (count), (dest))

#define KosEvalState_SetSuspensionEvent(es, ev) \
  ((void)(KosContext_SUSPENSION_EVENT(KosEvalState_CONTEXT(es)) = (ev)))

#define KosEvalState_ClearSuspensionEvent(es) \
  ((void)(KosContext_SUSPENSION_EVENT(KosEvalState_CONTEXT(es)) \
          = KOS_SUSP_NONE))

#define KosEvalState_SuspensionEvent(es) \
  KosContext_SUSPENSION_EVENT(KosEvalState_CONTEXT(es))

#define KosEvalState_SuspendActivePlan(es) kos_suspend_transfer_plan(es)

#define KosEvalState_ResumeSuspendedPlan(es) kos_resume_transfer_plan(es)

#define KosEvalState_MarkSuspendedNotRunning(es) \
  do { \
    KosContext *kos_ctx_mark_sr_ = KosEvalState_CONTEXT(es); \
    KosContext_FLAGS(kos_ctx_mark_sr_) &= ~KOS_CTX_RUNNING; \
  } while (0)

#define KosEvalState_MarkDeadAfterYield(es) \
  do { \
    KosContext *kos_ctx_mark_dy_ = KosEvalState_CONTEXT(es); \
    KosContext_FLAGS(kos_ctx_mark_dy_) &= ~KOS_CTX_RUNNING; \
    KosContext_FLAGS(kos_ctx_mark_dy_) |= KOS_CTX_DEAD; \
  } while (0)

#define KosEvalState_MarkDeadAfterReturn(es) \
  do { \
    KosContext *kos_ctx_mark_dr_ = KosEvalState_CONTEXT(es); \
    KosContext_FLAGS(kos_ctx_mark_dr_) &= ~KOS_CTX_RUNNING; \
    KosContext_FLAGS(kos_ctx_mark_dr_) |= KOS_CTX_DEAD; \
  } while (0)

#define kos_frame_new(code, ns, stack, parent) \
  kos_dynframe_new((parent), (code), (ns), (stack))

#define kos_frame_clone(frame) kos_dynframe_clone(frame)

#define kos_frame_free(frame) kos_dynframe_free(frame)


void KosEvalState_Init(KosEvalState *es, KosContext *ctx);


/**
  Steal dyn_top, plan, plan_stack, and derived snapshots from ctx onto es and
  NULL those slots on ctx. Call at koskari_eval entry after Init. Does not
  steal ctx flags or Python bridge fields.
*/

void KosEvalState_ThawFromContext(KosEvalState *es);


/**
  Restore dyn_top, plan, plan_stack, and derived snapshots from es onto ctx and
  NULL es. Call from koskari_eval_cleanup after FlushLiveRegsToFrame. Updates
  KOS_CTX_IN_TRANSFER from restored plan.
*/

void KosEvalState_FreezeToContext(KosEvalState *es);


/** @deprecated name; use KosEvalState_FreezeToContext */
#define KosEvalState_FreezeDerivedToContext(es) KosEvalState_FreezeToContext(es)


/**
  Freeze live pc and acting_pc from es onto the current heap frame before the
  chain or current-frame pointer changes, or before another subsystem reads
  frame PC slots. No-op when es has no frame.
*/
static inline void KosEvalState_FlushLiveRegsToFrame(KosEvalState *es) {

  KosDynFrame *f;

  KOS_TRACE_EVAL_SYNC_BEGIN();

  f = KosEvalState_FRAME(es);
  if (! f)
    goto done;
  KosDynFrame_PC(f) = (es)->pc;
  KosDynFrame_ACTING_PC(f) = (es)->acting_pc;

done:
  KOS_TRACE_EVAL_SYNC_END(KOS_TEVS_flush_live_regs);
}


/**
  Replace any active transfer plan with plan and set KOS_CTX_IN_TRANSFER.
  plan must be non-NULL. Frees the previous plan if present.
*/
void KosEvalState_ArmTransferPlan(KosEvalState *es, KosTransferPlan *plan);

/**
  Stage return value for async unwind / parent delivery. to_parent is 0 or 1.
  Refcount semantics match direct assignments to ctx (no extra INCREF).
*/
void KosEvalState_SetPendingReturn(
    KosEvalState *es,
    PyObject *val,
    int to_parent);

/**
  Target frame for resume after async dynamic-wind pop (KOS_DYN_UNWIND_ASYNC_STARTED).
*/
void KosEvalState_SetUnwindTargetFrame(KosEvalState *es, KosDynFrame *frame);

int KosEvalState_SetFrameCodeAndNamespace(
    KosEvalState *es,
    KosDynFrame *frame,
    KosCode *code,
    PyObject *ns);

KosEvaluatorToken *KosEvalState_Token(const KosEvalState *es);

/**
  Eval-facing dynamic-chain transition dispatcher; implemented in
  eval/eval_chain.c included into eval.c. On -1, propagates PyErr.
*/
int KosEvalState_Transition(
    KosEvalState *es,
    const KosTransitionReq *req,
    KosTransitionResult *out,
    KosEvent *out_ev);

/**
  Innermost dyn node that owns a non-NULL operand stack (from dyn_top toward
  root). NULL if no explicit stack on the chain.
*/
KosDynNode *KosEvalState_InnermostStackOwner(KosEvalState *es);

void KosEvalState_PythonAwaitClearIter(KosEvalState *es);
PyObject *KosEvalState_PythonAwaitIter(KosEvalState *es);
void KosEvalState_PythonResumeSet(KosEvalState *es, PyObject *val, uint8_t kind);

int KosEvalState_ParentStackPushRef(KosEvalState *es, PyObject *val);
int KosEvalState_ParentStackPUSH(KosEvalState *es, PyObject *val);


/**
  Pop tail PyObject * fields into aggregate agg (deepest first, TOS last).
  Same layout contract as KOS_STACK_BLOCK_POP_ARGS in stack.h.
*/
#define KOS_EVALSTATE_POP_ARGS(es, agg) \
  KosStackBlock_PopTailPtr( \
      KosEvalState_BLOCK(es), \
      sizeof(agg) / sizeof(PyObject *), \
      (PyObject **)(void *)&(agg))


/* === Context lifecycle === */


/**
  Create a new execution context with the given initial frame.

  :param top: initial frame (ownership transferred to context)

  :return: new context, or NULL on error
*/
KosContext *KosContext_New(KosDynFrame *top);


/**
  Destroy an execution context and release all resources it owns.

  This runs real teardown, not only releasing the struct: freeing the
  dynamic chain walks every node and runs kind-specific free logic
  (including Py_DECREF on Python objects held by dyn nodes). That can
  invoke arbitrary Python code via destructors. It does not start
  koskari_eval or execute bytecode. Before walking the chain, KosEvalState_Init
  and KosEvalState_FlushLiveRegsToFrame run so frame PCs reflect the last
  live eval snapshot.

  Also frees active and stacked transfer plans, clears await/resume and
  inject fields, and drops domain and pending return state. The context
  record is returned to an internal freelist or PyMem_Free.

  :param ctx: context to destroy; NULL is a no-op
*/
void KosContext_Free(KosContext *ctx);


/* === Frame management === */

/**
  kos_frame_new, kos_frame_clone, and kos_frame_free are macros in this
  header (wrappers around kos_dynframe_*).
*/

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

  When non-NULL, send_val and throw_typ/throw_exc are consumed (ownership
  transferred). Caller must not use them after the call. Pass NULL to
  borrow nothing.

  :return: 0 on success, -1 on internal error
*/
int koskari_eval(
    KosContext *ctx,
    KosEvent *out_ev,
    PyObject *send_val,
    PyObject *throw_typ,
    PyObject *throw_exc);


/**
  Return the active Koskari context during bytecode execution, or NULL
  when not inside koskari_eval.

  Callable from native Python object methods (e.g. ParamRef resolution).
*/
KosContext *kos_ctx_current(void);


/**
  Push active KosEvalState for GC traverse while KOS_CTX_RUNNING (steal model).
*/

void kos_tls_push_active_eval(KosEvalState *es);


void kos_tls_pop_active_eval(void);


/**
  Innermost TLS eval slice for ctx, or NULL when not in koskari_eval on this thread.
*/

KosEvalState *kos_tls_active_eval_for_ctx(const KosContext *ctx);


/**
  Return the active KosDomain during bytecode execution, or NULL when
  not inside koskari_eval. Borrowed reference.
*/
PyObject *kos_domain_current(void);


/* === GC helpers for Python wrappers === */

/**
  Traverse a KosContext for GC.

  Visits all Python objects reachable from the context.

  :return: 0 on success, non-zero if visit returns non-zero
*/
int KosContext_Traverse(KosContext *ctx, visitproc visit, void *arg);


/* === Exception types === */


extern PyObject *KoskariContinuationDead;
extern PyObject *KoskariContinuationApply;
extern PyObject *KoskariContinuationError;


/* === KosEvalState_Call* : eval-state-oriented generic call entry points === */

/**
   KosEvalState_Call: generic call entry point with vectorcall-style args.

   Accepts any PyObject* callable. Performs KosCallable_Check; if true,
   dispatches to KosCallable_Call with the same es after applying frame
   behavior per wants_frame and tail_call. If false, invokes
   PyObject_Vectorcall and KosEvalState_PushOrAwait.

   Frame matrix (when KosCallable): wants_frame+tail -> reset; wants_frame+!tail
   -> create; !wants_frame+tail -> drop; !wants_frame+!tail -> no change.

   Caller must ensure stack is clear before calling when tail_call is true
   (use KosStackSteal). callable, args, kwnames are borrowed.

   Returns 0 continue, 1 suspend, -1 error. On tail_call success, may return
   KOS_OP_RES_CONTROL_RETURNED to signal control returned (wind thunks).
*/
int KosEvalState_Call(
    KosEvalState *es,
    KosEvent *out_ev,
    PyObject *callable,
    PyObject *const *args,
    Py_ssize_t nargs,
    PyObject *kwnames,
    int tail_call);


/**
   KosEvalState_CallArgs: generic call entry point with tuple+dict args.

   Same semantics as KosEvalState_Call but for tuple+dict form. Use for
   CALL_STAR, TAIL_CALL_STAR, wind after-thunk, dyn_with exit.

   callable, args_tuple, kwds_dict are borrowed.
*/
int KosEvalState_CallArgs(
    KosEvalState *es,
    KosEvent *out_ev,
    PyObject *callable,
    PyObject *args_tuple,
    PyObject *kwds_dict,
    int tail_call);


/**
   KosEvalState_CallNoArgs: fast path for zero-argument calls.

   Same semantics as KosEvalState_Call. Uses PyObject_CallNoArgs for
   non-KosCallable path. callable is borrowed.
*/
int KosEvalState_CallNoArgs(
    KosEvalState *es,
    KosEvent *out_ev,
    PyObject *callable,
    int tail_call);


/* === Unit lifecycle === */


/**
   Push val onto the current frame stack, or if val is awaitable run one step
   and either push the result or suspend with KOS_EV_AWAIT.

   Use from code outside opcode dispatch that must not apply the CALL* POP,1
   peephole. Implementation: koskari/lib/eval/eval_utils.c included into the
   eval translation unit.

   CALL* opcodes use a separate static op_push_or_await in the same translation
   unit, which may elide push plus a trailing POP,1.

   val is consumed (ownership transferred).

   :return: 0 continue, 1 stop (AWAIT), -1 error
*/
int KosEvalState_PushOrAwait(
    KosEvalState *es, KosEvent *out_ev, PyObject *val);


/**
   Move current PyErr into out_ev as KOS_EV_RAISE with injected traceback,
   clear the thread error state, and mark the context dead.  Call only when
   PyErr is set.  Used by dynchain local-exception delivery when unwinding
   does not find a handler.  When es is non-NULL, flushes live pc/acting_pc
   onto the top frame first so traceback and heap introspection see the
   current instruction.
*/
void kos_eval_emit_raise_from_pyerr(
    KosContext *ctx,
    struct KosEvent *out_ev,
    KosEvalState *es);


int koskari_eval_init(PyObject *mod);
int koskari_eval_clear(PyObject *mod);


#endif


// The end.
