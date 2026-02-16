# Evaluation Model

This document describes the Koskari evaluator implementation in `eval.c`/`eval.h`
and the supporting DynNode chain infrastructure in `dynchain.c`/`dynchain.h`.
The KosCoroutine type and `kos_run_code_as_coroutine` (the Python-facing
entry that creates a coroutine from code+namespace) live in `func.c`/`func.h`;
driving a KosCoroutine (send/await) invokes `koskari_eval()` from func.c.

## Design Philosophy

Koskari's evaluator implements:
- **Delimited multi-shot continuations** (reset/shift)
- **VM-native dynamic wind/unwind hooks** (bytecode blocks, not Python callables)
- **Flattened async execution**
- **Unified dynamic chain** for all control flow concerns

## Core VM Structures

### Dynamic Chain (DynNode)

The evaluator uses a unified **dynamic chain** that links all execution context:
frames, prompts, wind extents, and control regions. This replaces the previous
separate frame chain and handler chain.

**KosDynNode** — Single tagged-union struct for all chain node kinds. Base fields
`parent`, `kind`, `flags` are followed by a union `u` with variants: `frame`,
`prompt`, `cc`, `wind`, `region`, `ns`, `pycall`. Typedefs (KosDynFrame,
KosDynPrompt, KosDynWind, KosDynRegion, KosDynNs, KosDynCC) all refer to this
same struct. **All field access must use the macros** defined in dynchain.h;
do not touch union members or base fields directly. Node kinds: KOS_DYN_FRAME,
KOS_DYN_PROMPT, KOS_DYN_WIND, KOS_DYN_REGION, KOS_DYN_NS, KOS_DYN_CC,
KOS_DYN_PYCALL, KOS_DYN_YIELD_FROM (see yield-from-execution-model.md).

- **Frame** (u.frame): code (borrowed), pc, stack (owned), ns (owned), frame_flags.
  Macros: KosDynFrame_CODE, KosDynFrame_PC, KosDynFrame_STACK, KosDynFrame_NS, KosDynFrame_FLAGS.
- **Prompt** (u.prompt): prompt_id, handler_code, handler_pc.
  Macros: KosDynPrompt_ID, KosDynPrompt_HANDLER_CODE, KosDynPrompt_HANDLER_PC.
- **Wind** (u.wind): before_code, before_pc, after_code, after_pc, entered.
  Macros: KosDynWind_BEFORE_CODE, KosDynWind_BEFORE_PC, KosDynWind_AFTER_CODE, KosDynWind_AFTER_PC, KosDynWind_ENTERED.
- **Region** (u.region): style (KosRegionStyle: TRY, FINALLY, LOOP, WITH, FOREACH, etc.), break_pc, continue_pc, enabled, result, iter, exhausted; FINALLY also has handling_raise (1 when entered due to exception) and result holds stashed return value; FOREACH has iter_next_else_pc (else PC when ITER_NEXT gen exhausts; 0 = not set).
  Macros: KosDynRegion_STYLE, KosDynRegion_BREAK_PC, KosDynRegion_CONTINUE_PC, KosDynRegion_ENABLED, etc.
- **Ns** (u.ns): saved_ns. Macro: KosDynNs_SAVED_NS.
- **CC** (u.cc): resume_pc. Macro: KosDynCC_RESUME_PC.
- **Pycall** (u.pycall): no extra fields; marks Python/C boundary.

All nodes are allocated from a single-size pool in dynchain.c (one freelist for
all kinds). KosContinuation and KosContext have separate freelists/caches (see
dynchain.c and eval.c).

### KosContext — Top-level VM Execution State

