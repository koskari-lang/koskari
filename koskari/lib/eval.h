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


/* === KosOpResult — defined before dynchain.h so dynchain can use it === */


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
  KOS_RES_DISPATCH,        /* resume instruction dispatch directly: live regs
                              valid, no PyErr, no plan, no resume.
                              Loop-internal only; must never escape
                              koskari_eval (post-op consumers normalize it) */
  KOS_RES_FATAL,           /* hard fault; koskari_eval returns -1 immediately */
} KosOpResultKind;


struct KosOpResult {
  KosOpResultKind kind;
  PyObject *val;  /* strong ref where documented; NULL otherwise */
};


#define EVAL_NORMAL()           ((KosOpResult){ KOS_RES_NORMAL, NULL })
#define EVAL_REPROLOGUE()       ((KosOpResult){ KOS_RES_REPROLOGUE, NULL })
#define EVAL_RAISE()            ((KosOpResult){ KOS_RES_RAISE, NULL })
#define EVAL_YIELD(v)           ((KosOpResult){ KOS_RES_YIELD, (v) })
#define EVAL_AWAIT(v)           ((KosOpResult){ KOS_RES_AWAIT, (v) })
#define EVAL_RETURN(v)          ((KosOpResult){ KOS_RES_RETURN, (v) })
#define EVAL_CONTROL_RETURNED() ((KosOpResult){ KOS_RES_CONTROL_RETURNED, NULL })
#define EVAL_DISPATCH()         ((KosOpResult){ KOS_RES_DISPATCH, NULL })
#define EVAL_FATAL()            ((KosOpResult){ KOS_RES_FATAL, NULL })


#include "dynchain.h"
#include "hints.h"
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


/**
  KosSuspensionEvent records why the evaluator last suspended.
  Set when koskari_eval suspends with AWAIT or YIELD; cleared when execution
  starts or the context is not suspended.
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
  EXCEPTION = run process_raise.
*/
typedef enum KosEvalMode {
  KOS_MODE_NORMAL    = 0,
  KOS_MODE_TRANSFER  = 1,
  KOS_MODE_EXCEPTION = 2,
} KosEvalMode;


/**
  Result of op_get_next_from_iter and low-level iter helpers.
*/
typedef enum KosGetNextResult {
  GET_NEXT_VALUE,
  GET_NEXT_EXHAUSTED,
  GET_NEXT_SUSPEND,
  GET_NEXT_ERROR
} KosGetNextResult;


/* KosThreadFn, the one prototype every threaded step shares (a CPS
   resume, an opcode handler), is defined in bytecode.h beside KosSlot,
   the instruction record that carries a handler pointer. */


/**
   A per-frame CPS resume: the callback a consumer sets on a frame via
   KosEvalState_CallResume / KosEvalState_NextResume to receive exactly
   one settled delivery -- a call's result, a callee frame's return, a
   Next request's element, or an awaited value, however it was
   produced.

   value is a new reference the resume owns, and is always settled: a
   delivery that still needs awaiting is awaited by the delivery point
   before the resume runs, however many times it takes. A resume never
   awaits, and never inspects a value to decide whether it must --
   that duplication across consumers is what these entries exist to
   remove. NULL with PyErr set means the operation failed (the resume
   may recover, or relay by returning EVAL_RAISE(), exactly as if it
   had received the NULL from an inline call).

   Returns a KosOpResult naming the next signal to the loop: another
   CPS entry call, EVAL_RETURN, EVAL_RAISE, EVAL_AWAIT, or
   EVAL_REPROLOGUE.
*/
typedef KosThreadFn KosResumeFn;


/* === Threaded control transfer === */


/**
   KOS_MUSTTAIL_ENABLED selects the threaded bodies of TAILCALL and
   DISPATCH, in which every step reaches the next by a guaranteed tail
   call. Off: the trampoline bodies, which are today's call and
   stage-and-bounce. Hardcoded until the dispatch loop can accept a
   threaded chain.
*/
#define KOS_MUSTTAIL_ENABLED 0

#if KOS_MUSTTAIL_ENABLED && KOS_HAVE_MUSTTAIL
# define KOS_MUSTTAIL __attribute__((musttail))
#else
# define KOS_MUSTTAIL
#endif


