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
  KOS_RES_REPROLOGUE,      /* restart prologue (PyErr set, or goal armed) */
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
   resume, an opcode handler), is defined in bytecode.h beside KosOp,
   the instruction record that carries a handler pointer. */


/**
   A per-frame CPS resume: the fn of the KosResumeKind (dynchain.h) a
   consumer sets on a frame via KosEvalState_CallResult /
   KosEvalState_NextResume to receive exactly one settled delivery -- a call's result, a callee frame's return, a
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
   frame with KosNativeFrame_ReturnPark, or return EVAL_RAISE to
   decline. Declining re-raises with the resume already disarmed, so
   the unwind proceeds exactly as it would have with no resume armed
   at all; a consumer that only propagates needs no code beyond that
   one line.

   The delivery reaches a code-less step frame through the prologue
   (process_raise hands off to it rather than walking). A frame
   carrying bytecode is never handed a failure that way: its resume is
   a yield-from phase resume, which reads a NULL delivery as a send of
   None, and it is reached through its own node instead.

   A resume on a frame the walk reaches ABOVE the live frame (a
   flattened delegation) does not get that freedom. The prologue's
   offer tests the live frame and cannot see it, so the walk still
   calls it as a plain call from inside the unwind, and it may only
   answer REPROLOGUE or EVAL_RAISE -- never thread on, since a chain
   run inside the walk would hand it a signal it cannot apply. A
   resume cannot tell the two paths apart from its own code, so a
   consumer that could be armed on either must keep to the narrower
   rule. Every KosIter provider arms on the live frame only.

   Returns a KosOpResult naming the next signal to the loop: another
   CPS entry call, EVAL_RETURN, EVAL_RAISE, EVAL_AWAIT, or
   EVAL_REPROLOGUE. The prototype is KosThreadFn.
*/


/**
   The breaker service, a KosThreadFn the cooperation points tail into
   when the eval breaker is up: yields the GIL when another thread has
   asked for it and runs the pending calls, then threads on at es->pc,
   or answers REPROLOGUE when the poll raised so the prologue's
   pending-exception phase takes the signal. insn and val are unused.
*/
KOS_PRESERVE_NONE KosOpResult kos_eval_poll(
    KosEvalState *es,
    void *insn,
    PyObject *val);


/**
   The prologue, a KosThreadFn: the ordered re-entry protocol -- staged
   delivery, pending exception, goal drain, resume delivery, with the
   breaker polled ahead of every pass -- run until no step asks for
   another pass, then the instruction at es->pc fetched and tail-called.
   Every re-entry into the instruction stream (a raise walk that found
   its handler, a plan transfer, a continuation replay, a staged
   delivery) tails into this, and it returns only a boundary result.
   insn and val are unused.
*/
KOS_PRESERVE_NONE KosOpResult kos_eval_prologue(
    KosEvalState *es,
    void *insn,
    PyObject *val);


/**
   The raise walk, a KosThreadFn every RESULT_RAISE tails into: the
   exception set is offered up the chain (try and finally extents,
   node conversion, a code-less frame's resume) and the answer is
   REPROLOGUE with the handler pc set, AWAIT for an async unwind, or
   the terminal RAISE or FATAL. insn and val are unused.
*/
KOS_PRESERVE_NONE KosOpResult kos_eval_raise(
    KosEvalState *es,
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
    KosEvalState *es,
    void *insn,
    PyObject *val);
KOS_PRESERVE_NONE KosOpResult kos_yield_deliver(
    KosEvalState *es,
    void *insn,
    PyObject *val);
KOS_PRESERVE_NONE KosOpResult kos_eval_await(
    KosEvalState *es,
    void *insn,
    PyObject *val);