```c
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

  /* Python bridge: await/yield suspend and resume */
  PyObject *py_await_iter;   /* await-iterator from __await__() */
  PyObject *py_last_yielded; /* last yielded object (debug/optional) */
  PyObject *py_resume_inject; /* value to push on next koskari_eval when resuming */
  uint8_t py_resume_kind;    /* 0=none, 1=await, 2=yield */

  /* Exception injection (set by coroutine_throw or dynchain inject) */
  int inject_mode;       /* 0=none, 1=pending_throw */
  PyObject *inject_value;  /* exception object when inject_mode==1 */

  /* Pending async gen (in-loop NEXT/ITER_NEXT): gen frame is attached, awaiting yield/return */
  void *pending_agen;        /* KosPyAsyncGen * when set */
  PyObject *pending_agen_default;
  int pending_agen_n;        /* 1 or 2 for NEXT; 0 for ITER_NEXT (exhausted path reads region via kos_find_region_for_loop) */

  uint32_t flags;            /* KosContextFlags */
};
```

### KosStackBlock — Storage-Only Operand Stack

Control flow metadata (block style, labels, enabled flag) has been moved to
`KosDynRegion` nodes. `KosStackBlock` is now purely storage:

```c
struct KosStackBlock {
  KosStackBlock *back;    /* previous block */
  size_t refcount;        /* for copy-on-write */
  size_t max;             /* allocated capacity */
  size_t len;             /* used count */
  PyObject *objs[0];      /* flexible array of values */
};
```

### KosEvent — Boundary Event from koskari_eval

```c
typedef enum KosEventKind {
  KOS_EV_RETURN,  /* normal completion */
  KOS_EV_RAISE,   /* exception propagation */
  KOS_EV_YIELD,   /* generator yield */
  KOS_EV_AWAIT,   /* suspension on Python awaitable */
} KosEventKind;

typedef struct KosEvent {
  KosEventKind kind;
  PyObject *payload;  /* new reference; caller owns it */
} KosEvent;
```

## Continuation Infrastructure

### KosContinuation — Captured Delimited Continuation

```c
struct KosContinuation {
  KosDynNode *captured_chain;   /* cloned chain segment */
  KosDynFrame *resume_frame;    /* frame to resume into */
  uint32_t resume_pc;           /* pc to resume at */
  KosStackBlock *resume_stack;  /* stack state (COW reference) */
  uint32_t prompt_id;           /* target prompt tag */
  uint8_t one_shot;             /* 0 = multi-shot, 1 = one-shot */
};
```

### KosTransferPlan — Continuation Transfer Execution

When a continuation is invoked, a transfer plan orchestrates the execution:

```c
typedef enum KosTransferStep {
  KOS_XFER_RUN_HOOK,  /* execute a wind/unwind hook block */
  KOS_XFER_INSTALL,   /* splice target chain segment */
  KOS_XFER_INJECT,    /* push resume value onto stack */
} KosTransferStep;

struct KosTransferPlan {
  KosTransferPlan *suspended;   /* outer plan if nested */
  KosTransferAction *actions;   /* action array */
  size_t action_count;          /* total actions */
  size_t action_index;          /* next action index */
  KosDynNode *original_target;  /* for chain-change detection */
  uint32_t chain_version;       /* version at plan creation */
  uint32_t resume_pc;           /* UINT32_MAX = not set; set for invocation */
  KosDynFrame *resume_frame;    /* frame to resume into (invocation only) */
  KosStackBlock *resume_stack; /* stack state at capture (invocation only) */
  PyObject *continuation_ref;  /* keep continuation alive until plan is freed */
};
```

### KosHookRunner — Hook Execution State

Hooks are bytecode blocks (not Python callables) executed with isolated stacks:

```c
struct KosHookRunner {
  KosCode *code;          /* hook bytecode (borrowed) */
  uint32_t pc;            /* hook program counter */
  KosStackBlock *scratch; /* scratch operand stack (owned) */
  int active;             /* nonzero if hook is running */
};
```

## Python-Facing Wrapper Types

**KosPyContinuation** (eval.c):
- Wraps `KosContinuation`
- Callable: returns `KosCoroutine` that resumes with the passed value
- Multi-shot: clones captured state on each invocation
- GC-tracked: traverses captured chain