/**
   TAILCALL(fn, es, arg, value): hand control to fn, a KosThreadFn, as
   the calling function's last act. One bounded hop: the trampoline
   body is a plain call, so a chain of TAILCALLs whose length depends
   on data grows the C stack in that mode -- use DISPATCH for those.
   Any Py_DECREF the caller owes runs before this, never after; the
   threaded body discards the caller's frame.
*/
#if KOS_MUSTTAIL_ENABLED && KOS_HAVE_MUSTTAIL
# define TAILCALL(fn, es, arg, value) \
  KOS_MUSTTAIL return (fn)((es), (arg), (value))
#else
# define TAILCALL(fn, es, arg, value) \
  return (fn)((es), (arg), (value))
#endif


/**
   DISPATCH(fn, es, arg, value): deliver value to fn, a KosThreadFn,
   with constant C stack however many times the chain runs. The
   trampoline body stages the delivery: fn and arg become the current
   frame's resume, value (an owned reference, or NULL for a cold start
   with nothing staged) goes into the eval state's pending slot, and
   EVAL_REPROLOGUE returns to the prologue, whose delivery step takes
   the slot and invokes fn. The frame stack is not touched. The
   threaded body is the same tail call TAILCALL makes. Caller must
   have a current frame, and nothing may already be staged.
*/
#if KOS_MUSTTAIL_ENABLED && KOS_HAVE_MUSTTAIL
# define DISPATCH(fn, es, arg, value) \
  KOS_MUSTTAIL return (fn)((es), (arg), (value))
#else
# define DISPATCH(fn, es, arg, value)                                   \
  do {                                                                  \
    KosEvalState *kos_dsp_es_ = (es);                                   \
    KosDynFrame *kos_dsp_frame_ = KosEvalState_FRAME(kos_dsp_es_);      \
    PyObject *kos_dsp_val_ = (value);                                   \
    KOS_DISPATCH_SAFETYNET(kos_dsp_es_, kos_dsp_val_);                  \
    KosEvalState_PENDING_VALUE(kos_dsp_es_) = kos_dsp_val_;             \
    KosDynFrame_RESUME_FN(kos_dsp_frame_) = (void *) (fn);              \
    KosDynFrame_RESUME_STATE(kos_dsp_frame_) = (void *) (arg);          \
    return EVAL_REPROLOGUE();                                           \
  } while (0)
#endif

/* === Opcode result macros === */

/*
   The tails an opcode handler ends in. Each names what the handler
   did and where control goes next, so the same body reads correctly
   under both dispatch modes:

   - RESULT_NEXT(es): continue at the next instruction, es->pc.
   - RESULT_JUMP(es, target): continue at target.
   - RESULT_PUSH(es, val): push a borrowed val, then continue. A
     failed push leaves val with its caller.
   - RESULT_STEAL(es, val): push an owned val (consumed, on failure
     too; NULL raises), then continue.
   - RESULT_RAISE(): PyErr is set; hand off to the raise machinery.
   - RESULT_FAIL(): hard fault.

   Trampoline bodies are today's idioms: push and return NORMAL or
   RAISE, set pc and return NORMAL, return RAISE or FATAL. Threaded
   bodies thread straight into the handler at es->pc's slot -- read
   from es->pc rather than the handler's own slot plus one, since a
   handler may have moved the pc before its tail (a jump, the fused
   POP peephole) -- after advancing acting_pc and pc the way the
   dispatch loop's fetch does. RAISE and FAIL return in both modes:
   they are a real crossing back to the trampoline. Every threaded
   step is a tail call, so a handler using these macros must have the
   KosThreadFn prototype and must run any Py_DECREF it owes before
   the macro.
*/

#if KOS_MUSTTAIL_ENABLED && KOS_HAVE_MUSTTAIL

/* Thread into the handler at es->pc: the fetch the dispatch loop does,
   ending in a tail call instead of a computed goto. When the poll
   counter trips, return NORMAL instead: the trampoline polls, zeroes
   the counter, and threads again from es->pc. */
