# Evaluation Model

This document describes the Koskari evaluator implementation in `eval.c`/`eval.h`
and the supporting DynNode chain infrastructure in `dynchain.c`/`dynchain.h`.

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

**KosDynNode** — Base structure for all chain nodes:

```c
typedef enum KosDynKind {
  KOS_DYN_FRAME,   /* function activation */
  KOS_DYN_PROMPT,  /* reset delimiter */
  KOS_DYN_WIND,    /* dynamic-wind extent */
  KOS_DYN_REGION,  /* block-local control (try, with, loop) */
  KOS_DYN_PYCALL,  /* Python/C boundary marker */
} KosDynKind;

struct KosDynNode {
  KosDynNode *parent;   /* enclosing node (toward root) */
  KosDynKind kind;      /* discriminator for node type */
  uint32_t flags;       /* KosDynFlags bitfield */
};
```

**KosDynFrame** — Function activation record:

```c
struct KosDynFrame {
  KosDynNode base;        /* must be first */
  KosCode *code;          /* bytecode object (borrowed) */
  uint32_t pc;            /* program counter */
  KosStackBlock *stack;   /* operand stack (owned) */
  PyObject *ns;           /* namespace PyObject (owned ref) */
  uint32_t frame_flags;   /* frame-specific flags */
};
```

**KosDynPrompt** — Delimiter for delimited continuations (reset):

```c
struct KosDynPrompt {
  KosDynNode base;        /* must be first */
  uint32_t prompt_id;     /* tag for matching (0 = default) */
  KosCode *handler_code;  /* handler body code (borrowed, or NULL) */
  uint32_t handler_pc;    /* handler entry point */
};
```

**KosDynWind** — Dynamic-wind extent with hook blocks:

```c
struct KosDynWind {
  KosDynNode base;        /* must be first */
  KosCode *before_code;   /* before hook code (borrowed, or NULL) */
  uint32_t before_pc;     /* before hook entry point */
  KosCode *after_code;    /* after hook code (borrowed, or NULL) */
  uint32_t after_pc;      /* after hook entry point */
  uint8_t entered;        /* nonzero if before hook has run */
};
```

**KosDynRegion** — Block-local control (try/finally/loop/with):

```c
typedef enum KosRegionStyle {
  KOS_REGION_TRY,
  KOS_REGION_FINALLY,
  KOS_REGION_LOOP,
  KOS_REGION_WITH,
  KOS_REGION_FOREACH,
  KOS_REGION_AWAIT_WITH,
  KOS_REGION_AWAIT_FOREACH,
  KOS_REGION_NS,
  KOS_REGION_BEGIN,
} KosRegionStyle;

struct KosDynRegion {
  KosDynNode base;        /* must be first */
  KosRegionStyle style;   /* region type */
  uint32_t a_label;       /* primary jump target */
  uint32_t b_label;       /* secondary jump target (or 0) */
  uint8_t enabled;        /* handler active flag */
};
```

### KosContext — Top-level VM Execution State

```c
struct KosContext {
  KosDynNode *dyn_top;       /* top of dynamic chain */
  KosDynFrame *frame_top;    /* cached topmost frame (for fast access) */
  KosDynFrame *globals_frame;     /* cached outermost frame (for op_GLOBALS) */
  uint32_t globals_cache_version; /* chain_version when globals_frame was set */

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

  /* Exception injection for resume */
  int pending_throw;
  PyObject *pending_exc;

  /* Finally handler state: we entered a finally due to raise/return */
  int handling_raise;        /* 1 when in finally because of exception */
  PyObject *handling_result;  /* stashed return value (future return-through-finally) */

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

### KosEvent — Boundary Event from kos_drive

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

**KosPyContinuation**:
- Wraps `KosContinuation`
- Callable: returns `KosPyCoroutine` that resumes with the passed value
- Multi-shot: clones captured state on each invocation
- GC-tracked: traverses captured chain

**KosPyCoroutine**:
- Wraps `KosContext` for Python coroutine protocol
- Implements `__await__`, `send`, `throw`, `close`
- State machine: NEW → RUNNING → WAITING_PY → DONE

**KosPyAsyncGen** / **KosPyAsyncGenASend**:
- Wraps `KosContext` for async generator protocol
- `__aiter__` returns self
- `__anext__` returns `KosPyAsyncGenASend` awaitable

## Public C API

### DynNode Chain Management

```c
/* Push/pop frames */
KosDynFrame *kos_dyn_push_frame(KosContext *ctx, KosCode *code, PyObject *ns);
int kos_dyn_pop_frame(KosContext *ctx);

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
int kos_drive(KosContext *ctx, KosEvent *out_ev);
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
   wrapper (`KosPyCoroutine` or `KosPyAsyncGen`) during its lifetime.

2. **Chain ownership**: The dynamic chain is owned by the context. On
   continuation capture, chains are cloned for multi-shot semantics.

3. **Suspension**: When `kos_drive` returns `KOS_EV_AWAIT`, the context's
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
- [callcc.md](callcc.md) — Delimited continuation implementation details
- [execution-invariants.md](execution-invariants.md) — Execution model invariants
- [bytecode.md](bytecode.md) — Bytecode format and opcodes
- [../language/control-flow.md](../language/control-flow.md) — Language-level
  control flow including reset/shift