KOS_PRESERVE_NONE KosOpResult kos_eval_control_returned(
    KosEvalState *es,
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
    KosEvalState *es,
    void *kind,
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
   TAILCALL(fn, es, arg, value): hand control to fn, a KosThreadFn, as
   the calling function's last act. The calling function must itself
   have the KosThreadFn prototype (musttail refuses anything else), and
   any Py_DECREF it owes runs before this, never after: its frame is
   discarded.
*/
#define TAILCALL(fn, es, arg, value) \
  KOS_MUSTTAIL return (fn)((es), (arg), (value))


/**
   DISPATCH(fn, es, arg, value): deliver value to fn, a KosResumeKind,
   by way of the prologue: fn and arg become the current frame's
   resume, value (an owned reference, or NULL for a cold start with
   nothing staged) goes into the eval state's pending slot, and the
   prologue is tailed into, which polls if due and then takes the slot
   and invokes fn. The frame stack is not touched. Caller must have a
   current frame, nothing may already be staged, and the calling
   function must have the KosThreadFn prototype. A resume handing a
   delivery to the next resume uses RESUME_HOP, which tail-calls the
   resume directly and falls back to this when the eval breaker is up.
*/
#define DISPATCH(kind, es, arg, value)                                  \
  do {                                                                  \
    KosEvalState *kos_dsp_es_ = (es);                                   \
    KosDynFrame *kos_dsp_frame_ = KosEvalState_FRAME(kos_dsp_es_);      \
    PyObject *kos_dsp_val_ = (value);                                   \
    KOS_DISPATCH_SAFETYNET(kos_dsp_es_, kos_dsp_val_);                  \
    KosEvalState_PENDING_VALUE(kos_dsp_es_) = kos_dsp_val_;             \
    KosDynFrame_RESUME_KIND(kos_dsp_frame_) = (kind);                   \
    KosDynFrame_RESUME_STATE(kos_dsp_frame_) = (void *) (arg);          \
    KOS_TRACE_TRAMPOLINE_STEP();                                        \
    TAILCALL(kos_eval_prologue, kos_dsp_es_, NULL, NULL);               \
  } while (0)

/* === Opcode result macros === */

/*
   The tails an opcode handler ends in. Each names what the handler
   did and where control goes next, so the same body reads correctly
   under both dispatch modes:

   - RESULT_NEXT(es): continue at the next instruction, insn + 1.
   - RESULT_JUMP_AT(es, target): continue at the target record.
   - RESULT_CONTINUE(es): continue at es->pc through es->code.
   - RESULT_PUSH(es, val): push a borrowed val, then continue. A
     failed push leaves val with its caller.
   - RESULT_STEAL(es, val): push an owned val (consumed, on failure
     too; NULL raises), then continue.
   - RESULT_RAISE(): PyErr is set; hand off to the raise machinery.
   - RESULT_FAIL(): hard fault.

   The straight-line forms (NEXT, PUSH, STEAL) step from the handler's
   own record: the next instruction is insn + 1, and its pc comes off
   that record, so no walk through es->code is needed. They are for a
   handler that leaves the frame and the pc alone. A handler whose
   helper installed a callee, returned to a parent, or stored a pc
   ends in RESULT_CONTINUE, which fetches from es->pc the way the
   trampoline does. A jump ends in RESULT_JUMP_AT with its resolved
   target record. RAISE and FAIL
   return: they are a real crossing back to the trampoline. Every
   continuing step is a tail call, so a handler using these macros
   must have the KosThreadFn prototype, must name its own record insn
   (KOS_OP_HANDLER does), and must run any Py_DECREF it owes before
   the macro.
*/

/* Thread into next, a record of the live frame's code: acting_pc and
   pc come off the record, then a tail call. A bare jump; the eval
   breaker is tested at the cooperation points (RESULT_NEXT_POLL, a
   frame install, RESUME_HOP, the prologue), not here. */
#define KOS_THREAD_AT(es, next)                                         \
  do {                                                                  \
    KosEvalState *kos_thr_es_ = (es);                                   \
    KosOp *kos_thr_next_ = (next);                                      \
    uint32_t kos_thr_pc_ = kos_thr_next_->pc;                           \
    kos_thr_es_->acting_pc = kos_thr_pc_;                               \
    kos_thr_es_->pc = kos_thr_pc_ + 1u;                                 \
    KOS_TRACE_THREAD_STEP(kos_thr_next_->op);                           \
    TAILCALL(kos_thr_next_->fn, kos_thr_es_, kos_thr_next_, NULL);      \
  } while (0)

/* Thread into the handler at es->pc: the fetch the trampoline does.
   The form for a tail after a frame change or a stored pc. */
#define KOS_THREAD_NEXT(es)                                             \
  do {                                                                  \
    KosEvalState *kos_thn_es_ = (es);                                   \
    KOS_THREAD_AT(kos_thn_es_,                                          \
                  &KosEvalState_OPS(kos_thn_es_)[KosEvalState_PC(kos_thn_es_)]); \
  } while (0)

#define RESULT_NEXT(es)      KOS_THREAD_AT((es), ((KosOp *) insn) + 1)
#define RESULT_CONTINUE(es)  KOS_THREAD_NEXT(es)

#define RESULT_JUMP_AT(es, target)  KOS_THREAD_AT((es), (target))

#define RESULT_PUSH(es, val)                                            \
  do {                                                                  \
    if (unlikely(KosEvalState_StackPushRef((es), (val)) < 0))           \
      RESULT_RAISE();                                                   \
    RESULT_NEXT(es);                                                    \
  } while (0)

#define RESULT_STEAL(es, val)                                           \
  do {                                                                  \
    PyObject *kos_rs_val_ = (val);                                      \
    if (unlikely(! kos_rs_val_))                                        \
      RESULT_RAISE();                                                   \
    if (unlikely(KosEvalState_StackPUSH((es), kos_rs_val_) < 0))        \
      RESULT_RAISE();                                                   \
    RESULT_NEXT(es);                                                    \
  } while (0)

/* RESULT_RAISE(): the exception is set; tail into the raise walk. The
   calling function must have the KosThreadFn prototype and hold es. A
   helper without that prototype returns EVAL_RAISE() instead and its
   handler's RESULT_RELAY makes the same tail call. */
#define RESULT_RAISE()  TAILCALL(kos_eval_raise, es, NULL, NULL)
#define RESULT_FAIL()   return EVAL_FATAL()

/* RESULT_REPROLOGUE(es): the handler left prologue work behind (an
   armed goal, a restored exception); tail into the prologue. The
   trampoline slot is stepped here as the loop stepped it on a raw
   REPROLOGUE, since the prologue itself does not step. */
#define RESULT_REPROLOGUE(es)                                           \
  do {                                                                  \
    KOS_TRACE_TRAMPOLINE_STEP();                                        \
    TAILCALL(kos_eval_prologue, (es), NULL, NULL);                      \
  } while (0)

/* The owned value leaves through the event's thread function; the
   calling function must have the KosThreadFn prototype. */
#define RESULT_AWAIT(es, val)   TAILCALL(kos_eval_await, (es), NULL, (val))
#define RESULT_RETURN(es, val)  TAILCALL(kos_return_deliver, (es), NULL, (val))

/**
   RESULT_RELAY(es, r): end a handler with the result of the helper
   that settled its operation -- the call core, an iteration step, an
   await, a dynamic node's transition. NORMAL means the helper left
   the next instruction at es->pc (a callee's first instruction after
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
#define RESULT_RELAY(es, r)                                             \
  do {                                                                  \
    KosOpResult kos_rr_ = (r);                                          \
    if (likely(kos_rr_.kind == KOS_RES_NORMAL))                         \
      KOS_THREAD_NEXT(es);                                              \
    TAILCALL(kos_eval_relay, (es),                                      \
             (void *) (uintptr_t) kos_rr_.kind, kos_rr_.val);           \
  } while (0)

/**
   RESULT_ENTRY(es, r, resume, state): end a resume with the outcome r
   of a CPS entry (KosEvalState_CallResult, KosEvalState_NextResume)
   it called with that same resume. NORMAL means a callee frame is
   installed and current with resume armed on the caller: thread into
   its first instruction. RETURN means the settled value is in hand
   and nothing is armed: tail-call resume with it now. Anything else
   (a suspension, a bounce the entry had to take, a failure) returns
   to the trampoline as it stands. The calling resume must have the
   KosThreadFn prototype and must run any Py_DECREF it owes before
   the macro.
*/
#define RESULT_ENTRY(es, r, resume, state)                              \
  do {                                                                  \
    KosOpResult kos_re_ = (r);                                          \
    if (likely(kos_re_.kind == KOS_RES_RETURN))                         \
      RESUME_HOP((resume), (es), (state), kos_re_.val);                 \
    if (kos_re_.kind == KOS_RES_NORMAL)                                 \
      KOS_THREAD_NEXT(es);                                              \
    return kos_re_;                                                     \
  } while (0)


/**
   RESUME_HOP(kind, es, state, value): hand a delivery from one resume
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
#define RESUME_HOP(kind, es, state, value)                              \
  do {                                                                  \
    if (KOS_EVAL_BREAKER_UP(es))                                        \
      DISPATCH((kind), (es), (state), (value));                         \
    KOS_TRACE_RESUME_STEP();                                            \
    TAILCALL((kind)->fn, (es), (state), (value));                       \
  } while (0)


/**
   RESULT_NEXT_POLL(es): thread on to the instruction at es->pc, unless
   the eval breaker is up, in which case tail into the breaker service,
   which polls and threads on to that same step. The cooperation point
   for an opcode that closes a cycle on its own (CONTINUE, the
   compiler's one back-edge).
*/
#define RESULT_NEXT_POLL(es)                                            \
  do {                                                                  \
    if (KOS_EVAL_BREAKER_UP(es))                                        \
      TAILCALL(kos_eval_poll, (es), NULL, NULL);                        \
    KOS_THREAD_NEXT(es);                                                \
  } while (0)

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
   Resume a return that parked on a node whose leave went asynchronous,
   now that the node has left the chain: re-enter the return path the
   obligation names with its value. ob is a KOS_OB_RETURN obligation
   handed back by kos_dyn_settle_top; it is consumed. The answer has
   been through the raise ingress or the await tag as a walk's would.
*/
KosOpResult KosEval_ResumeReturn(KosEvalState *es, KosObligation *ob);


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
   allocation and *cache is emptied. The frame carries kind and state
   from the install on, so a teardown at any point after it hands the
   kind's clear that state; the caller's later KosEvalState_CallResult
   / NextResume call sets the resume that receives the delivery.
   Returns the frame, or NULL with PyErr set.
*/
KosDynFrame *KosResumeFrame_Install(
    KosEvalState *es,
    const KosResumeKind *kind,
    void *state,
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
   by hand. Like KosEvalState_CallResult, the entry reports what
   happened and the calling resume ends in RESULT_ENTRY: a delivered
   element that is settled reports RETURN with it, nothing armed (the
   resume is tail-called with it); one that needs settling settles one
   step, arms resume, and reports REPROLOGUE or AWAIT; an in-flight
   CONTINUES arms resume and reports NORMAL when the install left a
   bytecode frame current (thread into it) or REPROLOGUE otherwise; an
   awaitable in-flight (EXIT) arms resume and reports EVAL_AWAIT, and
   the AWAITED revive delivers into it. Exhaustion arms resume and
   raises StopIteration, which reaches resume through the raise walk
   like an in-flight source's failure does; a request that fails
   outright reports RAISE with nothing armed. send_val is accepted for
   a future Send entry point; unused today. Caller must have a current
   frame.

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
    const KosResumeKind *resume,
    void *state);


/* === KosContext: VM execution context === */

/**
  KosContext is the top-level execution state for the Koskari VM.

  The dyn_top field points to the top of the dynamic chain, which may
  be any node kind (FRAME, GEN, PROMPT, WIND, TRY, and the rest).

  The frame_top field is a cached pointer to the topmost FRAME node
  for fast access during normal execution.

  The goal field is the active transfer goal, or NULL; parked outer
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

  Between koskari_eval drives, holds dyn_top, goal, and derived revive
  snapshots. During RUN those fields are NULL on ctx (stolen onto
  KosEvalState); see KosEvalState_ThawFromContext and KosEvalState_FreezeToContext.
*/


/**
   A call for koskari_eval to install at entry on a fresh context
   (KosContext::entry_call): KosEvalState_CallInto from a code-less root
   frame carrying resume and frame_flags. args and kwnames are
   borrowed for the drive.
*/
typedef struct KosEntryCall {
  PyObject *callable;
  PyObject *const *args;
  Py_ssize_t nargs;
  PyObject *kwnames;
  const KosResumeKind *resume;
  uint32_t frame_flags;
} KosEntryCall;


struct KosContext {
  PyObject_HEAD
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

  /* The active transfer goal, or NULL; parked outer goals hang off it */
  KosXferGoal *goal;

  /* --- Python bridge: await/yield suspend and resume --- */
  PyObject *py_resume_inject;  /* value to push on next koskari_eval when resuming */
  uint8_t py_resume_kind;      /* 0=none, 1=await, 2=yield */
  PyObject *py_await_iter;     /* iterator from __await__() when suspended on AWAIT */

  /* --- Entry call (set by a Python-boundary entry on a fresh context):
     koskari_eval installs it on its own eval state at entry, then
     clears it. Borrowed; the setter owns the record for the drive. --- */
  const struct KosEntryCall *entry_call;

  /* --- Exception injection (set by coroutine_throw or dynchain inject) --- */
  int inject_mode;         /* 0=none, 1=pending_throw */
  PyObject *inject_type;   /* exception type when inject_mode==KOS_INJECT_THROW */
  PyObject *inject_value;  /* exception object when inject_mode==1 */

  /* --- Handler-visible exception state (sys.exc_info) saved across
     suspensions: swapped with the thread state at koskari_eval
     entry/exit so interleaved contexts on one thread cannot corrupt
     each other's view. Context-isolated: a fresh context starts with
     empty handler state (no fall-through to the host's). --- */
  KosExcState exc_info;

  /* Last eval boundary reason for revive */
  uint8_t suspension_event;  /* KosSuspensionEvent */

  uint32_t flags;            /* KosContextFlags */
};


extern PyTypeObject KosContextType;

#define KosContext_Check(o) PyObject_TypeCheck((o), &KosContextType)


/* === KosContext accessor macros === */

#define KosContext_DYN_TOP(c)                (((KosContext *)(c))->dyn_top)
#define KosContext_FRAME_TOP(c)              (((KosContext *)(c))->frame_top)
#define KosContext_BLOCK(c)                  (((KosContext *)(c))->block)
#define KosContext_NAMESPACE(c)              (((KosContext *)(c))->namespace)
#define KosContext_TOKEN(c)                  (((KosContext *)(c))->token)
#define KosContext_DOMAIN(c)                 (((KosContext *)(c))->domain)
#define KosContext_PARAM_REFS(c)            (((KosContext *)(c))->param_refs)
#define KosContext_GOAL(c)                   (((KosContext *)(c))->goal)
#define KosContext_PY_AWAIT_ITER(c)          (((KosContext *)(c))->py_await_iter)
#define KosContext_PY_RESUME_INJECT(c)       (((KosContext *)(c))->py_resume_inject)
#define KosContext_PY_RESUME_KIND(c)         (((KosContext *)(c))->py_resume_kind)

#define KOS_RESUME_NONE  0
#define KOS_RESUME_AWAIT 1
#define KOS_RESUME_YIELD 2
#define KosContext_ENTRY_CALL(c)             (((KosContext *)(c))->entry_call)
#define KosContext_INJECT_MODE(c)            (((KosContext *)(c))->inject_mode)
#define KosContext_INJECT_TYPE(c)            (((KosContext *)(c))->inject_type)
#define KosContext_INJECT_VALUE(c)           (((KosContext *)(c))->inject_value)
#define KosContext_EXC_INFO(c)               (&((KosContext *)(c))->exc_info)

#define KOS_INJECT_NONE   0
#define KOS_INJECT_THROW  1
#define KosContext_SUSPENSION_EVENT(c)      (((KosContext *)(c))->suspension_event)

#define KosContext_FLAGS(c)                  (((KosContext *)(c))->flags)


/* === KosEvalState: C-local execution snapshot (API scaffolding) === */

/**
  KosEvalState holds a borrowed pointer to the heap KosContext plus the live
  frame, code, operand-segment slot, and namespace for the active eval
  slice.

  See design/interpreter/eval-state-live-authority.md and the archived
  design/archive/freeze-thaw-context-segments.md.

  KosEvalState_Init only binds ctx and clears es. koskari_eval calls
  KosEvalState_ThawFromContext to move dyn_top, goal, and derived snapshots
  from ctx onto es (NULL on ctx). While RUN, es owns chain and goal;
  KosEvalState_DYN_TOP and KosEvalState_GOAL never read ctx. Freeze restores
  onto ctx and NULLs es. Chain shape changes refresh live fields via
  kos_dyn_resync_after_push/pop or kos_dyn_resync_chain.

  With KOS_TRACE_EVAL_SYNC, call counts and total wall time per sync API are
  recorded for kos_trace_report / get_trace_report under eval_state_sync.
*/

struct KosEvalState {
  KosContext *ctx;
  KosDynNode *dyn_top;       /* RUN: top of dynamic chain (stolen from ctx) */
  KosXferGoal *goal;         /* RUN: active transfer goal (stolen from ctx) */
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
  const volatile KosBreakerWord *breaker; /* this thread's eval breaker
                         (compat.h), resolved at Init; the cooperation
                         points test it and the trampoline polls when
                         it is up */
  uint16_t recursion_limit; /* the deepest frame this context may
                         install: sys.getrecursionlimit() read at Init
                         and clamped to KOS_FRAME_DEPTH_MAX */
  uint8_t terminal;         /* the result in flight is a boundary that
                         has been through its event: a raise walked to
                         no handler, a suspension recorded for AWAIT or
                         YIELD, a root return. The trampoline and the
                         resume delivery leave with it instead of
                         applying it again */
};


#define KosEvalState_BREAKER(es) ((es)->breaker)

/* The eval breaker test at a cooperation point: one load, one branch.
   Up means another thread wants the GIL or a signal is pending, and the
   trampoline's poll section answers both before threading resumes. */
#define KOS_EVAL_BREAKER_UP(es)                                         \
  unlikely((*KosEvalState_BREAKER(es) & KOS_BREAKER_MASK) != 0)


/**
   The result a call-core install arm returns once the callee frame is
   live at pc 0: thread on, or POLL when the eval breaker is up so the
   handler's RESULT_RELAY tails into the breaker service before the
   callee's first instruction. Frame entry is a cooperation point
   because a tail-recursive loop has no CONTINUE.
*/
/**
   True when a just-installed frame can be threaded into at its pc: it
   has code and is not waiting on a resume. A frame suspended inside a
   yield-from carries the node's phase resume, and its delivery must
   run first, so the installer bounces through the prologue instead.
*/
static inline int kos_frame_threads(KosDynFrame *nf) {
  return nf && KosDynFrame_CODE(nf) && ! KosDynFrame_RESUME_KIND(nf);
}


static inline KosOpResult kos_call_installed(KosEvalState *es) {
  return KOS_EVAL_BREAKER_UP(es) ? EVAL_POLL() : EVAL_NORMAL();
}


#define KosEvalState_CONTEXT(es) ((es)->ctx)
#define KosEvalState_DYN_TOP(es)     ((es)->dyn_top)
#define KosEvalState_GOAL(es)        ((es)->goal)
#define KosEvalState_FRAME(es) ((es)->frame)
#define KosEvalState_RECURSION_LIMIT(es) ((es)->recursion_limit)

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
    KosEvalState *es,
    KosDynFrame *base,
    unsigned count) {

  unsigned depth = base ? KosDynFrame_DEPTH(base) : 0u;

  if (likely(depth + count <= KosEvalState_RECURSION_LIMIT(es)))
    return 0;
  return kos_eval_recursion_error();
}
#define KosEvalState_CODE(es) ((es)->code)
/** The live code's instruction array. Read through the code object on
    every fetch, never cached on the state or a frame: packing a code
    object into an arena relocates its block, and a parked frame (a
    defer, a generator, a function's cached frame) may hold the code
    across that move. */