# define KOS_THREAD_NEXT(es)                                            \
  do {                                                                  \
    KosEvalState *kos_thr_es_ = (es);                                   \
    uint32_t kos_thr_pc_ = KosEvalState_PC(kos_thr_es_);                \
    KosSlot *kos_thr_next_;                                             \
    if (unlikely(++KosEvalState_POLL_OPS(kos_thr_es_)                   \
                 > KOS_EVAL_GIL_POLL))                                  \
      return EVAL_NORMAL();                                             \
    kos_thr_next_ =                                                     \
        &KosCode_SLOTS(KosEvalState_CODE(kos_thr_es_))[kos_thr_pc_];    \
    kos_thr_es_->acting_pc = kos_thr_pc_;                               \
    kos_thr_es_->pc = kos_thr_pc_ + 1u;                                 \
    TAILCALL(kos_thr_next_->fn, kos_thr_es_, kos_thr_next_, NULL);      \
  } while (0)

# define RESULT_NEXT(es)  KOS_THREAD_NEXT(es)

# define RESULT_JUMP(es, target)                                        \
  do {                                                                  \
    KosEvalState_SET_PC((es), (target));                                \
    KOS_THREAD_NEXT(es);                                                \
  } while (0)

# define RESULT_PUSH(es, val)                                           \
  do {                                                                  \
    if (unlikely(KosEvalState_StackPushRef((es), (val)) < 0))           \
      return EVAL_RAISE();                                              \
    KOS_THREAD_NEXT(es);                                                \
  } while (0)

# define RESULT_STEAL(es, val)                                          \
  do {                                                                  \
    PyObject *kos_rs_val_ = (val);                                      \
    if (unlikely(! kos_rs_val_))                                        \
      return EVAL_RAISE();                                              \
    if (unlikely(KosEvalState_StackPUSH((es), kos_rs_val_) < 0))        \
      return EVAL_RAISE();                                              \
    KOS_THREAD_NEXT(es);                                                \
  } while (0)

#else  /* trampoline */

# define RESULT_NEXT(es)                                                \
  do {                                                                  \
    (void) (es);                                                        \
    return EVAL_NORMAL();                                               \
  } while (0)

# define RESULT_JUMP(es, target)                                        \
  do {                                                                  \
    KosEvalState_SET_PC((es), (target));                                \
    return EVAL_NORMAL();                                               \
  } while (0)

# define RESULT_PUSH(es, val)                                           \
  do {                                                                  \
    return (KosEvalState_StackPushRef((es), (val)) < 0)                 \
        ? EVAL_RAISE() : EVAL_NORMAL();                                 \
  } while (0)

# define RESULT_STEAL(es, val)                                          \
  do {                                                                  \
    PyObject *kos_rs_val_ = (val);                                      \
    if (unlikely(! kos_rs_val_))                                        \
      return EVAL_RAISE();                                              \
    return (KosEvalState_StackPUSH((es), kos_rs_val_) < 0)              \
        ? EVAL_RAISE() : EVAL_NORMAL();                                 \
  } while (0)

#endif

#define RESULT_RAISE()  return EVAL_RAISE()
#define RESULT_FAIL()   return EVAL_FATAL()


/* At most one delivery is staged at a time; a second is an internal
   sequencing bug, reported under the safety net instead of leaking. */
#if KOS_EVAL_SAFETYNET
# define KOS_DISPATCH_SAFETYNET(es, val)                                \
  do {                                                                  \
    if (unlikely(KosEvalState_PENDING_VALUE(es))) {                     \
      Py_XDECREF(val);                                                  \
      PyErr_SetString(PyExc_SystemError,                                \
                      "CPS delivery staged over a live one");           \
      return EVAL_RAISE();                                              \
    }                                                                   \
  } while (0)
#else
# define KOS_DISPATCH_SAFETYNET(es, val)  ((void) 0)
#endif


/**
   Complete a code-less frame with a return value: run the return
   unwind walk from the frame, honor FINALLY/async-unwind outcomes,
   detach and free the frame, and deliver the value to the parent.
   value is consumed.
*/
KosOpResult KosNativeFrame_Return(KosEvalState *es, PyObject *value);