**KosCoroutine** (func.c):
- Wraps `KosContext` for Python coroutine protocol
- Implements `__await__`, `send`, `throw`, `close`
- State machine: NEW → RUNNING → WAITING_PY → DONE
- Created by Expression/Function call (via `kos_run_code_as_coroutine`) or Continuation call; drive calls `koskari_eval()`

**KosPyAsyncGen** / **KosPyAsyncGenASend** (eval.c):
- Wraps `KosContext` for async generator protocol
- `__aiter__` returns self
- `__anext__` returns `KosPyAsyncGenASend` awaitable
- When driven from **within** a Koskari eval (e.g. `await-for-each`), the gen’s frame is **attached** to the current context and stepped in the same loop; when driven from Python (`__anext__`), `koskari_eval(agen->ctx, &ev)` is used directly.

### Synchronous adapters (run / iter)

For ease of use in **synchronous** Python code (e.g. tests, scripts) that does not already run an event loop, the C extension provides:

- **KosCoroutine.run()**: Runs the coroutine to completion using `asyncio.run(self)`. Callers can write `expr().run()` instead of `asyncio.run(expr())`.
- **KosPyAsyncGen.iter()**: Returns a synchronous iterator that drives the async generator by calling `asyncio.run(awaitable)` on each `__anext__` result, so `for x in agen.iter():` yields values without an explicit event loop.

**Warning**: These are **unsafe** convenience APIs. They must **not** be used when an event loop is already running (e.g. from inside async code or from another `run()` call), as that can cause deadlock. They are intended for tests and synchronous entry points only. The API docstrings and type docs carry this warning.

## In-loop flattening (same koskari_eval run)

To avoid recursive C calls and keep a single state machine:

1. **CALL (Koskari callables)**: When the callable is a `KosExpression` or `KosFunction`, the evaluator does **not** call Python or recurse into `koskari_eval`. It pushes a new frame with `kos_dyn_push_frame(ctx, code, ns)` (with args applied for functions) and continues the dispatch loop. When that frame executes RETURN, the **frame-has-parent** rule (below) applies: the frame is detached and the return value is pushed onto the caller’s stack in the same loop.

2. **NEXT/ITER_NEXT (Koskari async gen)**: When the iterator is a Koskari async generator, the evaluator sets `pending_agen` and **attaches** the generator’s frame to the current context with `kos_dyn_attach_frame(ctx, gen_frame)`, then continues the loop. The gen’s frame runs until it YIELD or RETURN. On YIELD/RETURN, the **frame-has-parent** rule runs: the gen frame is **detached** (without freeing), the value (or exhausted/default) is delivered to the awaiting frame, and the loop continues with the caller as `frame_top`. No recursive `koskari_eval(agen->ctx, ...)`.

3. **YIELD/RETURN (frame has parent)**: If the current frame’s `base.parent` is non-NULL, the frame is a **child** (either from a Koskari CALL or from an attached async gen). YIELD and RETURN then **do not** produce a boundary event. They call `kos_dyn_detach_top_frame` to pop the current frame, push the value (or handle exhausted/exception) onto the parent’s stack, and return 0 so the loop continues with the parent. Only when the frame has **no** parent do YIELD/RETURN set `out_ev` and return from `koskari_eval`.

4. **Attach/detach (dynchain)**: `kos_dyn_attach_frame(ctx, frame)` reparents an existing frame onto `ctx->dyn_top` and sets it as the new top (no allocation). `kos_dyn_detach_top_frame(ctx, &frame_out)` removes the top frame from the chain without freeing it and returns it; used when the gen yields or returns so the frame can remain stored in the async gen’s context.

## Pushing results and auto-await

The evaluator **auto-awaits** only in two places:

1. **CALL (Python path)**: When the callable is a Python object and the call returns a coroutine/awaitable, the VM awaits it (eager step or suspend/resume) and pushes the final value; otherwise the call result is pushed as-is.
2. **AWAIT opcode**: The value on TOS is explicitly awaited; the VM suspends or injects the result.

All other opcodes push their result with **no** automatic await. That includes: CONST, VAR, GLOBALS, LOCALS, FAST, getattr, subscript, binary ops, yield-from yielded or final value, and any value that was already awaited or is not an awaitable. The implementation uses a push-only path (e.g. `push_result`) for every opcode except CALL's Python result and AWAIT. This avoids re-awaiting delegate results from YIELD_FROM and keeps semantics predictable.

## Public C API

### DynNode Chain Management

```c
/* Push/pop frames */
KosDynFrame *kos_dyn_push_frame(KosContext *ctx, KosCode *code, PyObject *ns);
int kos_dyn_pop_frame(KosContext *ctx);

/* Attach/detach existing frame (no allocation/free); for in-loop async gen stepping */
int kos_dyn_attach_frame(KosContext *ctx, KosDynFrame *frame);
int kos_dyn_detach_top_frame(KosContext *ctx, KosDynFrame **frame_out);

/* Push/pop prompts */
KosDynPrompt *kos_dyn_push_prompt(KosContext *ctx, uint32_t prompt_id);
void kos_dyn_pop_prompt(KosContext *ctx, KosDynPrompt *prompt);

/* Push/pop wind extents */
KosDynWind *kos_dyn_push_wind(KosContext *ctx,
        KosCode *before_code, uint32_t before_pc,
        KosCode *after_code, uint32_t after_pc);
int kos_dyn_pop_wind(KosContext *ctx, KosDynWind *wind);

/* Push/pop regions */
KosDynRegion *kos_dyn_push_region(KosContext *ctx,
        KosRegionStyle style, uint32_t label_a, uint32_t label_b);
void kos_dyn_pop_region(KosContext *ctx, KosDynRegion *region);

/* Find topmost enabled TRY or FINALLY region (for exception handling) */
KosDynRegion *kos_find_region_for_raise(KosContext *ctx);

/* Python boundary markers */
KosDynNode *kos_dyn_push_pycall(KosContext *ctx);
void kos_dyn_pop_pycall(KosContext *ctx, KosDynNode *marker);
```

### Continuation Capture and Invocation

```c
/* Find prompt by tag */
KosDynPrompt *kos_find_prompt(KosContext *ctx, uint32_t prompt_id);

/* Validate capture (checks for Python boundaries) */
int kos_validate_capture(KosContext *ctx, KosDynPrompt *prompt);

/* Capture continuation (shift) */
int kos_shift_capture(KosContext *ctx, uint32_t prompt_id,
        KosContinuation **out_k);

/* Invoke continuation (continuation_ref keeps k alive until plan is freed) */
int kos_invoke_continuation(KosContext *ctx,
        KosContinuation *k, PyObject *value, int is_throw,
        PyObject *continuation_ref);

/* Free or clone continuation */
void kos_continuation_free(KosContinuation *k);
KosContinuation *kos_continuation_clone(KosContinuation *k);
```

### Transfer Planning

```c
/* Compute LCA between two chain positions */
KosDynNode *kos_compute_lca(KosDynNode *a, KosDynNode *b);

/* Build transfer plan */
int kos_build_transfer_plan(KosContext *ctx,
        KosDynNode *target, PyObject *value, int is_throw,
        KosTransferPlan **out_plan);

/* Execute next step of transfer plan */
int kos_run_transfer_step(KosContext *ctx);

/* Free transfer plan */
void kos_free_transfer_plan(KosTransferPlan *plan);

/* Suspend/resume for nested transfers */
void kos_suspend_transfer_plan(KosContext *ctx);
int kos_resume_transfer_plan(KosContext *ctx);
```

### Hook Execution

