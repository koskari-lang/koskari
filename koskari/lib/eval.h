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


/* === Forward declarations === */

struct KosCode;
typedef struct KosCode KosCode;


/* === KosEvaluatorToken === */


/**
   KosEvaluatorToken is a refcounted PyObject that identifies a koskari_eval
   domain/instance.  It tracks whether the evaluator is currently active on
   this thread (live_depth) and serves as an ownership marker for
   Continuations and DelimitedContinuations.
*/

typedef struct KosEvaluatorToken KosEvaluatorToken;

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
  KOS_CTX_RUNNING      = 0x01,
  KOS_CTX_FROZEN       = 0x02,
  KOS_CTX_DEAD         = 0x04,
  KOS_CTX_IN_HOOK      = 0x08,  /* executing a hook block */
  KOS_CTX_IN_TRANSFER  = 0x10,  /* executing a transfer plan */
} KosContextFlags;


/* === KosFrame: alias for KosDynFrame === */


/**
   KosFrame is an alias for KosDynFrame for backward compatibility.
   See dynchain.h for the actual structure definition.

   The frame represents a function activation in the dynamic chain.
   It holds the bytecode, program counter, operand stack, and namespace.
*/

typedef KosDynFrame KosFrame;

#define KosFrame_CODE(f)   KosDynFrame_CODE(f)
#define KosFrame_PC(f)     KosDynFrame_PC(f)
#define KosFrame_STACK(f)  KosDynFrame_STACK(f)
#define KosFrame_NS(f)     KosDynFrame_NS(f)


/* === KosEvent: boundary events === */

/**
   KosEventKind enumerates the boundary conditions that can cause
   koskari_eval to suspend and return control to the caller.

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
   KosEvent carries the result of a koskari_eval call.

   The kind field indicates what boundary was reached.
   The payload field holds the associated value (return value,
   exception, yielded value, or awaitable).
*/

typedef struct KosEvent {
  KosEventKind kind;
  PyObject *payload;       /* new reference; caller owns it */
} KosEvent;


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

  /* Hook execution state */
  KosHookRunner hook_runner;

  /* Transfer plan state */
  KosTransferPlan *plan;        /* active transfer plan (or NULL) */
  KosTransferPlan *plan_stack;  /* suspended plans stack */
  uint32_t chain_version;       /* incremented on chain modifications */

  /* Delimited continuation: top of installed segment when inside invoked k */
  KosDynNode *invocation_segment_top;

  /* Python await bridging state */
  PyObject *py_await_iter;   /* await-iterator from __await__() */
  PyObject *py_last_yielded; /* last yielded object (debug/optional) */
  PyObject *py_await_inject; /* value to push on next koskari_eval when resuming */
  PyObject *py_yield_inject; /* value to push on next koskari_eval when resuming from yield */

  /* exception injection for resume */
  int pending_throw;
  PyObject *pending_exc;

  /* finally handler state: we entered a finally due to raise/return */
  int handling_raise;        /* 1 when in finally because of exception */
  PyObject *handling_result;  /* stashed return value (future return-through-finally) */

  uint32_t flags;            /* KosContextFlags */
};


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

   :return: 0 on success, -1 on internal error
*/
int koskari_eval(KosContext *ctx, KosEvent *out_ev);


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


/* === Exception types === */


extern PyObject *KoskariContinuationDead;
extern PyObject *KoskariContinuationApply;
extern PyObject *KoskariContinuationError;


/* === Unit lifecycle === */


int koskari_eval_init(PyObject *mod);
int koskari_eval_clear(PyObject *mod);


#endif


// The end.