#define KosEvalState_OPS(es) KosCode_CODE(KosEvalState_CODE(es))
#define KosEvalState_BLOCK(es) ((es)->block)
#define KosEvalState_NAMESPACE(es) ((es)->namespace)

/** Live next-instruction PC on es only; use KosEvalState_SET_PC to assign. */
#define KosEvalState_PC(es) ((es)->pc)
/** Instruction currently executing (last fetch); live on es only. */
#define KosEvalState_ACTING_PC(es) ((es)->acting_pc)
#define KosEvalState_SET_PC(es, v) ((void)((es)->pc = (uint32_t)(v)))
/** The staged CPS delivery, owned; NULL when nothing is staged. */
#define KosEvalState_PENDING_VALUE(es) ((es)->pending_value)


/** Stage an owned delivery for the current frame's resume, releasing
    any earlier one still in the slot. */
static inline void KosEvalState_StageDelivery(KosEvalState *es, PyObject *val) {
  Py_XDECREF(KosEvalState_PENDING_VALUE(es));
  KosEvalState_PENDING_VALUE(es) = val;
}


/**
   The result of an iteration install: thread into the installed frame
   when it can be threaded into, else bounce through the prologue, which
   runs its resume or its entry sequence.
*/
static inline KosOpResult kos_iter_installed(KosEvalState *es) {
  return kos_frame_threads(KosEvalState_FRAME(es))
      ? EVAL_NORMAL() : EVAL_REPROLOGUE();
}


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
  KosNamespace_Resolve(KosEvalState_CurrentNamespace(es), (key))

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
  Steal dyn_top, goal, and derived snapshots from ctx onto es and
  NULL those slots on ctx. Call at koskari_eval entry after Init. Does not
  steal ctx flags or Python bridge fields.