/**
   Complete a code-less frame by handing value to the eval loop's
   return machinery, which owns every delivery terminus (parent frame,
   iteration node, drive root). When cache is given, sets the frame to
   park itself into that caller-owned one-frame cache when the return
   path frees it (kos_dynframe_park_inert); a NULL or occupied cache
   frees as usual. The exception path never parks -- a frame popped by
   the raise walk is freed, and the cache simply stays empty for the
   next install. The caller returns this result from its resume. value
   is consumed.
*/
KosOpResult KosNativeFrame_ReturnPark(
    KosEvalState *es,
    PyObject *value,
    KosDynFrame **cache);


/**
   Push a code-less frame onto the chain for CPS resume use. When cache
   holds a parked frame it is reinstalled in place of a fresh
   allocation and *cache is emptied. The frame carries only name (for a
   capture refusal message, see kos_dynframe_clone) -- it has no resume
   set until the caller's next KosEvalState_CallResume / NextResume
   call sets one. Returns the frame, or NULL with PyErr set.
*/
KosDynFrame *KosResumeFrame_Install(
    KosEvalState *es,
    const char *name,
    KosDynFrame **cache);


/**
   Request the next element of iter from inside a code-less frame:
   push a one-request iteration node for iter and run one request
   through KosIterRequest_Next. On DELIVERED *out is the element (new
   ref) and the node is already cleaned up; on EXHAUSTED and ERROR the
   node is cleaned up and *out is NULL (no exception is set for
   EXHAUSTED; ERROR leaves PyErr set). On CONTINUES the node remains
   with installed work above it and the element later lands on the
   native frame's stack (an ITERATE wake pops it); on EXIT the node
   remains and *out is the awaitable to suspend on (EVAL_AWAIT), the
   element arriving the same way.
*/
KosIterRequestStatus KosNativeFrame_RequestNext(
    KosEvalState *es,
    PyObject *iter,
    PyObject **out);


/**
   Request the next element of iter, delivering it (or the failure) to
   resume instead of making the caller interpret KosIterRequestStatus
   by hand. A delivered element and an in-flight CONTINUES both stage
   through the current frame's own stack and set resume there, so the
   delivery always bounces through the trampoline rather than calling
   resume inline (constant stack across a synchronous run of many
   elements). An awaitable in-flight (EXIT) sets resume the same way
   and returns EVAL_AWAIT; the AWAITED revive delivers into it.
   Exhaustion raises StopIteration, offered to resume via the raise
   walk like any other failure. send_val is accepted for a future Send
   entry point; unused today. Caller must have a current frame.

   resume may be NULL: no continuation, behave exactly like the
   OBJECT_NEXT opcode's own default -- push the delivered element and
   return EVAL_NORMAL; on an in-flight CONTINUES, return EVAL_NORMAL
   only if the install already left a real bytecode frame current (no
   trampoline hop needed), EVAL_REPROLOGUE otherwise; an in-flight EXIT
   still returns EVAL_AWAIT, since revival for a plain frame with no
   resume set already works the way ordinary AWAIT revival always has.
   Never valid from a codeless caller -- exactly as unsafe as any other
   plain frame receiving a delivery today, not a new contract NULL
   introduces.
*/
KosOpResult KosEvalState_NextResume(
    KosEvalState *es,
    PyObject *iter,
    PyObject *send_val,
    KosResumeFn resume,
    void *state);


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
  - When koskari_eval returns KOS_RES_AWAIT, the context is suspended.
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

  KosSegment **block;        /* non-owning: &innermost dynnode stack; frozen snapshot */
  PyObject *namespace;       /* borrowed: active namespace object */

  KosEvaluatorToken *token; /* evaluator identity; ownership marker for continuations */
  PyObject *domain;         /* KosDomain; mandatory. Inherited from the
                               ambient domain at creation, minted fresh when
                               none is ambient. Identity/lifecycle anchor;
                               live parameter state is in param_refs. */
  PyObject *param_refs;    /* PyDict: name -> cell (KosRef); this context's
                               parameter cell table, forked (pointer-copied)
                               at creation. Lazy; NULL when empty. */

  /* When unwind returns ASYNC_STARTED (wind after-thunk), RETURN stashes the
     value here and resumes unwinding when the thunk returns.  If
     pending_return_to_parent is set, push onto parent and detach; else
     return KOS_RES_RETURN. */
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

  /* --- Exception stash: LIFO of exceptions parked while a teardown or
     handler works (propagation state; survives chain surgery). PyList of
     (exception instance, parking node_id) tuples, or NULL when empty. --- */
  PyObject *exc_stash;

  /* --- Handler-visible exception state (sys.exc_info) saved across
     suspensions: swapped with the thread state at koskari_eval
     entry/exit so interleaved contexts on one thread cannot corrupt
     each other's view. Context-isolated: a fresh context starts with
     empty handler state (no fall-through to the host's). --- */
  PyObject *exc_info_type;
  PyObject *exc_info_value;
  PyObject *exc_info_tb;

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
#define KosContext_PARAM_REFS(c)            (((KosContext *)(c))->param_refs)
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
#define KosContext_EXC_STASH(c)              (((KosContext *)(c))->exc_stash)
#define KosContext_EXC_INFO_TYPE(c)          (((KosContext *)(c))->exc_info_type)
#define KosContext_EXC_INFO_VALUE(c)         (((KosContext *)(c))->exc_info_value)
#define KosContext_EXC_INFO_TB(c)            (((KosContext *)(c))->exc_info_tb)

