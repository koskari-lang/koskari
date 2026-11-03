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
#include "exception.h"
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
  KOS_RES_REPROLOGUE,      /* restart prologue (PyErr set, or goal active) */
  KOS_RES_YIELD,           /* generator yielded val */
  KOS_RES_AWAIT,           /* suspend on awaitable val */
  KOS_RES_RETURN,          /* return val (needs walk+pop via eval_event_return) */
  KOS_RES_RAISE,           /* PyErr is set; run exception handler */
  KOS_RES_CONTROL_RETURNED,/* dyn-node control-returned hook */
  KOS_RES_DISPATCH,        /* resume instruction dispatch directly: live regs
                              valid, no PyErr, no goal, no resume.
                              Loop-internal only; must never escape
                              koskari_eval (post-op consumers normalize it) */
  KOS_RES_POLL,            /* as NORMAL, from a cooperation point that found
                              the eval breaker up: the handler's relay tails
                              into the breaker service before threading on.
                              Loop-internal like DISPATCH */
  KOS_RES_RESUME,          /* the live frame's resume takes it from here
                              with val (owned, or NULL for a plain
                              continuation, or NULL with the end's
                              StopIteration pending): the relay clears
                              the resume and tail-calls it. An install
                              that left a resume-carrying frame current
                              answers this, and so does a flattened
                              producer's yield or end delivered to its
                              consumer. Loop-internal like DISPATCH */
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
#define EVAL_RESUME(v)          ((KosOpResult){ KOS_RES_RESUME, (v) })
#define EVAL_CONTROL_RETURNED() ((KosOpResult){ KOS_RES_CONTROL_RETURNED, NULL })
#define EVAL_DISPATCH()         ((KosOpResult){ KOS_RES_DISPATCH, NULL })
#define EVAL_POLL()             ((KosOpResult){ KOS_RES_POLL, NULL })
#define EVAL_FATAL()            ((KosOpResult){ KOS_RES_FATAL, NULL })


#include "compat.h"
#include "dynchain.h"
#include "hints.h"
#include "tracing.h"


/* === KosEvaluatorToken === */


/**
  KosEvaluatorToken is a refcounted PyObject that identifies a koskari_eval
  domain/instance.  It tracks whether the evaluator is currently active on
  this thread (live_depth) and serves as an ownership marker for
  Continuations and DelimitedContinuations. A context mints its token on
  the first continuation capture (KosContext_EnsureToken); the depth is
  maintained only while a token exists.
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
} KosContextFlags;


/* === KosFrame: alias for KosDynFrame === */


/**
  KosFrame is an alias for KosDynFrame for backward compatibility.
  See dynchain.h for the actual structure definition.

  The frame represents a function activation in the dynamic chain.
  It holds the bytecode, the instruction records (pc = the next
  instruction, acting_pc = the instruction executing), operand stack,
  and namespace.
*/

typedef KosDynFrame KosFrame;


/**
  KosSuspensionEvent records why the evaluator last suspended.
  Set when koskari_eval suspends with AWAIT or YIELD; cleared when execution
  starts or the context is not suspended.
  Used at entry to choose the revive path (revive_await, revive_yield).
*/
typedef enum KosSuspensionEvent {
  KOS_SUSP_NONE   = 0,
  KOS_SUSP_AWAIT  = 1,
  KOS_SUSP_YIELD  = 2,
} KosSuspensionEvent;


/**
  KosEvalMode is the result of entry/revive and drives the mode-handler loop.
  NORMAL = run next insn or check completion; EXCEPTION = run process_raise.
*/
typedef enum KosEvalMode {
  KOS_MODE_NORMAL    = 0,
  KOS_MODE_EXCEPTION = 2,
} KosEvalMode;


/* KosThreadFn, the one prototype every threaded step shares (a CPS
   resume, an opcode handler), is defined in bytecode.h beside KosOp,
   the instruction record that carries a handler pointer. */


/**
   A per-frame CPS resume: the fn of the KosResumeKind (dynchain.h) a
   consumer sets on a frame via KosContext_Call /
   KosContext_NextResume to receive exactly one settled delivery -- a call's result, a callee frame's return, a
   Next request's element, or an awaited value, however it was
   produced.

   value is a new reference the resume owns, and is always settled: a
   delivery that still needs awaiting is awaited by the delivery point
   before the resume runs, however many times it takes. A resume never
   awaits, and never inspects a value to decide whether it must --
   that duplication across consumers is what these entries exist to
   remove.

   NULL with PyErr set means the delivery failed -- the source was
   exhausted, the source raised, the request failed, the callee raised
   -- reported the way a PyObject * API reports failure to its caller.
   A resume on the live frame runs from the trampoline against a
   settled chain, so it may do anything a resume handed a value may
   do. Thread on with
   RESULT_ENTRY or RESUME_HOP to carry the step further, complete the
   frame with RESULT_FRAME_RETURN, or return EVAL_RAISE to
   decline. Declining re-raises with the resume already cleared, so
   the unwind proceeds exactly as it would have with no resume set
   at all; a consumer that only propagates needs no code beyond that
   one line.

   The delivery reaches every frame with a resume the same way,
   bytecode or code-less: the raise walk answers RESUME when the live
   frame has a resume, at entry and after every pop or generator
   teardown that reveals one, and never calls a resume itself. A
   thread caller relays the answer to the resume by tail call, and the
   trampoline delivers one a plain caller returned through the
   prologue, so every resume takes its failure on a settled chain.

   Returns a KosOpResult naming the next signal to the loop: another
   CPS entry call, EVAL_RETURN, EVAL_RAISE, EVAL_AWAIT, or
   EVAL_REPROLOGUE. The prototype is KosThreadFn.
*/


/**
   The breaker service, a KosThreadFn the cooperation points tail into
   when the eval breaker is up: yields the GIL when another thread has
   asked for it and runs the pending calls, then threads on at ctx->pc,
   or answers REPROLOGUE when the poll raised so the prologue's
   pending-exception phase takes the signal. insn and val are unused.
*/
KOS_PRESERVE_NONE KosOpResult kos_eval_poll(
    KosContext *ctx,
    void *insn,
    PyObject *val);


/**
   The prologue, a KosThreadFn: the ordered re-entry protocol -- staged
   delivery, pending exception, goal drain, resume delivery, with the
   breaker polled ahead of every pass -- run until no step asks for
   another pass, then the instruction at ctx->pc fetched and tail-called.
   Every re-entry into the instruction stream (a raise walk that found
   its handler, a plan transfer, a continuation replay, a staged
   delivery) tails into this, and it returns only a boundary result.
   insn and val are unused.
*/
KOS_PRESERVE_NONE KosOpResult kos_eval_prologue(
    KosContext *ctx,
    void *insn,
    PyObject *val);


/**
   The raise walk, a KosThreadFn every RESULT_RAISE tails into: the
   exception set is offered up the chain (the live frame's resume at
   every level, try and finally extents, generator conversion) and the
   answer is RESUME into a resume, REPROLOGUE with the handler pc set,
   AWAIT for an async unwind, or the terminal RAISE or FATAL. insn and
   val are unused.
*/
KOS_PRESERVE_NONE KosOpResult kos_eval_raise(
    KosContext *ctx,
    void *insn,
    PyObject *val);


/**
   The delivery of RETURN, YIELD and AWAIT and the control-returned
   hook, each a KosThreadFn the handlers and the relay tail into with
   the owned value (NULL for the hook): the event runs in the chain,
   threads on when it can, and returns a boundary otherwise. insn is
   unused.
*/
KOS_PRESERVE_NONE KosOpResult kos_return_deliver(
    KosContext *ctx,
    void *insn,
    PyObject *val);
KOS_PRESERVE_NONE KosOpResult kos_yield_deliver(
    KosContext *ctx,
    void *insn,
    PyObject *val);
KOS_PRESERVE_NONE KosOpResult kos_eval_await(
    KosContext *ctx,
    void *insn,
    PyObject *val);
KOS_PRESERVE_NONE KosOpResult kos_eval_control_returned(
    KosContext *ctx,
    void *insn,
    PyObject *val);

/**
   The cold half of RESULT_RELAY: every helper answer but NORMAL, out
   of line once instead of a seven-arm ladder in each relaying handler.
   The kind travels in the void slot as an integer, since a tail call
   cannot hand over a pointer into the caller's frame, and the value in
   its own slot. Tails into the answer's service; FATAL returns.
*/
KOS_PRESERVE_NONE KosOpResult kos_eval_relay(
    KosContext *ctx,
    void *kind,
    PyObject *val);