*/

void KosEvalState_ThawFromContext(KosEvalState *es);


/**
  Restore dyn_top, goal, and derived snapshots from es onto ctx and NULL
  es. Call from koskari_eval_cleanup after FlushLiveRegsToFrame.
*/

void KosEvalState_FreezeToContext(KosEvalState *es);



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
   Push a prepared frame from the per-function cache as DYN_TOP and
   FRAME_TOP.  Code, namespace, and cleared stack are already attached
   and the node base was normalized at park time
   (KosFunction_TryParkFrame); this is the install half of
   kos_dyn_push_frame with the node id reset, and cannot fail.
   The caller has asked kos_eval_check_frame_room for one frame before
   binding into the parked namespace.
*/
static inline void kos_dyn_push_ready_frame(
    KosEvalState *es,
    KosDynFrame *frame) {

  KosDynNode_PARENT(frame) = KosEvalState_DYN_TOP(es);
  KosDynNode_NODE_ID(frame) = 0;  /* a fresh instance; minted on first read */

  {
    KosDynFrame *cur = KosEvalState_FRAME(es);
    if (cur) {
      KosDynFrame_FLAGS(frame) |= (KosDynFrame_FLAGS(cur) & KOS_FRAME_DEMAND);
      KosDynFrame_DEPTH(frame) = KosDynFrame_DEPTH(cur) + 1;
      KosEvalState_FlushLiveRegsToFrame(es);
    } else {
      KosDynFrame_DEPTH(frame) = 1;
    }
  }

  /* the installed segment's unwind cache: the live slot it displaces */
  KosSegment_BACK(KosDynNode_STACK((KosDynNode *) frame)) =
      KosEvalState_BLOCK(es);
  if (KosSegment_IS_INLINE(KosDynNode_STACK((KosDynNode *) frame)))
    KOS_TRACE_EVENTS_INC(KOS_TE_SEG_INLINE_INSTALL);
  KosDynFrame_BACK_FRAME(frame) = (KosDynNode *) KosEvalState_FRAME(es);
  KosEvalState_DYN_TOP(es) = (KosDynNode *) frame;
  KosEvalState_FRAME(es) = frame;
  KosEvalState_BLOCK(es) = &KosDynNode_STACK((KosDynNode *) frame);
  KosEvalState_NAMESPACE(es) = KosDynNode_NS((KosDynNode *) frame);
  KosEvalState_CODE(es) = KosDynFrame_CODE(frame);
  es->pc = 0;
  es->acting_pc = 0;
}


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
   Pop TOS and materialize it as a fast sequence for the UNPACK family.
   need is the required length; exact demands len == need, otherwise
   len >= need. Returns a new reference, or NULL with TypeError (subject
   not iterable) or ValueError (wrong length) set. who names the opcode
   in the message.