```c
int kos_begin_hook(KosContext *ctx, KosCode *code, uint32_t pc);
int kos_hook_active(KosContext *ctx);
int kos_end_hook(KosContext *ctx);
```

### Context and Frame Lifecycle

```c
KosContext *kos_context_new(KosDynFrame *top);
void kos_context_free(KosContext *ctx);

/* Frame helpers (wrappers around kos_dynframe_new/clone/free) */
KosFrame *kos_frame_new(KosCode *code, PyObject *ns,
        KosStackBlock *stack, KosDynNode *parent);
KosFrame *kos_frame_clone(KosFrame *frame);
void kos_frame_free(KosFrame *frame);
```

### Call Operations

```c
int kos_koscall(KosContext *ctx, KosCode *code, PyObject *ns);
int kos_tail_koscall(KosContext *ctx, KosCode *code, PyObject *ns);
```

### Drive Loop

```c
int koskari_eval(KosContext *ctx, KosEvent *out_ev);
```

Main entry point: runs opcodes until boundary (RETURN/RAISE/YIELD/AWAIT).

### GC Helpers

```c
int kos_dynnode_traverse(KosDynNode *node, visitproc visit, void *arg);
int kos_dynchain_traverse(KosDynNode *node, KosDynNode *stop,
        visitproc visit, void *arg);
int kos_continuation_traverse(KosContinuation *k, visitproc visit, void *arg);
int kos_stackblock_traverse(KosStackBlock *block, visitproc visit, void *arg);
int kos_frame_traverse(KosFrame *frame, visitproc visit, void *arg);
int kos_context_traverse(KosContext *ctx, visitproc visit, void *arg);
```

## Ownership and Invariants

1. **Context ownership**: A `KosContext` is owned by exactly one Python
   wrapper (`KosCoroutine` or `KosPyAsyncGen`) during its lifetime.

2. **Chain ownership**: The dynamic chain is owned by the context. On
   continuation capture, chains are cloned for multi-shot semantics.

3. **Suspension**: When `koskari_eval` returns `KOS_EV_AWAIT`, the context's
   `py_await_iter` holds the iterator. Resumption injects result onto stack.

4. **Multi-shot**: Continuations clone all state on invocation, allowing
   multiple independent resumptions.

5. **GC safety**: Only Python-visible wrapper objects are GC-tracked.
   Internal C structs are traversed via wrapper `tp_traverse`.

6. **Python boundary**: Continuation capture rejects if any `KOS_DYN_PYCALL`
   boundary nodes exist between current position and the target prompt.

7. **Hook isolation**: Wind/unwind hooks execute with isolated operand stacks
   and cannot observe or modify the enclosing frame's stack.

## Wind/Unwind Ordering

On continuation transfer from `src` chain to `dst` chain:

1. **Unwind phase**: Execute `after` hooks from `src` toward LCA.
   - Order: innermost (top) to outermost (toward LCA).
   - Only WIND nodes with `entered = 1`.

2. **Install phase**: Splice `dst` chain segment onto current chain.

3. **Wind phase**: Execute `before` hooks from LCA toward `dst`.
   - Order: outermost (from LCA) to innermost (toward dst top).
   - Mark each WIND as `entered = 1` after its before hook completes.

This matches Scheme's `dynamic-wind` semantics.

## Related Documentation

- [evaluator-restructure.md](evaluator-restructure.md) — Design document for
  the DynNode chain restructure
- [introspection-trace.md](introspection-trace.md) — Read-only introspection
  and trace snapshot design (TraceView, KOS_OP_CAPTURE_FRAME)
- [callcc.md](callcc.md) — Delimited continuation implementation details
- [execution-invariants.md](execution-invariants.md) — Execution model invariants
- [bytecode.md](bytecode.md) — Bytecode format and opcodes
- [../language/control-flow.md](../language/control-flow.md) — Language-level
  control flow including reset/shift
