# Evaluation Model

This document describes both the legacy evaluation approaches and the current
Koskari implementation in `eval.c`/`eval.h`.

## Legacy Approaches

### From python-sibilant

**CPython Evaluation**:
- Uses CPython's evaluation loop
- Koskari bytecode executed by Python interpreter
- Full Python interoperability
- Python tracebacks and debugging

**Trampoline TCO**:
- Functions marked with `@trampoline`
- Tail calls bounce out of frame
- Evaluated in trampoline loop
- Stack doesn't grow
- Side effect: collapsed call stack

### From sibilant-ceval

**Custom Ceval Loop**:
- Custom frame type (SibFrame)
- Custom stack type (SibStack)
- Custom namespace type (Namespace)
- Opcode dispatch loop

## Current Implementation (eval.c / eval.h)

The Koskari evaluator core implements:
- **Undelimited multi-shot continuations**
- **Dynamic wind/unwind hooks**
- **Flattened async execution**

### Core VM Structures (C-only, not PyObjects)

**KosHandler** — Dynamic wind/unwind chain node:
```c
struct KosHandler {
  KosHandler *parent;   /* enclosing handler (outer scope) */
  PyObject *before;     /* callable or NULL; invoked on wind-in */
  PyObject *after;      /* callable or NULL; invoked on unwind-out */
};
```

**KosFrame** — Execution activation record:
```c
struct KosFrame {
  KosFrame *parent;        /* caller frame */
  KosCode *code;           /* bytecode object (borrowed) */
  uint32_t pc;             /* program counter */
  KosStackBlock *stack;    /* operand stack (owned) */
  PyObject *ns;            /* namespace PyObject (owned ref) */
  KosHandler *handlers;    /* dynamic wind/unwind chain */
  uint32_t flags;          /* frame-specific flags */
};
```

**KosContext** — Top-level VM execution state:
```c
struct KosContext {
  KosFrame *top;            /* current frame */
  PyObject *py_await_iter;  /* await-iterator from __await__() */
  PyObject *py_last_yielded; /* last yielded (debug) */
  int pending_throw;        /* next resume throws */
  PyObject *pending_exc;    /* exception to raise */
  uint32_t flags;           /* RUNNING/FROZEN/DEAD */
};
```

**KosEvent** — Boundary event from kos_drive:
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

### Python-Facing Wrapper Types (GC-tracked PyObjects)

**KosPyContinuation**:
- Captures frame chain and handler state
- Callable: returns KosPyCoroutine
- Multi-shot: clones state on each invocation

**KosPyCoroutine**:
- Wraps KosContext for Python coroutine protocol
- Implements `__await__`, `send`, `throw`, `close`
- State machine: NEW → RUNNING → WAITING_PY → DONE

**KosPyAsyncGen** / **KosPyAsyncGenASend**:
- Wraps KosContext for async generator protocol
- `__aiter__` returns self
- `__anext__` returns KosPyAsyncGenASend awaitable

### Public C API

**Lifecycle**:
```c
KosContext *kos_context_new(KosFrame *top);
void kos_context_free(KosContext *ctx);
KosFrame *kos_frame_new(KosCode *code, PyObject *ns,
        KosStackBlock *stack, KosFrame *parent, KosHandler *handlers);
KosFrame *kos_frame_clone(KosFrame *frame);
void kos_frame_free(KosFrame *frame);
```

**Wind Chain Management**:
```c
int kos_push_wind(KosContext *ctx, PyObject *before, PyObject *after);
int kos_push_before_wind(KosContext *ctx, PyObject *before);
int kos_push_after_wind(KosContext *ctx, PyObject *after);
int kos_pop_wind(KosContext *ctx);
```

**Continuation Application**:
```c
int kos_apply_continuation(KosContext *ctx,
        KosFrame *target_top, KosHandler *target_handlers,
        int is_throw, PyObject *value_or_exc);
```

