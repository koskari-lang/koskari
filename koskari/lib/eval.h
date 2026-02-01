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

   Evaluator core for Koskari: undelimited multi-shot continuations,
   dynamic wind/unwind, and flattened async execution.

   :author: Christopher O'Brien <obriencj@preoccupied.net>
   :license: GNU General Public License v3
   :ai-assistant: Claude Opus 4.5 via Cursor
*/


#ifndef KOSKARI_EVAL_H
#define KOSKARI_EVAL_H


#include <Python.h>


/* === Forward declarations === */

struct KosCode;
typedef struct KosCode KosCode;

struct KosHandler;
typedef struct KosHandler KosHandler;

struct KosFrame;
typedef struct KosFrame KosFrame;

struct KosContext;
typedef struct KosContext KosContext;


/* === Context flags === */


typedef enum KosContextFlags {
  KOS_CTX_RUNNING = 0x01,
  KOS_CTX_FROZEN  = 0x02,
  KOS_CTX_DEAD    = 0x04,
} KosContextFlags;


/* === KosHandler: dynamic wind/unwind chain === */

/**
   KosHandler represents a node in the dynamic wind/unwind chain.

   Each handler may have a 'before' callable (invoked when entering
   the dynamic extent) and/or an 'after' callable (invoked when leaving
   the dynamic extent). Either may be NULL.

   The parent pointer links handlers from inner to outer scope.
   This chain is traversed during continuation application to determine
   which unwind (after) and wind (before) hooks to execute.
*/

struct KosHandler {
  KosHandler *parent;   /* enclosing handler (outer scope) */
  PyObject *before;     /* callable or NULL; invoked on wind-in */
  PyObject *after;      /* callable or NULL; invoked on unwind-out */
};


KosHandler *kos_handler_new(KosHandler *parent,
        PyObject *before, PyObject *after);

void kos_handler_free(KosHandler *handler);


/* === KosFrame: execution frame === */

/**
   KosFrame represents a single activation record in the Koskari VM.

   Invariants:
   - A frame is owned by exactly one context while RUNNING.
   - On capture (continuation creation), frames may be cloned to allow
     multi-shot resumption.
   - The pc field is mutated during execution; on freeze, frames should
     be considered immutable or cloned.

   The stack field points to a KosStackBlock chain (from stack.h),
   which holds the operand stack for this frame. The ns field holds
   the namespace (locals/closures) for this frame.

   Future optimization (TODO): implement lazy frame COW so that
   captured frame chains can share unchanged tail segments.
*/

struct KosFrame {
  KosFrame *parent;        /* caller frame */

  KosCode *code;           /* bytecode object (borrowed) */
  uint32_t pc;             /* program counter */

  struct KosStackBlock *stack;  /* operand stack (owned) */
  PyObject *ns;            /* namespace PyObject (owned ref) */

  KosHandler *handlers;    /* dynamic wind/unwind chain for this frame */

  uint32_t flags;          /* frame-specific flags */
};


KosFrame *kos_frame_new(KosCode *code, PyObject *ns,
        struct KosStackBlock *stack, KosFrame *parent,
        KosHandler *handlers);

KosFrame *kos_frame_clone(KosFrame *frame);

void kos_frame_free(KosFrame *frame);


/* === KosEvent: boundary events === */

/**
   KosEventKind enumerates the boundary conditions that can cause
   kos_drive to suspend and return control to the caller.

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
   KosEvent carries the result of a kos_drive call.

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

   Ownership and lifecycle:
   - A context is created via kos_context_new and freed via kos_context_free.
   - The context owns its frame chain and associated state.
   - While RUNNING, exactly one Python wrapper (KosPyCoroutine) may own it.

   Suspension and resumption:
   - When kos_drive returns KOS_EV_AWAIT, the context is suspended.
   - py_await_iter holds the iterator from __await__() on the Python awaitable.
   - Resumption injects the result back into the VM via the stack.

   Exception injection:
   - pending_throw indicates whether the next resume should throw.
   - pending_exc holds the exception to raise.
*/