*/
PyObject *KosEvalState_PopUnpackSequence(
    KosEvalState *es,
    Py_ssize_t need,
    int exact,
    const char *who);


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

  :return: new reference to the context, or NULL on error
*/
KosContext *KosContext_New(KosDynFrame *top);


/*
  A context deallocates with its last reference. Deallocation frees
  the dynamic chain, walking every node and running kind-specific free
  logic (including Py_DECREF on Python objects held by dyn nodes), which
  can invoke arbitrary Python code via destructors, and frees the armed
  transfer goal and its outers and the await, resume, inject, domain,
  and parameter state. It does not start koskari_eval or execute
  bytecode: a holder that owes the extent protocol a close drive runs
  it before releasing.
*/


/**
   Make a context reusable after a completion at its root: drop the
   finished frame chain with the frozen block and namespace derived
   from it, and clear the completion residue -- dead and running
   flags, suspension tag, in-flight await iterator, resume inject,
   armed throw injection, and the handler-visible exception state.
   The token, domain, and parameter cells stay: they are the context's
   identity. A context holding state this reset cannot account for (an
   armed transfer goal) is left untouched and the caller releases it
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



/* === Exception types === */


extern PyObject *KoskariContinuationDead;
extern PyObject *KoskariContinuationApply;
extern PyObject *KoskariContinuationError;