This function:
1. Computes LCA between current and target frames
2. Collects unwind-after hooks (inner→outer)
3. Collects wind-before hooks (outer→inner)
4. Executes unwind hooks
5. Installs target top/handlers into context
6. Executes wind hooks
7. Injects resume value onto target stack (or raises exception)

**Call Operations**:
```c
int kos_koscall(KosContext *ctx, KosCode *code, PyObject *ns);
int kos_tail_koscall(KosContext *ctx, KosCode *code, PyObject *ns);
```

**Drive Loop**:
```c
int kos_drive(KosContext *ctx, KosEvent *out_ev);
```

Main entry point: runs opcodes until boundary (RETURN/RAISE/YIELD/AWAIT).

**GC Helpers**:
```c
int kos_stackblock_traverse(KosStackBlock *block, visitproc visit, void *arg);
int kos_handler_traverse(KosHandler *handler, visitproc visit, void *arg);
int kos_frame_traverse(KosFrame *frame, visitproc visit, void *arg);
int kos_context_traverse(KosContext *ctx, visitproc visit, void *arg);
```

### Stack Management (stack.c / stack.h)

**KosStackBlock** — Block-structured operand stack:
```c
struct KosStackBlock {
  KosStackBlock *back;       /* previous block */
  KosBlockStyle block_style; /* determines signal handling */
  int enabled;               /* handler active flag */
  Py_ssize_t a_label;        /* primary jump label */
  Py_ssize_t b_label;        /* secondary label */
  size_t max;                /* allocated capacity */
  size_t len;                /* used count */
  PyObject *objs[0];         /* flexible array of values */
};
```

**KosStack** — Python wrapper for block chain:
- Provides Python-accessible stack operations
- Block push/pop for control flow
- Value push/pop/peek operations
- Supports block metadata (style, labels, enabled)

**Block Styles**:
- `KOS_BLOCK_BEGIN`: Default block
- `KOS_BLOCK_FINALLY`: Finally cleanup
- `KOS_BLOCK_FOREACH`: For-each iteration
- `KOS_BLOCK_LOOP`: While loop
- `KOS_BLOCK_NS`: Namespace scope
- `KOS_BLOCK_TRY`: Exception handler
- `KOS_BLOCK_WITH`: Context manager
- `KOS_BLOCK_AWAIT_FOREACH`: Async for-each
- `KOS_BLOCK_AWAIT_WITH`: Async context manager

### Ownership and Invariants

1. **Context ownership**: A KosContext is owned by exactly one Python
   wrapper (KosPyCoroutine or KosPyAsyncGen) during its lifetime.

2. **Frame ownership**: Frames are owned by the context. On continuation
   capture, frames are cloned for multi-shot semantics.

3. **Handler chain**: Each frame has its own handler chain. Handlers
   are allocated/freed with the frame or via push/pop_wind.

4. **Suspension**: When kos_drive returns KOS_EV_AWAIT, the context's
   py_await_iter holds the iterator. Resumption injects result onto stack.

5. **Multi-shot**: Continuations clone all state on invocation, allowing
   multiple independent resumptions.

6. **GC safety**: Only Python-visible wrapper objects are GC-tracked.
   Internal C structs (KosFrame, KosHandler, KosContext) are not.

### Eager-Step Awaitable Optimization

When the VM encounters `AWAIT(py_awaitable)`:

1. Obtain await-iterator via `__await__()`
2. Perform speculative step via `send(None)`
3. If immediate completion (StopIteration): inject value, continue VM
4. If yields: suspend, return yielded object to event loop

This avoids unnecessary event-loop hops for already-resolved awaitables.

### Future Optimizations (TODO)

- **Lazy frame COW**: Make pc mutation trigger copy-on-write so captured
  frame chains can share unchanged tail segments
- **Stack block cloning**: Deep-copy stack blocks for continuation capture
- **Frame pooling**: Reuse frame allocations

## Related Documentation

- [execution-invariants.md](execution-invariants.md) — Koskari execution model
  invariants and design commitments (target semantics)
- [callcc.md](callcc.md) — Continuation implementation details
- [../language/async.md](../language/async.md) — Async language features