/**
   The RESUME answer's service, a thread function of its own: the live
   frame's resume takes the delivery by tail call, the prologue's slim
   bounce without the pass. A site that knows it has a RESUME tails
   here directly; the relay's RESUME arm tails here for one that
   carries the kind in a variable. A delivery already staged for the
   frame is the value; an awaitable delivered to a kind that settles
   goes through the prologue, which settles it, while a plain value
   hops at once. A failed delivery arrives as NULL
   with the error pending and supersedes any staged delivery, in the
   slot or on a code-less frame's stack. insn is unused.
*/
KOS_PRESERVE_NONE KosOpResult kos_eval_resume_live(
    KosContext *ctx,
    void *insn,
    PyObject *val);


/* === Threaded control transfer === */


/**
   Dispatch is threaded: every opcode handler reaches the next by a
   guaranteed tail call, and control returns to the trampoline only on
   a signal. That needs a compiler that enforces the tail call.
*/
#if ! KOS_HAVE_MUSTTAIL
# error "koskari requires a compiler with musttail (clang, or GCC 15+)"
#endif

#define KOS_MUSTTAIL __attribute__((musttail))


/**
   TAILCALL(fn, ctx, arg, value): hand control to fn, a KosThreadFn, as
   the calling function's last act. The calling function must itself
   have the KosThreadFn prototype (musttail refuses anything else), and
   any Py_DECREF it owes runs before this, never after: its frame is
   discarded.
*/
#define TAILCALL(fn, ctx, arg, value) \
  KOS_MUSTTAIL return (fn)((ctx), (arg), (value))


/**
   DISPATCH(fn, ctx, arg, value): deliver value to fn, a KosResumeKind,
   by way of the prologue: fn and arg become the current frame's
   resume, value (an owned reference, or NULL for a cold start with
   nothing staged) goes into the context's pending slot, and the
   prologue is tailed into, which polls if due and then takes the slot
   and invokes fn. The frame stack is not touched. Caller must have a
   current frame, nothing may already be staged, and the calling
   function must have the KosThreadFn prototype. A resume handing a
   delivery to the next resume uses RESUME_HOP, which tail-calls the
   resume directly and falls back to this when the eval breaker is up.
*/
#define DISPATCH(kind, ctx, arg, value)                                 \
  do {                                                                  \
    KosContext *kos_dsp_ctx_ = (ctx);                                   \
    KosDynFrame *kos_dsp_frame_ = KosContext_FRAME(kos_dsp_ctx_);       \
    PyObject *kos_dsp_val_ = (value);                                   \
    KOS_DISPATCH_SAFETYNET(kos_dsp_ctx_, kos_dsp_val_);                 \
    KosContext_PENDING_VALUE(kos_dsp_ctx_) = kos_dsp_val_;              \
    KosDynFrame_RESUME_KIND(kos_dsp_frame_) = (kind);                   \
    KosDynFrame_RESUME_STATE(kos_dsp_frame_) = (void *) (arg);          \
    KOS_TRACE_TRAMPOLINE_STEP();                                        \
    TAILCALL(kos_eval_prologue, kos_dsp_ctx_, NULL, NULL);              \
  } while (0)

/* === Opcode result macros === */

/*
   The tails an opcode handler ends in. Each names what the handler
   did and where control goes next, so the same body reads correctly
   under both dispatch modes:

   - RESULT_NEXT(ctx): continue at the next instruction, insn + 1.
   - RESULT_JUMP_AT(ctx, target): continue at the target record.
   - RESULT_CONTINUE(ctx): continue at the record ctx->pc holds.
   - RESULT_PUSH(ctx, val): push a borrowed val, then continue. A
     failed push leaves val with its caller.
   - RESULT_STEAL(ctx, val): push an owned val (consumed, on failure
     too; NULL raises), then continue.
   - RESULT_RAISE(): PyErr is set; hand off to the raise machinery.
   - RESULT_FAIL(): hard fault.

   The straight-line forms (NEXT, PUSH, STEAL) step from the handler's
   own record: the next instruction is insn + 1, and its pc comes off
   that record, so no fetch through ctx->pc is needed. They are for a
   handler that leaves the frame and the pc alone. A handler whose
   helper installed a callee, returned to a parent, or stored a pc
   ends in RESULT_CONTINUE, which fetches from ctx->pc the way the
   trampoline does. A jump ends in RESULT_JUMP_AT with its resolved
   target record. RAISE and FAIL
   return: they are a real crossing back to the trampoline. Every
   continuing step is a tail call, so a handler using these macros
   must have the KosThreadFn prototype, must name its own record insn
   (KOS_OP_HANDLER does), and must run any Py_DECREF it owes before
   the macro.
*/

/* The live pc fields set from a handler's own record: acting_pc is
   the record, pc the one after it. A handler that hands ctx to anything
   beyond a stack push or pop syncs at its head, so every helper, the
   raise walk and the frame flush read a current pc; a handler that
   hands ctx to nothing else skips it, and its raises go through
   RESULT_RAISE_AT, which syncs first. */
#define KOS_SYNC_PC(ctx, insn)                                          \
  do {                                                                  \
    KosOp *kos_sp_ = (KosOp *) (insn);                                  \
    (ctx)->acting_pc = kos_sp_;                                         \
    (ctx)->pc = kos_sp_ + 1;                                            \
  } while (0)

/* Thread into next, a record of the live frame's code: a bare tail
   call, the pc fields left to the handler (KOS_SYNC_PC). The eval
   breaker is tested at the cooperation points (RESULT_NEXT_POLL, a
   frame install, RESUME_HOP, the prologue), not here. */
#define KOS_THREAD_AT(ctx, next)                                        \
  do {                                                                  \
    KosOp *kos_thr_next_ = (next);                                      \
    KOS_TRACE_THREAD_STEP(kos_thr_next_->op);                           \
    TAILCALL(kos_thr_next_->fn, (ctx), kos_thr_next_, NULL);            \
  } while (0)

/* Thread into the handler at ctx->pc: the fetch the trampoline does.
   The form for a tail after a frame change or a stored pc. */
#define KOS_THREAD_NEXT(ctx)  KOS_THREAD_AT((ctx), KosContext_PC(ctx))

#define RESULT_NEXT(ctx)      KOS_THREAD_AT((ctx), ((KosOp *) insn) + 1)
#define RESULT_CONTINUE(ctx)  KOS_THREAD_NEXT(ctx)

#define RESULT_JUMP_AT(ctx, target)  KOS_THREAD_AT((ctx), (target))

#define RESULT_PUSH(ctx, val)                                           \
  do {                                                                  \
    if (unlikely(KosContext_StackPushRef((ctx), (val)) < 0))            \
      RESULT_RAISE_AT((ctx), insn);                                     \
    RESULT_NEXT(ctx);                                                   \
  } while (0)

#define RESULT_STEAL(ctx, val)                                          \
  do {                                                                  \
    PyObject *kos_rs_val_ = (val);                                      \
    if (unlikely(! kos_rs_val_))                                        \
      RESULT_RAISE_AT((ctx), insn);                                     \
    if (unlikely(KosContext_StackPUSH((ctx), kos_rs_val_) < 0))         \
      RESULT_RAISE_AT((ctx), insn);                                     \
    RESULT_NEXT(ctx);                                                   \
  } while (0)

/* RESULT_RAISE(): the exception is set; tail into the raise walk. The
   calling function must have the KosThreadFn prototype and hold ctx. A
   helper without that prototype returns EVAL_RAISE() instead and its
   handler's RESULT_RELAY makes the same tail call. */
#define RESULT_RAISE()  TAILCALL(kos_eval_raise, ctx, NULL, NULL)

/* RESULT_RAISE_AT(ctx, insn): the raise from a handler that did not
   sync at its head; the pc fields are set from its record first. */
#define RESULT_RAISE_AT(ctx, insn)                                      \
  do {                                                                  \
    KOS_SYNC_PC((ctx), (insn));                                         \
    TAILCALL(kos_eval_raise, (ctx), NULL, NULL);                        \
  } while (0)
#define RESULT_FAIL()   return EVAL_FATAL()

/* RESULT_REPROLOGUE(ctx): the handler left prologue work behind (an
   active goal, a restored exception); tail into the prologue. The
   trampoline slot is stepped here as the loop stepped it on a raw
   REPROLOGUE, since the prologue itself does not step. */
#define RESULT_REPROLOGUE(ctx)                                          \
  do {                                                                  \
    KOS_TRACE_TRAMPOLINE_STEP();                                        \
    TAILCALL(kos_eval_prologue, (ctx), NULL, NULL);                     \
  } while (0)

/* The owned value leaves through the event's thread function; the
   calling function must have the KosThreadFn prototype. */