#define KOS_INJECT_NONE   0
#define KOS_INJECT_THROW  1
#define KosContext_SUSPENSION_EVENT(c)      (((KosContext *)(c))->suspension_event)

#define KosContext_FLAGS(c)                  (((KosContext *)(c))->flags)
#define KosContext_FREELIST_NEXT(c)          (((KosContext *)(c))->context_freelist_next)


/* === KosEvalState: C-local execution snapshot (API scaffolding) === */

/**
  KosEvalState holds a borrowed pointer to the heap KosContext plus the live
  frame, code, operand-segment slot, and namespace for the active eval
  slice.

  See design/interpreter/eval-state-live-authority.md and the archived
  design/archive/freeze-thaw-context-segments.md.

  KosEvalState_Init only binds ctx and clears es. koskari_eval calls
  KosEvalState_ThawFromContext to move dyn_top, plan, plan_stack, and derived
  snapshots from ctx onto es (NULL on ctx). While RUN, es owns chain and plan;
  KosEvalState_DYN_TOP and KosEvalState_PLAN never read ctx. Freeze restores
  onto ctx and NULLs es. Chain shape changes refresh live fields via
  kos_dyn_resync_after_push/pop or kos_dyn_resync_chain.

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
  KosSegment **block; /* live innermost stack slot; non-owning */
  PyObject *namespace; /* live namespace; borrowed */
  uint32_t pc;        /* live next-instruction PC (freeze copies to frame) */
  uint32_t acting_pc; /* live acting PC (freeze copies to frame) */
  PyObject *pending_value; /* a staged CPS delivery (owned) awaiting the
                              current frame's resume; NULL when none.
                              Live transit only: freeze moves it onto
                              the frame stack, and the delivery step
                              takes it before the resume runs */
  uint32_t poll_ops;  /* instructions since the last GIL / signal poll;
                         the trampoline zeroes it at each poll and
                         every continuing step increments it */
};


/* GIL / signal poll cadence: a poll runs once this many instructions
   have run since the last one, and a GIL drop is offered at most once
   per interval. */
#define KOS_EVAL_GIL_POLL                1024u
#define KOS_EVAL_GIL_POLL_INTERVAL_NS    5000000ULL  /* 5 ms */


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
/** The staged CPS delivery, owned; NULL when nothing is staged. */
#define KosEvalState_PENDING_VALUE(es) ((es)->pending_value)
/** Instructions run since the last GIL / signal poll. */
#define KosEvalState_POLL_OPS(es) ((es)->poll_ops)


/**
  Operand stack head block for logging; NULL when no stack.
*/
#define KosEvalState_StackHead(es) \
  ((KosEvalState_BLOCK(es) && *(KosEvalState_BLOCK(es))) \
   ? (*(KosEvalState_BLOCK(es))) \
   : (KosSegment *) NULL)