struct KosContext {
  KosFrame *top;            /* current frame (top of call stack) */

  /* Python await bridging state */
  PyObject *py_await_iter;  /* await-iterator from __await__() */
  PyObject *py_last_yielded; /* last yielded object (debug/optional) */

  /* exception injection for resume */
  int pending_throw;
  PyObject *pending_exc;

  uint32_t flags;           /* KosContextFlags */
};


KosContext *kos_context_new(KosFrame *top);

void kos_context_free(KosContext *ctx);


/* === Wind chain management === */

/**
   Push a wind handler onto the current frame's handler chain.

   Either before or after may be NULL, but not both.

   :return: 0 on success, -1 on error (sets exception)
*/
int kos_push_wind(KosContext *ctx, PyObject *before, PyObject *after);


/**
   Convenience: push a handler with only a before hook.

   :return: 0 on success, -1 on error
*/
int kos_push_before_wind(KosContext *ctx, PyObject *before);


/**
   Convenience: push a handler with only an after hook.

   :return: 0 on success, -1 on error
*/
int kos_push_after_wind(KosContext *ctx, PyObject *after);


/**
   Pop the topmost handler from the current frame's handler chain.

   Does not invoke any hooks.

   :return: 0 on success, -1 on error
*/
int kos_pop_wind(KosContext *ctx);


/* === Continuation application === */

/**
   Apply a captured continuation, switching from the current execution
   state to the target state.

   This function:
   1. Computes the LCA (lowest common ancestor) between current and target frames.
   2. Collects unwind-after hooks from current to LCA (inner to outer).
   3. Collects wind-before hooks from LCA to target (outer to inner).
   4. Executes unwind hooks.
   5. Installs target_top and target_handlers into ctx.
   6. Executes wind hooks.
   7. Injects value_or_exc onto the target stack (or raises if is_throw).

   :param ctx: the current context
   :param target_top: the frame to resume into
   :param target_handlers: the handler chain to restore
   :param is_throw: if non-zero, value_or_exc is an exception to raise
   :param value_or_exc: the resume value or exception (borrowed ref)

   :return: 0 on success, -1 on error
*/
int kos_apply_continuation(KosContext *ctx,
        KosFrame *target_top, KosHandler *target_handlers,
        int is_throw, PyObject *value_or_exc);


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
   runs opcodes from ctx->top until:
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
int kos_drive(KosContext *ctx, KosEvent *out_ev);


/* === GC helpers for Python wrappers === */

/**
   Traverse a KosStackBlock chain for GC.

   This is used by Python wrapper types (KosPyCoroutine, etc.) to
   implement tp_traverse. It visits all PyObject* values held in
   the stack blocks.

   :param block: the head of the block chain
   :param visit: the visitproc callback
   :param arg: the opaque arg for visit

   :return: 0 on success, non-zero if visit returns non-zero
*/
int kos_stackblock_traverse(struct KosStackBlock *block,
        visitproc visit, void *arg);


/**
   Traverse a KosHandler chain for GC.

   Visits before and after callables in each handler.

   :return: 0 on success, non-zero if visit returns non-zero
*/
int kos_handler_traverse(KosHandler *handler, visitproc visit, void *arg);


/**
   Traverse a KosFrame chain for GC.

   Visits the namespace, stack, and handler chain for each frame.

   :return: 0 on success, non-zero if visit returns non-zero
*/
int kos_frame_traverse(KosFrame *frame, visitproc visit, void *arg);


/**
   Traverse a KosContext for GC.

   Visits all Python objects reachable from the context.

   :return: 0 on success, non-zero if visit returns non-zero
*/
int kos_context_traverse(KosContext *ctx, visitproc visit, void *arg);


/* === Python-facing wrapper types === */


/* --- KosPyContinuation --- */