#define RESULT_AWAIT(ctx, val)   TAILCALL(kos_eval_await, (ctx), NULL, (val))
#define RESULT_RETURN(ctx, val)  TAILCALL(kos_return_deliver, (ctx), NULL, (val))

/**
   RESULT_FRAME_RETURN(ctx, value, cache): complete the live code-less
   frame with value, flagged for cache when cache is given
   (KosNativeFrame_ReturnToCache), and tail into the return delivery,
   so the value reaches its terminus through kos_return_deliver's
   arms -- the parent frame, a region's frame waiting on a resume, a
   resume-carrying parent -- rather than through the trampoline's
   general return event. The calling resume must have the KosThreadFn
   prototype. value is consumed.
*/
#define RESULT_FRAME_RETURN(ctx, value, cache)                          \
  RESULT_RETURN((ctx),                                                  \
                KosNativeFrame_ReturnToCache((ctx), (value), (cache)).val)

/**
   RESULT_RELAY(ctx, r): end a handler with the result of the helper
   that settled its operation -- the call core, an iteration step, an
   await, a dynamic node's transition. NORMAL means the helper left
   the next instruction at ctx->pc (a callee's first instruction after
   an install, the next instruction after a push), and continues
   there; RAISE, AWAIT, RETURN, YIELD and CONTROL_RETURNED tail into
   their event's thread function; DISPATCH threads; REPROLOGUE tails
   into the prologue; FATAL returns to the trampoline. The helper
   itself keeps its own
   prototype and return; the tail call happens here, in the handler.
   POLL is NORMAL from a
   cooperation point that found the breaker up (a frame install, the
   one cycle a tail-recursive loop closes without a CONTINUE): the
   relay tails into the breaker service, which threads on.
*/
#define RESULT_RELAY(ctx, r)                                            \
  do {                                                                  \
    KosOpResult kos_rr_ = (r);                                          \
    if (likely(kos_rr_.kind == KOS_RES_NORMAL))                         \
      KOS_THREAD_NEXT(ctx);                                             \
    TAILCALL(kos_eval_relay, (ctx),                                     \
             (void *) (uintptr_t) kos_rr_.kind, kos_rr_.val);           \
  } while (0)

/**
   RESULT_ENTRY(ctx, r, resume, state): end a resume with the outcome r
   of a CPS entry (KosContext_Call, KosContext_NextResume)
   it called with that same resume. NORMAL means a callee frame is
   installed and current with resume set on the caller: thread into
   its first instruction. RETURN means the settled value is in hand
   and no resume is set: tail-call resume with it now. Anything else
   (a suspension, a bounce the entry had to take, a failure) returns
   to the trampoline as it stands. The calling resume must have the
   KosThreadFn prototype and must run any Py_DECREF it owes before
   the macro.
*/
#define RESULT_ENTRY(ctx, r, resume, state)                             \
  do {                                                                  \
    KosOpResult kos_re_ = (r);                                          \
    if (likely(kos_re_.kind == KOS_RES_RETURN))                         \
      RESUME_HOP((resume), (ctx), (state), kos_re_.val);                \
    if (kos_re_.kind == KOS_RES_NORMAL)                                 \
      KOS_THREAD_NEXT(ctx);                                             \
    if (kos_re_.kind == KOS_RES_RESUME)                                 \
      TAILCALL(kos_eval_resume_live, (ctx), NULL, kos_re_.val);         \
    return kos_re_;                                                     \
  } while (0)


/**
   RESUME_HOP(kind, ctx, state, value): hand a delivery from one resume
   to the next, kind being the next resume's KosResumeKind -- the hop a
   step's phases make, or a bytecode return into a
   resume-carrying parent. A tail call that is also a cooperation
   point: when the eval breaker is up it stages through DISPATCH
   instead, so the trampoline polls before the prologue delivers. A
   chain of resumes with no instruction between them (a filter
   rejecting every element) therefore sees a signal or a GIL request
   as promptly as a loop does. The calling function must have the
   KosThreadFn prototype, as for TAILCALL.
*/
#define RESUME_HOP(kind, ctx, state, value)                             \
  do {                                                                  \
    if (KOS_EVAL_BREAKER_UP(ctx))                                       \
      DISPATCH((kind), (ctx), (state), (value));                        \
    KOS_TRACE_RESUME_STEP();                                            \
    TAILCALL((kind)->fn, (ctx), (state), (value));                      \
  } while (0)


/**
   RESULT_NEXT_POLL(ctx): thread on to the instruction at ctx->pc, unless
   the eval breaker is up, in which case tail into the breaker service,
   which polls and threads on to that same step. The cooperation point
   for an opcode that closes a cycle on its own (CONTINUE, the
   compiler's one back-edge).
*/
#define RESULT_NEXT_POLL(ctx)                                           \
  do {                                                                  \
    if (KOS_EVAL_BREAKER_UP(ctx))                                       \
      TAILCALL(kos_eval_poll, (ctx), NULL, NULL);                       \
    KOS_THREAD_NEXT(ctx);                                               \
  } while (0)

/* At most one delivery is staged at a time; a second is an internal
   sequencing bug, reported under the safety net instead of leaking. */
#if KOS_EVAL_SAFETYNET
# define KOS_DISPATCH_SAFETYNET(ctx, val)                               \
  do {                                                                  \
    if (unlikely(KosContext_PENDING_VALUE(ctx))) {                      \
      Py_XDECREF(val);                                                  \
      PyErr_SetString(PyExc_SystemError,                                \
                      "CPS delivery staged over a live one");           \
      return EVAL_RAISE();                                              \
    }                                                                   \
  } while (0)
#else
# define KOS_DISPATCH_SAFETYNET(ctx, val)  ((void) 0)
#endif


/**
   Resume a return stored on a node whose leave went asynchronous,
   now that the node has left the chain: re-enter the return path the
   obligation names with its value. ob is a KOS_OB_RETURN obligation
   handed back by kos_dyn_settle_top; it is consumed. The answer has
   been through the raise ingress or the await tag as a walk's would.
*/
KosOpResult KosEval_ResumeReturn(KosContext *ctx, KosObligation *ob);


/**
   Complete a code-less frame by handing value to the eval loop's
   return machinery, which owns every delivery terminus (parent frame,
   step resume, drive root). When cache is given, sets the frame to
   be stored into that caller-owned one-frame cache when the return
   path frees it (kos_dynframe_cache_inert); a NULL or occupied cache
   frees as usual. A resume declining a failure caches the same way
   through KosNativeFrame_CacheOnFree before it raises, since the raise
   walk frees the popped frame through the same release. The result is
   always RETURN carrying value; a resume
   ends with RESULT_FRAME_RETURN, which tails that value into the
   return delivery. value is consumed.
*/
KosOpResult KosNativeFrame_ReturnToCache(
    KosContext *ctx,
    PyObject *value,
    KosDynFrame **cache);

/**
   The flagging half of KosNativeFrame_ReturnToCache: set the live
   code-less frame to be stored into cache when the path that pops it
   frees it; a NULL cache clears the slot.
*/
void KosNativeFrame_CacheOnFree(KosContext *ctx, KosDynFrame **cache);


/**
   Push a code-less frame onto the chain for CPS resume use. When cache
   holds a cached frame it is reinstalled in place of a fresh
   allocation and *cache is emptied. The frame carries kind and state
   from the install on, so a teardown at any point after it hands the
   kind's clear that state; the caller's later KosContext_Call
   / NextResume call sets the resume that receives the delivery.
   Returns the frame, or NULL with PyErr set.
*/
KosDynFrame *KosResumeFrame_Install(
    KosContext *ctx,
    const KosResumeKind *kind,
    void *state,
    KosDynFrame **cache);


/**
   Fetch the next element of iter from inside a code-less frame, with
   no node, for a caller that interprets the KosIterRequestStatus
   itself (see the enum): iter is bound into a request on the way in
   (kos_iter_request_bind_next) and fetched by its kind. The caller's own record
   holds iter and finishes it on delivery.
*/
KosIterRequestStatus KosNativeFrame_RequestNext(
    KosContext *ctx,
    PyObject *iter,
    PyObject **out);