#define KosEvalState_CurrentNode(es) \
  KosEvalState_DYN_TOP(es)

#define KosEvalState_CurrentCode(es) KosEvalState_CODE(es)

#define KosEvalState_CurrentNamespace(es) KosEvalState_NAMESPACE(es)

/* The current namespace, GC-revealed (tracked along its parent chain)
   and INCREF'd, ready for storage. See KosNamespace_Reveal. */
#define KosEvalState_RevealNamespace(es) \
  KosNamespace_Reveal(KosEvalState_NAMESPACE(es))

#define KosEvalState_StackPushRef(es, val) \
  kos_stack_push_ref_fast(KosEvalState_BLOCK(es), (val))

#define KosEvalState_StackPUSH(es, val) \
  kos_stack_push_steal_fast(KosEvalState_BLOCK(es), (val))

#define KosEvalState_StackPopRef(es) \
  kos_stack_pop_ref_fast(KosEvalState_BLOCK(es))

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
  KosStack_PopTail(KosEvalState_BLOCK(es), (count))

#define KosEvalState_StackTailPtr(es, count) \
  kos_stack_tail_ptr_fast(*KosEvalState_BLOCK(es), (count))

#define KosEvalState_StackLen(es) \
  KosSegment_LEN(*KosEvalState_BLOCK(es))

#define KosEvalState_StackDrop(es, count) \
  KosStack_Drop(KosEvalState_BLOCK(es), (count))

#define KosEvalState_StackDrop1(es) \
  kos_stack_drop1_fast(KosEvalState_BLOCK(es))

#define KosEvalState_StackDropNoErr(es, count) \
  kos_stack_drop_noerr_fast(KosEvalState_BLOCK(es), (count))

#define KosEvalState_StackClear(es) \
  KosStack_Clear(KosEvalState_BLOCK(es))

#define KosEvalState_StackDuplicate(es, count) \
  KosStack_Duplicate(KosEvalState_BLOCK(es), (count))

#define KosEvalState_StackPopTailDict(es, count) \
  KosStack_PopTailDict(KosEvalState_BLOCK(es), (count))

#define KosEvalState_StackPopTailDictNames(es, count) \
  KosStack_PopTailDictNames(KosEvalState_BLOCK(es), (count))

#define KosEvalState_StackPopTailDictFromNames(es, names) \
  KosStack_PopTailDictFromNames(KosEvalState_BLOCK(es), (names))

#define KosEvalState_StackPeek(es) \
  kos_stack_peek_fast(*KosEvalState_BLOCK(es))

#define KosEvalState_StackPeekAt(es, index) \
  KosStack_PeekAt(*KosEvalState_BLOCK(es), (index))

#define KosEvalState_StackPopTailPtr(es, count, dest) \
  kos_stack_pop_tail_ptr_fast(KosEvalState_BLOCK(es), (count), (dest))

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

int KosEvalState_SetFrameCodeAndNamespace(
    KosEvalState *es,
    KosDynFrame *frame,
    KosCode *code,
    PyObject *ns);