/**
   KosPyContinuation is a Python-callable that resumes at a captured point.

   Multi-shot: each call clones the captured state before resuming.
   Calling returns a KosPyCoroutine that executes the continuation.
*/

extern PyTypeObject KosPyContinuationType;


typedef struct {
  PyObject_HEAD
  KosFrame *captured_top;
  KosHandler *captured_handlers;
} KosPyContinuation;


#define KosPyContinuation_Check(obj)                                    \
  ((obj) && PyType_IsSubtype((obj)->ob_type, &KosPyContinuationType))

#define KosPyContinuation_CheckExact(obj)               \
  ((obj) && ((obj)->ob_type == &KosPyContinuationType))


PyObject *KosPyContinuation_New(KosFrame *top, KosHandler *handlers);


/* --- KosPyCoroutine --- */

/**
   KosPyCoroutine wraps a KosContext for Python's coroutine protocol.

   States:
   - KOS_CORO_NEW: created but not yet started
   - KOS_CORO_RUNNING: currently executing
   - KOS_CORO_WAITING_PY: suspended on Python awaitable
   - KOS_CORO_DONE: completed (returned or raised)
*/

extern PyTypeObject KosPyCoroutineType;


typedef enum {
  KOS_CORO_NEW = 0,
  KOS_CORO_RUNNING,
  KOS_CORO_WAITING_PY,
  KOS_CORO_DONE,
} KosCoroState;


typedef struct {
  PyObject_HEAD
  KosContext *ctx;
  KosCoroState state;
  PyObject *pending_await;  /* yielded object to bubble outward */
  int in_send;              /* re-entrancy guard */
} KosPyCoroutine;


#define KosPyCoroutine_Check(obj)                                       \
  ((obj) && PyType_IsSubtype((obj)->ob_type, &KosPyCoroutineType))

#define KosPyCoroutine_CheckExact(obj)                  \
  ((obj) && ((obj)->ob_type == &KosPyCoroutineType))


PyObject *KosPyCoroutine_New(KosContext *ctx);


/* --- KosPyAsyncGen --- */

/**
   KosPyAsyncGen wraps a KosContext for Python's async generator protocol.

   Similar to KosPyCoroutine but yields values via __anext__.
*/

extern PyTypeObject KosPyAsyncGenType;


typedef struct {
  PyObject_HEAD
  KosContext *ctx;
  int in_progress;   /* anext in progress */
  int closed;        /* generator closed */
} KosPyAsyncGen;


#define KosPyAsyncGen_Check(obj)                                        \
  ((obj) && PyType_IsSubtype((obj)->ob_type, &KosPyAsyncGenType))

#define KosPyAsyncGen_CheckExact(obj)                   \
  ((obj) && ((obj)->ob_type == &KosPyAsyncGenType))


PyObject *KosPyAsyncGen_New(KosContext *ctx);


/* --- KosPyAsyncGenASend --- */

/**
   KosPyAsyncGenASend is the awaitable returned by __anext__ on an async gen.

   Each __anext__ call creates one of these, which drives the generator
   context until a yield or completion.
*/

extern PyTypeObject KosPyAsyncGenASendType;


typedef struct {
  PyObject_HEAD
  KosPyAsyncGen *agen;
  PyObject *send_value;   /* value to send into generator */
  int is_throw;           /* if true, throw send_value as exception */
  PyObject *throw_exc;    /* exception to throw (if is_throw) */
} KosPyAsyncGenASend;


#define KosPyAsyncGenASend_Check(obj)                                   \
  ((obj) && PyType_IsSubtype((obj)->ob_type, &KosPyAsyncGenASendType))

#define KosPyAsyncGenASend_CheckExact(obj)              \
  ((obj) && ((obj)->ob_type == &KosPyAsyncGenASendType))


PyObject *KosPyAsyncGenASend_New(KosPyAsyncGen *agen, PyObject *send_value);


/* === Unit lifecycle === */


int koskari_eval_init(PyObject *mod);
int koskari_eval_clear(PyObject *mod);


#endif


// The end.