/**
   Request the next element of the bound iterator req describes,
   delivering it (or the failure) to resume instead of making the
   caller interpret KosIterRequestStatus by hand. The fetch is keyed
   by req->kind, so a site that bound its iterator once never
   classifies it again. Like KosContext_Call, the entry reports what
   happened and the calling resume ends in RESULT_ENTRY: a delivered
   element that is settled reports RETURN with it, no resume set (the
   resume is tail-called with it); one that needs settling settles one
   step, sets resume, and reports REPROLOGUE or AWAIT; an in-flight
   CONTINUES sets resume and reports what kos_iter_installed answers:
   NORMAL when the install left a bytecode frame current that can be
   threaded into, RESUME when that frame halts on a resume of its own
   (a yield-from in flight), REPROLOGUE otherwise; an
   awaitable in-flight (EXIT) sets resume and reports EVAL_AWAIT, and
   the AWAITED revive delivers into it. Exhaustion sets resume and
   raises StopIteration, which reaches resume as a RESUME answer, NULL
   with StopIteration pending, that RESULT_ENTRY relays; a request
   that fails before it defers raises at the caller with no resume
   set, since nothing is in flight for the resume to finish. Caller
   must have a current frame, and resume is required: every outcome
   but a settled element in hand and a synchronous failure reaches
   the caller through it.
*/
KosOpResult KosContext_NextResumeBound(
    KosContext *ctx,
    const KosIterRequest *req,
    const KosResumeKind *resume,
    void *state);


/**
   KosContext_NextResumeBound for an iterator with no record: iter is
   bound into one on the way in (kos_iter_request_bind_next).
*/
KosOpResult KosContext_NextResume(
    KosContext *ctx,
    PyObject *iter,
    const KosResumeKind *resume,
    void *state);


/* === KosContext: VM execution context === */

/**
  KosContext is the top-level execution state for the Koskari VM.

  The dyn_top field points to the top of the dynamic chain, which may
  be any node kind (FRAME, GEN, PROMPT, WIND, TRY, and the rest).

  The frame field is the topmost FRAME node; block, namespace, pc and
  acting_pc are the live registers derived from it and from the
  chain; the frame's code is read from the frame. kos_dyn_* chain edits retarget them, during a drive and
  outside one alike, so they are current whether the context is
  running or suspended. pc and acting_pc are flushed into the frame at
  frame boundaries (KosContext_FlushLiveRegsToFrame). Both are
  records of the frame's code, which the frame keeps alive, and NULL
  for a code-less frame; code never relocates, so a record pointer
  stays valid for the life of its code.

  The goal field is the active transfer goal, or NULL; waiting outer
  goals hang off it.

  Ownership and lifecycle:
  - A context is a Python object (koskari.lib.Context). KosContext_New
    returns a new reference; every holder (coroutine, async generator,
    map, filter, iteration adapter) keeps its own reference and drops
    it with Py_CLEAR or Py_DECREF, so the last holder deallocates.
  - Deallocation is memory teardown only: the chain is freed without
    running the extent protocol. A holder that owes a close drive runs
    it before dropping its reference.
  - koskari_eval holds a reference for the length of a drive, so a
    running context outlives every holder that lets go mid-drive.
  - The context owns its dynamic chain and associated state.

  Suspension and resumption:
  - When koskari_eval returns KOS_RES_AWAIT, the context is suspended.
  - py_await_iter holds in-flight Python await machinery
    (iterator from __await__(), defer payload). Frozen GEN resume uses
    KosPyAsyncGen::gen plus kos_dyn_gen_install at the driver before
    re-entering koskari_eval.
  - Resumption injects the result back into the VM via the stack.

  Handlers and helpers take the context itself; a drive begins with
  KosContext_EvalEnter, which sets breaker, recursion_limit and
  terminal for that drive from the thread state it runs on.
*/


/**
   A call for koskari_eval to install at entry on a fresh context
   (KosContext::entry_call): KosContext_Call from a code-less root
   frame carrying resume and frame_flags, or, for a simple call of a
   non-yielding KosFunction, the function's frame as the root frame
   carrying frame_flags. args and kwnames are borrowed for the drive.
*/
typedef struct KosEntryCall {
  PyObject *callable;
  PyObject *const *args;
  Py_ssize_t nargs;
  PyObject *kwnames;
  const KosResumeKind *resume;
  uint32_t frame_flags;
  KosDynFrame **frame_cache;  /* the wrapper's inert root-frame slot:
                                 the code-less root frame is taken from
                                 it and returned to it, and it is the
                                 resume's state; NULL when there is no
                                 wrapper */
} KosEntryCall;


struct KosContext {
  PyObject_HEAD

  /* --- The live registers, grouped at the head. pc and acting_pc are
     flushed into frame at frame boundaries. --- */
  KosDynNode *dyn_top;       /* top of dynamic chain */
  KosDynFrame *frame;        /* topmost frame on the chain */
  KosSegment **block;        /* innermost operand stack slot; non-owning */
  PyObject *namespace;       /* active namespace; borrowed */
  KosOp *pc;                 /* the next instruction */
  KosOp *acting_pc;          /* the instruction executing */

  /* The active transfer goal, or NULL; waiting outer goals hang off it */
  KosXferGoal *goal;

  PyObject *pending_value; /* a staged CPS delivery (owned) awaiting the
                              current frame's resume; NULL when none.
                              Live transit only: a drive's exit moves it
                              onto the frame stack, and the delivery
                              step takes it before the resume runs */
  const volatile KosBreakerWord *breaker; /* this thread's eval breaker
                         (compat.h), located from the thread state by
                         KosContext_EvalEnter; the cooperation points
                         test it and the trampoline polls when it is
                         up */
  uint16_t recursion_limit; /* the deepest frame this context may
                         install: sys.getrecursionlimit(), read from
                         the thread state by KosContext_EvalEnter and
                         clamped to KOS_FRAME_DEPTH_MAX */
  uint8_t terminal;         /* the result in flight is a boundary that
                         has been through its event: a raise walked to
                         no handler, a suspension recorded for AWAIT or
                         YIELD, a root return. The trampoline and the
                         resume delivery leave with it instead of
                         applying it again */

  /* Last eval boundary reason for revive */
  uint8_t suspension_event;  /* KosSuspensionEvent */

  uint32_t flags;            /* KosContextFlags */

  KosEvaluatorToken *token; /* ownership marker for continuations; NULL
                               until the first capture mints it */
  PyObject *domain;         /* KosDomain while an eval runs the context:
                               the domain current on the thread at the
                               eval entry (the active context's, else
                               the innermost open Domain block's, else
                               the thread default), cleared at exit.
                               NULL between evals: nothing is carried
                               across. Live parameter state is in
                               param_refs. */
  PyObject *param_refs;    /* PyDict: name -> cell (KosRef); this context's
                               parameter cell table, forked at creation by
                               sharing the creator's table; a writer holding
                               a shared table copies it first
                               (kos_context_param_refs). Lazy; NULL when
                               empty. */

  /* --- Python bridge: await/yield suspend and resume --- */
  PyObject *py_resume_inject;  /* value to push on next koskari_eval when resuming */
  PyObject *py_await_iter;     /* iterator from __await__() when suspended on AWAIT */

  /* --- Entry call (set by a Python-boundary entry on a fresh context):
     koskari_eval installs it at entry, then clears it. Borrowed; the
     setter owns the record for the drive. --- */
  const struct KosEntryCall *entry_call;

  /* --- Handler-visible exception state (sys.exc_info) saved across
     suspensions: swapped with the thread state at koskari_eval
     entry/exit so interleaved contexts on one thread cannot corrupt
     each other's view. Context-isolated: a fresh context starts with
     empty handler state (no fall-through to the host's). --- */
  KosExcState exc_info;
};


extern PyTypeObject KosContextType;

#define KosContext_Check(o) PyObject_TypeCheck((o), &KosContextType)


/* === KosContext accessor macros === */

#define KosContext_DYN_TOP(c)                (((KosContext *)(c))->dyn_top)
#define KosContext_FRAME(c)                  (((KosContext *)(c))->frame)
#define KosContext_BLOCK(c)                  (((KosContext *)(c))->block)
#define KosContext_NAMESPACE(c)              (((KosContext *)(c))->namespace)
#define KosContext_TOKEN(c)                  (((KosContext *)(c))->token)
#define KosContext_DOMAIN(c)                 (((KosContext *)(c))->domain)
#define KosContext_PARAM_REFS(c)            (((KosContext *)(c))->param_refs)
#define KosContext_GOAL(c)                   (((KosContext *)(c))->goal)
#define KosContext_PY_AWAIT_ITER(c)          (((KosContext *)(c))->py_await_iter)
#define KosContext_PY_RESUME_INJECT(c)       (((KosContext *)(c))->py_resume_inject)
#define KosContext_ENTRY_CALL(c)             (((KosContext *)(c))->entry_call)
#define KosContext_EXC_INFO(c)               (&((KosContext *)(c))->exc_info)

#define KosContext_SUSPENSION_EVENT(c)      (((KosContext *)(c))->suspension_event)

#define KosContext_FLAGS(c)                  (((KosContext *)(c))->flags)


/* === Live register accessors === */