/* === KosEvalState_Call* : eval-state-oriented generic call entry points === */

/**
   Install a code frame for a call: on a tail call the live frame is
   torn down to, reset, and refilled with code and ns; otherwise a new
   frame is pushed above it. ns is stolen on every path. Returns the
   installed result (NORMAL, or POLL when the breaker is up), or RAISE.
*/
KosOpResult KosEvalState_InstallCallFrame(
    KosEvalState *es,
    KosCode *code,
    PyObject *ns,
    int tail_call);


/**
   KosEvalState_Call: the call entry whose result lands on the current
   frame's stack, for the CALL* opcodes and the tuple entry.

   Accepts any callable. A KosCallable runs its install; a value it
   reports in hand is pushed or awaited onto the caller's stack, and an
   installed callee is left current. A plain Python callable is
   vectorcalled and its result pushed or awaited the same way; in tail
   position its frame is popped first when nothing is open above it,
   else the result is delivered by RETURN.

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
   The call entry: call callable with vectorcall-style args, and say
   where its result goes. tail_call and resume are independent.

   With resume set, the entry reports what happened and leaves the
   next hop to the calling resume, which ends in RESULT_ENTRY: an
   installed callee arms resume on the caller's frame and reports
   NORMAL (the callee is current; thread into it, and its return
   delivers by tail call into resume); a value settled in hand --
   a synchronous RETURN, or a plain callable's result -- reports
   RETURN with the value, nothing armed (tail-call resume with it);
   a value that still needs settling settles one step, arms resume,
   and reports REPROLOGUE or AWAIT for the trampoline to deliver on a
   later trip; a call whose own result needed awaiting arms resume and
   reports AWAIT. RAISE / FATAL relay untouched -- the failure reaches
   resume through the raise walk instead. Caller must have a current
   frame. A tail call replaces that frame, so it has no caller to
   resume: tail_call with a resume is a caller error.

   With resume NULL the call's outcome relays as the install reports
   it, tail_call included: RETURN with the value in hand, not yet
   settled; NORMAL, POLL, or REPROLOGUE for an installed callee; AWAIT,
   RAISE, FATAL. Nothing is pushed on the caller's stack. This is the
   entry an install slot uses to call through to another callable.
*/
KosOpResult KosEvalState_CallResult(
    KosEvalState *es,
    PyObject *callable,
    PyObject *const *args,
    Py_ssize_t nargs,
    PyObject *kwnames,
    int tail_call,
    const KosResumeKind *resume,
    void *state);