KosEvaluatorToken *KosEvalState_Token(const KosEvalState *es);

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
  kos_stack_pop_tail_ptr_fast( \
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


/**
   Make a context reusable after a completion at its root: drop the
   finished frame chain and clear the completion residue -- dead and
   running flags, suspension tag, in-flight await iterator, resume
   inject. A context holding state this reset cannot account for (an
   armed transfer plan) is left untouched and the caller must free it
   instead.

   :return: 0 recycled; -1 the context cannot be recycled
*/
int KosContext_Recycle(KosContext *ctx);


/* === Frame management === */

/**
  kos_frame_new, kos_frame_clone, and kos_frame_free are macros in this
  header (wrappers around kos_dynframe_*).
*/

/* === Drive loop === */

/**
   Duck-typed asyncio.isfuture: futures and tasks are handles by
   convention, exempt from ambient auto-await at implicit sites.

   :return: 1 (future), 0 (not a future), -1 (error; PyErr set)
*/
int KosEval_IsFuture(PyObject *val);


/**
   Classify a produced result under ambient async: a non-awaitable, a
   Defer, or a future/task handle is a completed value; anything else
   awaitable must be awaited before use.

   :param result: borrowed
   :return: 1 (value), 0 (awaitable), -1 (error; PyErr set)
*/
int KosEval_ResultIsValue(PyObject *result);


/**
   Acquire an iterator from an iterable, uniform over both protocols
   and shared by every acquisition site (PUSH_FOREACH, the *_ITER
   opcodes, KosMap): a KosIter provider passes through as itself, an
   object with am_aiter yields its async iterator, otherwise the sync
   protocol (PyObject_GetIter, including the sequence fallback).

   :param iterable: borrowed
   :return: new iterator reference, or NULL with PyErr set
*/
PyObject *kos_iter_acquire(PyObject *iterable);


/**
   Close an acquired iterator, best-effort and exception-fenced: a
   KosIter provider through its suite's ki_close, a sync iterator
   through close(), an async iterator through aclose() -- the aclose
   awaitable is demand-driven, so an immediate completion honors the
   cleanup and a genuine suspension takes the cannot-honor fallback of
   closing the in-flight iterator. A missing close method is ignored.
   Any pending exception is preserved across the close.

   :param iter: borrowed; NULL is a no-op
*/
void kos_iter_close(PyObject *iter);


/**
   One iteration request against a iteration node's armed record: the
   single site API for stepping the node's iterator over all three
   protocols. send_val borrowed; *fast_out new ref on
   DELIVERED/EXHAUSTED, the awaitable payload on EXIT.
*/
KosIterRequestStatus KosIterRequest_Next(
    KosEvalState *es,
    KosDynNode *node,
    PyObject *send_val,
    PyObject **fast_out);


/**
   Deliver a typed event into a iteration node's iteration request: the
   one entry the eval delivery points call. value is consumed.
*/
KosOpResult KosIterRequest_Deliver(
    KosEvalState *es,
    KosDynNode *node,
    KosIterWakeEvent ev,
    PyObject *value);


/**
  Execute the VM until a boundary event occurs.

  This is the main entry point for driving execution. The function
  runs opcodes from ctx->frame_top until:
  - RETURN: normal completion (KOS_RES_RETURN, val is new ref)
  - RAISE: exception propagation (KOS_RES_RAISE, PyErr set)
  - YIELD: generator yield (KOS_RES_YIELD, val is new ref)
  - AWAIT: Python awaitable encountered (KOS_RES_AWAIT, val is new ref)

  The caller owns the reference in the returned KosOpResult.val.

  :param ctx: the execution context
  :param send_val: resume value when resuming from AWAIT/YIELD/YIELD_FROM;
      NULL on first call or when N/A
  :param throw_exc: exception to inject when resuming with throw; NULL when
      resuming with value or on first call

  send_val and the throw pair are borrowed: the entry phase takes its
  own references for whatever it delivers, and the caller releases its
  own afterward.
*/
KosOpResult koskari_eval(
    KosContext *ctx,
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
   PyObject_Vectorcall and pushes or auto-awaits the result.

   Frame matrix (when KosCallable): wants_frame+tail -> reset; wants_frame+!tail
   -> create; !wants_frame+tail -> drop; !wants_frame+!tail -> no change.

   Caller must ensure stack is clear before calling when tail_call is true
   (use KosStackSteal). callable, args, kwnames are borrowed.
*/
KosOpResult KosEvalState_Call(
    KosEvalState *es,
    PyObject *callable,
    PyObject *const *args,
    Py_ssize_t nargs,
    PyObject *kwnames,
    int tail_call);


/**
   Call callable, delivering the settled result (or failure) to resume
   instead of making the caller interpret KosEvalState_Call's own
   result by hand. Every arm sets resume on the current frame before
   returning: an installed callee (NORMAL/REPROLOGUE) delivers by
   frame return; a synchronous RETURN value stages onto the frame's
   own stack and bounces through the trampoline the same way; a
   RETURN value needing await, or the call's own result needing
   await, both set resume and let the AWAITED revive deliver into it.
   RAISE / FATAL relay untouched -- resume is offered the failure via
   the raise walk instead. Caller must have a current frame; not a
   tail call.

   resume may be NULL: no continuation, behave exactly like an
   ordinary CALL opcode -- NORMAL/REPROLOGUE (installed, or already
   pushed by Call's own plain-callable path) relay untouched; a
   synchronous RETURN value pushes (with the same ambient-await check
   every push already gets) instead of setting a resume. Never valid
   from a codeless caller -- exactly as unsafe as any other plain
   frame receiving a return today, not a new contract NULL introduces.
*/
KosOpResult KosEvalState_CallResume(
    KosEvalState *es,
    PyObject *callable,
    PyObject *const *args,
    Py_ssize_t nargs,
    PyObject *kwnames,
    KosResumeFn resume,
    void *state);


/**
   KosEvalState_CallArgs: generic call entry point with tuple+dict args.

   Same semantics as KosEvalState_Call but for tuple+dict form. Use for
   CALL_STAR, TAIL_CALL_STAR, wind after-thunk, dyn_with exit.

   callable, args_tuple, kwds_dict are borrowed.
*/
KosOpResult KosEvalState_CallArgs(
    KosEvalState *es,
    PyObject *callable,
    PyObject *args_tuple,
    PyObject *kwds_dict,
    int tail_call);


/**
   KosEvalState_CallNoArgs: fast path for zero-argument calls.

   Same semantics as KosEvalState_Call. Uses PyObject_CallNoArgs for
   non-KosCallable path. callable is borrowed.
*/
KosOpResult KosEvalState_CallNoArgs(
    KosEvalState *es,
    PyObject *callable,
    int tail_call);


/* === Unit lifecycle === */


/**
   Push val onto the current frame stack, or if val is awaitable run one step
   and either push the result or suspend.

   Use from code outside opcode dispatch that must not apply the CALL* POP,1
   peephole. Implementation: koskari/lib/eval/eval_utils.c included into the
   eval translation unit.

   CALL* opcodes use a separate static push_or_await_fused in the same
   translation unit, which may elide push plus a trailing POP,1.

   val is consumed (ownership transferred).
*/
KosOpResult KosEvalState_PushOrAwait(KosEvalState *es, PyObject *val);


/**
   Settle a delivery that still needs awaiting and stage the outcome
   for the current frame's resume: one eager step through
   KosEvalState_PushOrAwait, then the settled result moves from the
   frame stack into the pending slot and EVAL_REPROLOGUE returns to
   the prologue, which classifies it again (a value that settles to a
   further awaitable bounces again). A genuine suspension relays
   EVAL_AWAIT; a failure relays EVAL_RAISE. Sets no resume itself --
   the caller has set one, or sets one on the REPROLOGUE / AWAIT
   outcome, per its own contract. val is consumed.
*/
KosOpResult KosEvalState_SettleStage(KosEvalState *es, PyObject *val);


/**
   The trampoline's re-entry: everything that must happen before the
   next instruction is fetched, whenever control comes back to the
   loop by a signal rather than by the fetch itself. In order: a
   staged CPS delivery with no pending exception and no plan goes
   straight to its resume; a pending exception runs the raise walk; an
   armed transfer plan drains; a frame waiting on a resume takes its
   delivery. Each of those may ask to run again, and this function
   loops until none does.

   :return: EVAL_NORMAL when instruction dispatch may proceed from
       es->pc; otherwise the terminal result koskari_eval returns
       (AWAIT, YIELD, RETURN, RAISE, FATAL)
*/
KosOpResult kos_prologue_reenter(KosEvalState *es);


/**
   Inject traceback into the current PyErr in-place, leave PyErr set, and
   mark the context dead.  Call only when PyErr is set.  The RAISE invariant
   requires PyErr to remain set on every KOS_RES_RAISE boundary.
   When es is non-NULL, flushes live pc/acting_pc first.
*/
void kos_eval_prepare_boundary_raise(
    KosContext *ctx,
    KosEvalState *es);


/**
   Record the current frame into the in-flight exception's
   __koskari_dynchain__ metadata (marked "crossed") before the raise walk
   pops it. PyErr is preserved; decoration is best-effort.
*/
void kos_eval_note_crossed_frame(KosEvalState *es);


int koskari_eval_init(PyObject *mod);
int koskari_eval_clear(PyObject *mod);


#endif


// The end.