/**
  The KosContext_* names below read the live registers at the head of
  KosContext. With KOS_TRACE_EVAL_SYNC, call counts and total wall time
  of the register sync steps are recorded for kos_trace_report /
  get_trace_report under eval_state_sync.
*/


#define KosContext_BREAKER(ctx) ((ctx)->breaker)

/* The breaker bits the evaluator services. From 3.13 the word is per
   thread and the bits are named: another thread wants the GIL, a
   signal is pending, or the interpreter has scheduled a collection,
   which from 3.12 runs only from its own loop or from
   PyErr_CheckSignals, so a Koskari stretch has to answer it. The other
   bits (pending calls, async exception, stop requests) are Python's
   loop's. Before 3.13 the word is one aggregate the interpreter
   recomputes, tested whole. */
#if defined(KOS_BREAKER_WORD)
# define KOS_BREAKER_MASK                                               \
  ((KosBreakerWord) (KOS_BREAKER_GIL_DROP | KOS_BREAKER_SIGNALS          \
                     | KOS_BREAKER_GC_SCHEDULED))
#else
# define KOS_BREAKER_MASK ((KosBreakerWord) ~0)
#endif

/* The eval breaker test at a cooperation point: one load, one branch.
   Up means a bit of KOS_BREAKER_MASK is set, and the trampoline's poll
   section (kos_eval_poll) answers it before threading resumes. */
#define KOS_EVAL_BREAKER_UP(ctx)                                        \
  unlikely((*KosContext_BREAKER(ctx) & KOS_BREAKER_MASK) != 0)


/**
   True when a just-installed frame can be threaded into at its pc: it
   has code and is not waiting on a resume. A frame suspended inside a
   yield-from carries the delegation's resume, and its delivery must
   run first, so the installer hands the frame to that resume
   (kos_iter_installed) or answers REPROLOGUE (a call install) instead.
*/
static inline int kos_frame_threads(KosDynFrame *nf) {
  return nf && KosDynFrame_CODE(nf) && ! KosDynFrame_RESUME_KIND(nf);
}


/**
   The result a call-core install arm returns once the callee frame is
   live at pc 0: thread on, or POLL when the eval breaker is up so the
   handler's RESULT_RELAY tails into the breaker service before the
   callee's first instruction. Frame entry is a cooperation point
   because a tail-recursive loop has no CONTINUE.
*/
static inline KosOpResult kos_call_installed(KosContext *ctx) {
  return KOS_EVAL_BREAKER_UP(ctx) ? EVAL_POLL() : EVAL_NORMAL();
}


#define KosContext_RECURSION_LIMIT(ctx) ((ctx)->recursion_limit)

/**
   Raise RecursionError; the cold half of kos_eval_check_frame_room.
   Always returns -1.
*/
int kos_eval_recursion_error(void);

/**
   Test whether count more frames fit above base (a frame on the live
   chain, or NULL for the chain root) under the recursion limit. 0 when
   they fit; -1 with RecursionError set when they do not. Every site
   that installs or splices frames asks before touching the chain, so a
   refused install leaves the chain and the caller's state unchanged and
   the exception is raised into the caller's frame like any other.
*/
static inline int kos_eval_check_frame_room(
    KosContext *ctx,
    KosDynFrame *base,
    unsigned count) {

  unsigned depth = base ? KosDynFrame_DEPTH(base) : 0u;

  if (likely(depth + count <= KosContext_RECURSION_LIMIT(ctx)))
    return 0;
  return kos_eval_recursion_error();
}
/** The live frame's code: NULL with no frame or a code-less one. */
static inline KosCode *KosContext_FrameCode(KosContext *ctx) {
  KosDynFrame *frame = KosContext_FRAME(ctx);

  return frame ? KosDynFrame_CODE(frame) : NULL;
}

/** The live next instruction; use KosContext_SET_PC to assign. */
#define KosContext_PC(ctx) ((ctx)->pc)
/** Instruction currently executing (last fetch). */
#define KosContext_ACTING_PC(ctx) ((ctx)->acting_pc)
#define KosContext_SET_PC(ctx, v) ((void)((ctx)->pc = (v)))


/** Aim the live registers at the first instruction of code, where a
    frame entered or re-entered by a tail call starts; both NULL for a
    code-less frame. */
static inline void KosContext_SetEntryPC(KosContext *ctx, KosCode *code) {
  KosOp *entry = kos_code_entry(code);

  ctx->pc = entry;
  ctx->acting_pc = entry;
}
/** The staged CPS delivery, owned; NULL when nothing is staged. */
#define KosContext_PENDING_VALUE(ctx) ((ctx)->pending_value)


/** Stage an owned delivery for the current frame's resume, releasing
    any earlier one still in the slot. */
static inline void KosContext_StageDelivery(KosContext *ctx, PyObject *val) {
  Py_XDECREF(KosContext_PENDING_VALUE(ctx));
  KosContext_PENDING_VALUE(ctx) = val;
}


/** Empty the live registers derived from the chain. The caller has
    freed the chain or moved it elsewhere. */
static inline void KosContext_ClearLiveRegs(KosContext *ctx) {
  KosContext_DYN_TOP(ctx) = NULL;
  KosContext_FRAME(ctx) = NULL;
  KosContext_BLOCK(ctx) = NULL;
  KosContext_NAMESPACE(ctx) = NULL;
  ctx->pc = NULL;
  ctx->acting_pc = NULL;
}


/** Make frame the live frame: block and ns are the chain-derived
    registers the caller resolved (the frame's own segment and
    namespace, or a region's above it); the pc fields come from the
    frame. dyn_top is the caller's to set. */
static inline void KosContext_LoadFrame(
    KosContext *ctx,
    KosDynFrame *frame,
    KosSegment **block,
    PyObject *ns) {

  KosContext_FRAME(ctx) = frame;
  KosContext_BLOCK(ctx) = block;
  KosContext_NAMESPACE(ctx) = ns;
  ctx->pc = KosDynFrame_PC(frame);
  ctx->acting_pc = KosDynFrame_ACTING_PC(frame);
}


/**
   The result of an iteration install: thread into the installed frame
   when it can be threaded into; hand a bytecode frame halted on a
   resume (a generator suspended inside a yield-from) to that resume by
   tail call, its staged delivery or a plain next; else bounce through
   the prologue, which runs a code-less frame's entry sequence, whose
   delivery rides the frame's own stack.
*/
static inline KosOpResult kos_iter_installed(KosContext *ctx) {
  KosDynFrame *frame = KosContext_FRAME(ctx);

  if (kos_frame_threads(frame))
    return EVAL_NORMAL();
  if (frame && KosDynFrame_CODE(frame) && KosDynFrame_RESUME_KIND(frame))
    return EVAL_RESUME(NULL);
  return EVAL_REPROLOGUE();
}


/**
   KOS_ITER_INSTALLED_THREAD(ctx): kos_iter_installed as the tails a
   thread function makes once an iteration install has left its frame
   live: into the frame's next instruction, into the live frame's
   resume (kos_eval_resume_live) for a frame halted on one, into the
   prologue otherwise. The calling function must have the KosThreadFn
   prototype.
*/
#define KOS_ITER_INSTALLED_THREAD(ctx)                                  \
  do {                                                                  \
    KosDynFrame *kos_iit_frame_ = KosContext_FRAME(ctx);                \
    if (kos_frame_threads(kos_iit_frame_))                              \
      KOS_THREAD_NEXT(ctx);                                             \
    if (kos_iit_frame_ && KosDynFrame_CODE(kos_iit_frame_)              \
        && KosDynFrame_RESUME_KIND(kos_iit_frame_))                     \
      TAILCALL(kos_eval_resume_live, (ctx), NULL, NULL);                \
    RESULT_REPROLOGUE(ctx);                                             \
  } while (0)


/**
  Operand stack head block for logging; NULL when no stack.
*/
#define KosContext_StackHead(ctx) \
  ((KosContext_BLOCK(ctx) && *(KosContext_BLOCK(ctx))) \
   ? (*(KosContext_BLOCK(ctx))) \
   : (KosSegment *) NULL)


/* The current namespace, GC-revealed (tracked along its parent chain)
   and INCREF'd, ready for storage. See KosNamespace_Reveal. */
#define KosContext_RevealNamespace(ctx) \
  KosNamespace_Reveal(KosContext_NAMESPACE(ctx))

#define KosContext_StackPushRef(ctx, val) \
  kos_stack_push_ref_fast(KosContext_BLOCK(ctx), (val))

#define KosContext_StackPUSH(ctx, val) \
  kos_stack_push_steal_fast(KosContext_BLOCK(ctx), (val))

#define KosContext_StackPopRef(ctx) \
  kos_stack_pop_ref_fast(KosContext_BLOCK(ctx))