/**
   Call callable with its result delivered to resume, from a context
   that is not itself a resume (a callable's install slot). Pushes a
   code-less frame, ORs frame_flags into it (KOS_FRAME_DEMAND to
   refuse suspension within the call), and makes the call from that
   frame through KosEvalState_CallResult, which arms resume and state
   on it. A value back in hand is staged for the resume; a callee's
   return and a settled value reach it the same way. A failure
   propagates past the frame. Returns REPROLOGUE for the staged value,
   else CallResult's outcome. args are borrowed.
*/
KosOpResult KosEvalState_CallInto(
    KosEvalState *es,
    PyObject *callable,
    PyObject *const *args,
    Py_ssize_t nargs,
    PyObject *kwnames,
    const KosResumeKind *resume,
    void *state,
    uint32_t frame_flags);


/**
   Call callable with self as its receiver, injected as the first
   positional, and report the outcome as KosEvalState_CallResult with
   no resume does: RETURN with the value in hand, or an installed
   callee. No bound method is built. args and kwnames are borrowed.
*/
KosOpResult KosEvalState_CallWithReceiver(
    KosEvalState *es,
    PyObject *callable,
    PyObject *self,
    PyObject *const *args,
    Py_ssize_t nargs,
    PyObject *kwnames,
    int tail_call);


/**
   KosEvalState_CallArgs: KosEvalState_Call over a tuple and a dict,
   spread once to the vector shape. Used by CALL_STAR and
   TAIL_CALL_STAR.

   callable, args_tuple, kwds_dict are borrowed.
*/
KosOpResult KosEvalState_CallArgs(
    KosEvalState *es,
    PyObject *callable,
    PyObject *args_tuple,
    PyObject *kwds_dict,
    int tail_call);


/* === Unit lifecycle === */


/**
   Push val onto the current frame stack, or if val is awaitable run one step
   and either push the result or suspend.

   Use from code outside opcode dispatch that must not apply the CALL* POP,1
   peephole. Implementation: koskari/lib/eval/eval_await.c included into the
   eval translation unit.

   CALL* opcodes pass the fuse flag to the same push_or_await body,
   which may then elide the push plus a trailing POP,1.

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