#define KosContext_StackPopTail(ctx, count) \
  KosStack_PopTail(KosContext_BLOCK(ctx), (count))

#define KosContext_StackTailPtr(ctx, count) \
  kos_stack_tail_ptr_fast(*KosContext_BLOCK(ctx), (count))

#define KosContext_StackLen(ctx) \
  KosSegment_LEN(*KosContext_BLOCK(ctx))

#define KosContext_StackDrop(ctx, count) \
  kos_stack_drop_fast(KosContext_BLOCK(ctx), (count))

#define KosContext_StackDrop1(ctx) \
  kos_stack_drop1_fast(KosContext_BLOCK(ctx))

#define KosContext_StackDropNoErr(ctx, count) \
  kos_stack_drop_noerr_fast(KosContext_BLOCK(ctx), (count))

#define KosContext_StackClear(ctx) \
  KosStack_Clear(KosContext_BLOCK(ctx))

#define KosContext_StackDuplicate(ctx, count) \
  KosStack_Duplicate(KosContext_BLOCK(ctx), (count))

#define KosContext_StackDup1(ctx) \
  kos_stack_dup1_fast(KosContext_BLOCK(ctx))

#define KosContext_StackPopTailDict(ctx, count) \
  KosStack_PopTailDict(KosContext_BLOCK(ctx), (count))

#define KosContext_StackPopTailDictFromNames(ctx, names) \
  KosStack_PopTailDictFromNames(KosContext_BLOCK(ctx), (names))

#define KosContext_StackPeek(ctx) \
  kos_stack_peek_fast(*KosContext_BLOCK(ctx))

#define KosContext_StackPeekUnder(ctx, under) \
  kos_stack_peek_under_fast(*KosContext_BLOCK(ctx), (under))

#define KosContext_StackSetUnder(ctx, under, val) \
  kos_stack_set_under_fast(*KosContext_BLOCK(ctx), (under), (val))

#define KosContext_StackPopTailPtr(ctx, count, dest) \
  kos_stack_pop_tail_ptr_fast(KosContext_BLOCK(ctx), (count), (dest))

#define KosContext_SetSuspensionEvent(ctx, ev) \
  ((void)(KosContext_SUSPENSION_EVENT(ctx) = (ev)))

#define KosContext_ClearSuspensionEvent(ctx) \
  ((void)(KosContext_SUSPENSION_EVENT(ctx) \
          = KOS_SUSP_NONE))

#define KosContext_SuspensionEvent(ctx) \
  KosContext_SUSPENSION_EVENT(ctx)

#define KosContext_MarkSuspendedNotRunning(ctx) \
  do { \
    KosContext *kos_ctx_mark_sr_ = (ctx); \
    KosContext_FLAGS(kos_ctx_mark_sr_) &= ~KOS_CTX_RUNNING; \
  } while (0)

#define KosContext_MarkDeadAfterYield(ctx) \
  do { \
    KosContext *kos_ctx_mark_dy_ = (ctx); \
    KosContext_FLAGS(kos_ctx_mark_dy_) &= ~KOS_CTX_RUNNING; \
    KosContext_FLAGS(kos_ctx_mark_dy_) |= KOS_CTX_DEAD; \
  } while (0)

#define KosContext_MarkDeadAfterReturn(ctx) \
  do { \
    KosContext *kos_ctx_mark_dr_ = (ctx); \
    KosContext_FLAGS(kos_ctx_mark_dr_) &= ~KOS_CTX_RUNNING; \
    KosContext_FLAGS(kos_ctx_mark_dr_) |= KOS_CTX_DEAD; \
  } while (0)

#define kos_frame_new(code, ns, stack, parent) \
  kos_dynframe_new((parent), (code), (ns), (stack))

#define kos_frame_clone(frame) kos_dynframe_clone(frame)

#define kos_frame_free(frame) kos_dynframe_free(frame)


/**
   Begin a drive on ctx: set breaker, recursion_limit and terminal for
   it. tstate is the calling thread's state, passed by an eval that
   holds it; NULL resolves it with PyThreadState_Get. The breaker is
   the eval breaker word located from tstate, and the recursion limit
   is the one tstate reads. Also run by a site that edits a suspended
   context's chain outside koskari_eval, since the chain edits read the
   breaker and the recursion limit.
*/
void KosContext_EvalEnter(KosContext *ctx, PyThreadState *tstate);


/**
   End a drive on ctx: flush the live pc and acting_pc into the frame,
   and move a staged delivery onto the stack of the code-less frame it
   was staged for, where the delivery step finds it on the next drive.
   Also run by a site that edited a suspended context's chain outside
   koskari_eval after KosContext_EvalEnter.
*/
void KosContext_EvalExit(KosContext *ctx);


/**
  Flush live pc and acting_pc from ctx onto the current heap frame before
  the chain or current-frame pointer changes, or before another subsystem
  reads frame PC slots. No-op when ctx has no frame.
*/
static inline void KosContext_FlushLiveRegsToFrame(KosContext *ctx) {

  KosDynFrame *f;

  KOS_TRACE_EVAL_SYNC_BEGIN();

  f = KosContext_FRAME(ctx);
  if (! f)
    goto done;
  KosDynFrame_PC(f) = (ctx)->pc;
  KosDynFrame_ACTING_PC(f) = (ctx)->acting_pc;

done:
  KOS_TRACE_EVAL_SYNC_END(KOS_TEVS_flush_live_regs);
}


/**
   Push a prepared frame from the per-function cache as dyn_top and
   the live frame.  Code, namespace, and cleared stack are already attached
   and the node base was normalized when it was cached
   (KosFunction_CacheFrame); this is the install half of
   kos_dyn_push_frame with the node id reset, and cannot fail.
   The caller has asked kos_eval_check_frame_room for one frame before
   binding into the cached namespace.
*/
KOS_OP_INLINE void kos_dyn_push_ready_frame(
    KosContext *ctx,
    KosDynFrame *frame) {

  KosDynNode_PARENT(frame) = KosContext_DYN_TOP(ctx);
  kos_dynframe_note_parent(frame);
  KosDynNode_NODE_ID(frame) = 0;  /* a fresh instance; minted on first read */

  {
    KosDynFrame *cur = KosContext_FRAME(ctx);
    if (cur) {
      KosDynFrame_FLAGS(frame) |= (KosDynFrame_FLAGS(cur) & KOS_FRAME_DEMAND);
      KosDynFrame_DEPTH(frame) = KosDynFrame_DEPTH(cur) + 1;
      KosContext_FlushLiveRegsToFrame(ctx);
    } else {
      KosDynFrame_DEPTH(frame) = 1;
    }
  }

  /* the installed segment's unwind cache: the live slot it displaces */
  KosSegment_BACK(KosDynNode_STACK((KosDynNode *) frame)) =
      KosContext_BLOCK(ctx);
  if (KosSegment_IS_INLINE(KosDynNode_STACK((KosDynNode *) frame)))
    KOS_TRACE_EVENTS_INC(KOS_TE_SEG_INLINE_INSTALL);
  KosDynFrame_BACK_FRAME(frame) = (KosDynNode *) KosContext_FRAME(ctx);
  KosContext_DYN_TOP(ctx) = (KosDynNode *) frame;
  /* a cached frame holds its entry (or NULL, code-less) as its pc */
  KosContext_LoadFrame(ctx, frame, &KosDynNode_STACK((KosDynNode *) frame),
                       KosDynNode_NS((KosDynNode *) frame));
}


int KosContext_SetFrameCodeAndNamespace(
    KosContext *ctx,
    KosDynFrame *frame,
    KosCode *code,
    PyObject *ns);

KosEvaluatorToken *KosContext_Token(const KosContext *ctx);

void KosContext_PythonAwaitClearIter(KosContext *ctx);
PyObject *KosContext_PythonAwaitIter(KosContext *ctx);
void KosContext_PythonResumeSet(KosContext *ctx, PyObject *val);

/**
   Pop TOS and materialize it as a fast sequence for the UNPACK family.
   need is the required length; exact demands len == need, otherwise
   len >= need. Returns a new reference, or NULL with TypeError (subject
   not iterable) or ValueError (wrong length) set. who names the opcode
   in the message.
*/
PyObject *KosContext_PopUnpackSequence(
    KosContext *ctx,
    Py_ssize_t need,
    int exact,
    const char *who);


/* === Context lifecycle === */


/**
  Create a new execution context with the given initial frame.

  :param top: initial frame (ownership transferred to context)
  :param s: the calling thread's record (kos_tls_current), which
      supplies the snapshot the context starts from

  :return: new reference to the context, or NULL on error
*/
KosContext *KosContext_New(KosDynFrame *top, KosTlsState *s);
PyObject *KosContext_Snapshot(KosTlsState *s);
KosContext *KosContext_NewFromSnapshot(KosDynFrame *top, PyObject *cells);
KosEvaluatorToken *KosContext_EnsureToken(KosContext *ctx);


/*
  A context deallocates with its last reference. Deallocation frees
  the dynamic chain, walking every node and running kind-specific free
  logic (including Py_DECREF on Python objects held by dyn nodes), which
  can invoke arbitrary Python code via destructors, and frees the active
  transfer goal and its outers and the await, resume, inject, domain,
  and parameter state. It does not start koskari_eval or execute
  bytecode: a holder that owes the extent protocol a close drive runs
  it before releasing.
*/


/**
   Make a context reusable after a completion at its root: drop the
   finished frame chain with the frozen block and namespace derived
   from it, and clear the completion residue -- the evaluator token
   (the next capture mints a fresh one), dead and running flags,
   suspension tag, in-flight await iterator, resume inject, an
   unconsumed entry call, a staged delivery, and the handler-visible
   exception state. The parameter cells stay. A context holding state
   this reset cannot account for (an active transfer goal) is left
   untouched and the caller releases it instead.

   :return: 0 recycled; -1 the context cannot be recycled
*/
int KosContext_Recycle(KosContext *ctx);


/**
   Free the context's chain, detaching first: the live registers are
   cleared before any node is freed, so Python code a node's free runs
   (a __del__, a frame's cache reset, a delegate's close) that reaches
   the context, a collection traversing it included, finds no register
   naming freed memory. Every release of a chain through its context
   goes through here.
*/
void KosContext_ReleaseChain(KosContext *ctx);


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
   closing the in-flight iterator. A missing close method is ignored;
   an error the close raises has no frame to propagate into and is
   reported through sys.unraisablehook. Any pending exception is
   preserved across the close.

   :param iter: borrowed; NULL is a no-op
*/
void kos_iter_close(PyObject *iter);


/**
   Close the chain on ctx with GeneratorExit when there is no caller to
   raise into: the awaitable a mid-await chain holds is closed first,
   then the throw runs at the suspension point to an outcome. No error
   is left set; one the close owes is held in the out-params instead,
   so the caller frees the chain before it reports or raises it.

   :param ctx: the context holding the suspended chain
   :param typ: receives the held error's type, or NULL
   :param val: receives the held error's value, or NULL
   :param tb: receives the held error's traceback, or NULL
   :return: 0 when the chain wound down, with an error held only when
     the cleanup raised something other than GeneratorExit,
     StopIteration or StopAsyncIteration; 1 when the chain was left in
     place, with RuntimeError("async generator ignored GeneratorExit")
     held when the body tried to suspend, or the error that kept the
     throw from being made
*/
int kos_close_drive(
    KosContext *ctx,
    PyObject **typ,
    PyObject **val,
    PyObject **tb);


/**
   Report a close error held by kos_close_drive through
   sys.unraisablehook against obj, the object whose close owed it.
   No-op when nothing is held.

   :param obj: borrowed
   :param typ: stolen
   :param val: stolen
   :param tb: stolen
*/
void kos_close_report(
    PyObject *obj,
    PyObject *typ,
    PyObject *val,
    PyObject *tb);


/**
  Execute the VM until a boundary event occurs.

  This is the main entry point for driving execution. The function
  runs opcodes from the live frame until:
  - RETURN: normal completion (KOS_RES_RETURN, val is new ref)
  - RAISE: exception propagation (KOS_RES_RAISE, PyErr set)
  - YIELD: generator yield (KOS_RES_YIELD, val is new ref)
  - AWAIT: Python awaitable encountered (KOS_RES_AWAIT, val is new ref)

  The caller owns the reference in the returned KosOpResult.val.

  :param ctx: the execution context
  :param s: the calling thread's record, when the caller already resolved
      it with kos_tls_current; NULL to resolve it here
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
    KosTlsState *s,
    PyObject *send_val,
    PyObject *throw_typ,
    PyObject *throw_exc);


/**
   Route the raised exception to the current frame's resume when its
   kind takes a throw: the frame's kind becomes the throw kind and the
   exception object is staged as its delivery, so a delegation in
   flight throws it into the delegate and a delegate that catches it
   answers with its next value, as CPython's yield from does.
   GeneratorExit is not routed: it raises at the frame, the raise
   offers it to the same resume as a NULL with the exception pending,
   and the yf_drive_* kind closes the delegate.

   Callers: revive_yield, the flattened generator throw ingress.

   :return: 1 when the exception was staged for the throw kind, 0 when
       it stays raised for the frame
*/
int KosContext_ThrowIntoResume(KosContext *ctx);


/**
  Return the active Koskari context during bytecode execution, or NULL
  when not inside koskari_eval.

  Callable from native Python object methods (e.g. ParamRef resolution).
*/
KosContext *kos_ctx_current(void);


/* The per-thread record behind the ambient context stack (KosTlsState)
   is private to domain.c. A boundary call resolves it once with
   kos_tls_current and hands it to the snapshot and the eval. */


/**
  Return the current thread's record, allocated when it has none.
  Allocation failure is fatal.
*/
KosTlsState *kos_tls_current(void);


/* The context on top of s's ambient stack, or NULL when none is
   active. */
KosContext *kos_tls_active_context(const KosTlsState *s);


/**
  Return the KosDomain answering on the thread s records: the active
  context's, else the innermost `with Domain():` scope's, else the
  thread default domain, minted on first need. Borrowed reference, or
  NULL with an error set when minting the thread default fails.
*/
PyObject *kos_tls_domain(KosTlsState *s);


/**
  Enter a drive on the thread record s: the record is held for the
  drive's length, ctx becomes the ambient context, and the context's
  token gains a live depth.
*/
void kos_tls_enter_drive(KosTlsState *s, KosContext *ctx);


/* The thread state s records. Every record a resolution returns
   (kos_tls_current and the resolutions inside domain.c) is owned by the
   current thread state, so for such a record this is the calling
   thread's state, read without PyThreadState_Get. */
PyThreadState *kos_tls_thread_state(const KosTlsState *s);


/**
  Leave the drive holding s: the token's live depth and the ambient
  context are dropped and the record is released.
*/
void kos_tls_leave_drive(KosTlsState *s, KosContext *ctx);


/**
  Return the KosDomain answering on this thread, as kos_tls_domain over
  the current thread's record, allocated when it has none. Borrowed
  reference, or NULL with an error set when allocating the record or
  minting the thread default fails.
*/
PyObject *kos_domain_current(void);



/* === Exception types === */


extern PyObject *KoskariContinuationDead;
extern PyObject *KoskariContinuationApply;
extern PyObject *KoskariContinuationError;


/* === KosContext_Call* : context-oriented generic call entry points === */

/**
   Tear the chain down to frame before tail-call frame reuse. A tail
   call from the function body's top level finds the frame already on
   top and pays only this compare; one from inside a let, loop or
   foreach drops those extents out of line.

   :return: 0 on success (DYN_TOP == frame), -1 with an exception set
*/
static inline int kos_dyn_teardown_to_frame(
    KosContext *ctx,
    KosDynFrame *frame) {

  if (KosContext_DYN_TOP(ctx) == (KosDynNode *) frame)
    return 0;
  return kos_dyn_teardown_extents_to_frame(ctx, frame);
}


/**
   Install a code frame for a call: on a tail call the live frame is
   torn down to, reset, and refilled with code and ns; otherwise a new
   frame is pushed above it. ns is stolen on every path. Returns the
   installed result (NORMAL, or POLL when the breaker is up), or RAISE.
*/
KosOpResult KosContext_InstallCallFrame(
    KosContext *ctx,
    KosCode *code,
    PyObject *ns,
    int tail_call);


/**
   The call entry: call callable with vectorcall-style args, and say
   where its result goes. tail_call and resume are independent.

   With resume set, the entry reports what happened and leaves the
   next hop to the calling resume, which ends in RESULT_ENTRY: an
   installed callee sets resume on the caller's frame and reports
   NORMAL (the callee is current; thread into it, and its return
   delivers by tail call into resume); a value settled in hand --
   a synchronous RETURN, or a plain callable's result -- reports
   RETURN with the value, no resume set (tail-call resume with it);
   a value that still needs settling settles one step, sets resume,
   and reports REPROLOGUE or AWAIT for the trampoline to deliver on a
   later trip; a call whose own result needed awaiting sets resume and
   reports AWAIT. RAISE / FATAL relay untouched -- the failure reaches
   resume through the raise walk instead. Caller must have a current
   frame. A tail call replaces that frame, so it has no caller to
   resume: tail_call with a resume is a caller error.

   With resume NULL the call's outcome relays as the install reports
   it, tail_call included: RETURN with the value in hand, not yet
   settled; NORMAL, POLL, or REPROLOGUE for an installed callee; AWAIT,
   RAISE, FATAL. Nothing is pushed on the caller's stack. This is the
   entry an install slot uses to call through to another callable; the
   CALL* opcodes have their own push entry inside the call core.
*/
KosOpResult KosContext_Call(
    KosContext *ctx,
    PyObject *callable,
    PyObject *const *args,
    Py_ssize_t nargs,
    PyObject *kwnames,
    int tail_call,
    const KosResumeKind *resume,
    void *state);


/**
   Install a frame for a native program: a resume and its state in
   place of code and a namespace, with frame_flags ORed in. A tail
   call reuses the live frame, as InstallCallFrame does for bytecode.
   Otherwise a native frame is pushed, taken from frame_cache when the
   slot holds one (frame_cache may be NULL). The frame carries the
   empty namespace. The resume is set here, before the caller makes
   its call, so a failure raised while this frame is current is
   delivered to the resume as NULL. Returns the frame, or NULL with
   PyErr set.
*/
KosDynFrame *KosContext_InstallNativeFrame(
    KosContext *ctx,
    const KosResumeKind *resume,
    void *state,
    uint32_t frame_flags,
    KosDynFrame **frame_cache,
    int tail_call);


/**
   Call callable with its result delivered to resume, from a context
   that is not itself a resume (a callable's install slot): the native
   form of a function whose body calls callable and then continues.
   Installs a native frame carrying resume, state, and frame_flags
   (KOS_FRAME_DEMAND refuses suspension within the call), reusing the
   live frame on a tail call, else taking the frame from frame_cache
   when the slot holds one (frame_cache may be NULL), and makes the
   call from it; the inner call is never a tail call, since its
   return must reach the resume.
   A value back in hand is staged for the resume; a callee's return
   and a settled value reach it the same way. A failure during the
   call reaches resume as a NULL delivery, which it must handle. Later
   failures reach it the same way. Returns REPROLOGUE for the staged
   value, else the call's outcome. args are borrowed.
*/
KosOpResult KosContext_CallInto(
    KosContext *ctx,
    PyObject *callable,
    PyObject *const *args,
    Py_ssize_t nargs,
    PyObject *kwnames,
    const KosResumeKind *resume,
    void *state,
    uint32_t frame_flags,
    KosDynFrame **frame_cache,
    int tail_call);


/**
   Settle a value in hand under a fresh native frame carrying resume
   and frame_flags (KOS_FRAME_DEMAND refuses a suspension). A value
   that settles at once is staged for the resume. One that suspends
   delivers on the revive. A failure reaches resume as a NULL
   delivery, which it must handle. Returns REPROLOGUE or AWAIT, or
   RAISE. val is consumed.
*/
KosOpResult KosContext_SettleInto(
    KosContext *ctx,
    PyObject *val,
    const KosResumeKind *resume,
    void *state,
    uint32_t frame_flags);


/**
   KosContext_Call with self as the receiver, injected as the first
   positional, under the same resume contract. No bound method is
   built. args and kwnames are borrowed.
*/
KosOpResult KosContext_CallMethod(
    KosContext *ctx,
    PyObject *callable,
    PyObject *self,
    PyObject *const *args,
    Py_ssize_t nargs,
    PyObject *kwnames,
    int tail_call,
    const KosResumeKind *resume,
    void *state);


/**
   KosContext_Call over a tuple and a dict, under the same resume
   contract: a plain Python callable takes them as they are, a
   KosCallable's are spread once to the vector shape.

   callable, args_tuple (may be NULL), kwds_dict (may be NULL) are
   borrowed.
*/
KosOpResult KosContext_CallArgs(
    KosContext *ctx,
    PyObject *callable,
    PyObject *args_tuple,
    PyObject *kwds_dict,
    int tail_call,
    const KosResumeKind *resume,
    void *state);


/* === Unit lifecycle === */


/**
   Push val onto the current frame stack, or if val is awaitable run one step
   and either push the result or suspend; a fresh Koskari coroutine is
   installed instead and REPROLOGUE names the installed callee.

   Use from code outside opcode dispatch that must not apply the CALL* POP,1
   peephole. Implementation: koskari/lib/eval/eval_await.c included into the
   eval translation unit.

   CALL* opcodes pass the fuse flag to the same push_or_await body,
   which may then elide the push plus a trailing POP,1.

   val is consumed (ownership transferred).
*/
KosOpResult KosContext_PushOrAwait(KosContext *ctx, PyObject *val);


/**
   Settle a value in hand without touching the stack: a plain value, a
   Defer, or a future comes back as it is; a fresh Koskari coroutine is
   installed as a call frame on the running chain; any other awaitable
   is eagerly stepped to its result, or suspends. NORMAL with *out the
   settled value (new reference), REPROLOGUE with a callee installed
   whose return lands the value on the current frame's stack, AWAIT
   with the yielded value (the revive lands the settled value there
   too), or RAISE. val is consumed.
*/
KosOpResult KosContext_Settle(KosContext *ctx, PyObject *val, PyObject **out);


/**
   Settle a delivery that still needs awaiting and stage the outcome
   for the current frame's resume. Takes one eager step through
   KosContext_Settle. A settled value is placed in the pending slot
   and EVAL_REPROLOGUE returns to the prologue, which classifies it
   again, so a value that settles to a further awaitable bounces
   again. A fresh Koskari coroutine is instead installed as a callee
   and its return delivers. A genuine suspension relays EVAL_AWAIT. A
   failure relays EVAL_RAISE. Sets no resume itself. The caller has
   set one, or sets one on the REPROLOGUE or AWAIT outcome, per its
   own contract. val is consumed.
*/
KosOpResult KosContext_SettleStage(KosContext *ctx, PyObject *val);


/**
   Inject traceback into the current PyErr in-place, leave PyErr set, and
   mark the context dead.  Call only when PyErr is set.  The RAISE invariant
   requires PyErr to remain set on every KOS_RES_RAISE boundary.
   Flushes live pc/acting_pc first.
*/
void kos_eval_prepare_boundary_raise(KosContext *ctx);


/**
   Record the current frame into the in-flight exception's
   __koskari_dynchain__ metadata (marked "crossed") before the raise walk
   pops it. PyErr is preserved; decoration is best-effort.
*/
void kos_eval_note_crossed_frame(KosContext *ctx);


/**
   Give the exception a Python traceback entry for every frame in its
   __koskari_dynchain__ record that has none yet, innermost first.
   Called with the fetched triple at an eval boundary, and at a
   Koskari handler's entry so the handler's e.__traceback__ holds the
   Koskari frames the raise crossed. Entries are marked traced in the
   record, so a frame is never injected twice. Best-effort: a failure
   leaves the triple as it was.
*/
void kos_eval_inject_recorded_frames(
    PyObject **exc_type,
    PyObject **exc_val,
    PyObject **exc_tb);


/**
   The record that closes a region push's extent (the POP_* row), for
   the pushes whose target is not an instruction argument. Read from
   the site's cache slot once it is filled; the first execution
   resolves it through the extent table and fills the slot, and a site
   with no slot resolves every time. Returns -1 with an error set when
   no extent of the kind encloses the site.
*/
static inline int kos_site_extent_close(
    KosContext *ctx,
    KosOp *insn,
    KosExtentKind kind,
    const char *who,
    KosOp **pop) {

  KosCacheSlot *slot = KosOp_SLOT(insn);
  PyObject *code;
  uint32_t eid;
  const KosExtentDesc *ext;

  if (slot && likely(slot->kind == KOS_CACHE_TARGET)) {
    *pop = slot->u.target.pop;
    return 0;
  }

  code = (PyObject *) KosContext_FrameCode(ctx);
  eid = kos_extent_find(code, KosOp_PC(insn), kind);
  if (eid == KOS_EXTENT_NONE) {
    PyErr_Format(PyExc_RuntimeError, "%s: no extent at this PC", who);
    return -1;
  }
  ext = kos_extent_desc(code, eid);
  if (! ext) {
    PyErr_Format(PyExc_RuntimeError, "%s: invalid extent", who);
    return -1;
  }
  *pop = &KosCode_CODE(code)[ext->pop_pc];
  if (slot && slot->kind == KOS_CACHE_EMPTY) {
    slot->kind = KOS_CACHE_TARGET;
    slot->u.target.pop = *pop;
  }
  return 0;
}


int koskari_eval_init(PyObject *mod);
int koskari_eval_clear(PyObject *mod);


#endif


// The end.
