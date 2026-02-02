# Call/CC Implementation Design

This document describes the continuation implementation in Koskari, as realized
in `eval.c`/`eval.h`.

## Design Decisions (Implemented)

### Undelimited Continuations Only

For the initial implementation, Koskari supports **undelimited continuations**:
- Continuations capture the entire call stack from root to current frame
- No prompt/delimiter mechanism (shift/reset not yet supported)
- Simpler semantics matching traditional Scheme `call/cc`

### Multi-Shot Only

All continuations are **multi-shot** (reusable):
- Each invocation clones the captured state
- No single-shot/consumptive optimization
- Enables multiple independent resumptions of the same continuation

### Wind/Unwind Hooks

Dynamic wind/unwind behavior via `KosHandler` chain:
- `before` callable: invoked when entering dynamic extent
- `after` callable: invoked when leaving dynamic extent
- Either may be NULL; convenience APIs for single-hook cases

## Implementation (eval.c)

### KosPyContinuation Type

Python-facing continuation wrapper:

```c
typedef struct {
  PyObject_HEAD
  KosFrame *captured_top;       /* cloned frame chain */
  KosHandler *captured_handlers; /* cloned handler chain */
} KosPyContinuation;
```

**Behavior**:
- Callable from Python
- `tp_call(value)` returns a `KosPyCoroutine` that resumes with `value`
- Multi-shot: clones captured state on each call
- GC-tracked: traverses frame and handler chains

### Continuation Capture

To capture a continuation from within the VM:

1. Clone the current frame chain via `kos_frame_clone()`
2. Clone the handler chain via `kos_handler_clone()`
3. Create `KosPyContinuation` with cloned state

```c
PyObject *KosPyContinuation_New(KosFrame *top, KosHandler *handlers);
```

### Continuation Application

The `kos_apply_continuation()` function handles resumption:

```c
int kos_apply_continuation(KosContext *ctx,
        KosFrame *target_top, KosHandler *target_handlers,
        int is_throw, PyObject *value_or_exc);
```

**Algorithm**:

1. **Compute LCA**: Find lowest common ancestor between current and target frames
   ```c
   static KosFrame *compute_frame_lca(KosFrame *current, KosFrame *target);
   ```

2. **Collect unwind hooks**: Gather `after` handlers from current to LCA (inner→outer)
   ```c
   static PyObject **collect_unwind_hooks(KosFrame *current, KosFrame *lca,
           size_t *out_count);
   ```

3. **Collect wind hooks**: Gather `before` handlers from LCA to target (outer→inner)
   ```c
   static PyObject **collect_wind_hooks(KosFrame *target, KosFrame *lca,
           size_t *out_count);
   ```

4. **Execute unwind hooks**: Call each `after` handler (exceptions swallowed)

5. **Install target state**: Set `ctx->top` to target frame chain

6. **Execute wind hooks**: Call each `before` handler (exceptions propagate)

7. **Inject resume value**: Push value onto target stack, or set pending exception

### Handler Chain Management

```c
/* Push a wind handler with before and/or after hooks */
int kos_push_wind(KosContext *ctx, PyObject *before, PyObject *after);

/* Convenience: push with only before hook */
int kos_push_before_wind(KosContext *ctx, PyObject *before);

/* Convenience: push with only after hook */
int kos_push_after_wind(KosContext *ctx, PyObject *after);

/* Pop topmost handler (does not invoke hooks) */
int kos_pop_wind(KosContext *ctx);
```

### Wind/Unwind Semantics

Wind hooks (`before`) are invoked when:
- Entering a dynamic-wind protected region normally
- Resuming into a region via continuation application

Unwind hooks (`after`) are invoked when:
- Exiting a dynamic-wind protected region normally
- Leaving a region via continuation application
- Exception propagation crosses the region boundary
- Return crosses the region boundary

This matches Scheme's `dynamic-wind` semantics.

## Interaction with Async

Continuations interact with async execution:

1. **Capture during await**: Continuation captures suspended await state
2. **Resume during await**: Continuation can resume into awaiting context
3. **Python awaitable bridging**: `py_await_iter` state preserved in context

The `KosPyCoroutine` wrapper handles the async protocol while the underlying
continuation machinery manages control flow.

## GC Considerations

**GC-tracked objects**:
- `KosPyContinuation`: traverses captured frames and handlers
- `KosPyCoroutine`: traverses context, pending await state

**Not GC-tracked**:
- `KosFrame`, `KosHandler`, `KosContext` (internal C structs)
- Stack blocks (`KosStackBlock`)

Wrapper `tp_traverse` implementations walk all reachable PyObjects:
- Frame chain: namespace, stack values
- Handler chain: before/after callables
- Context: await iterator, pending exception

## Future Work

### Delimited Continuations

Consider adding prompt-delimited continuations (shift/reset):
- Require prompt markers in frame chain
- Capture only up to prompt, not full stack
- More composable than undelimited continuations

### Lazy Frame COW

Optimize continuation capture with copy-on-write:
- Share unchanged frame tail segments
- Clone only on mutation (pc update, stack modification)
- Reduces capture cost for deep stacks

### Stack Block Cloning

Currently TODO: deep-copy stack blocks for continuation capture.
Required for full continuation semantics.

## Related Documentation

- [evaluator.md](evaluator.md) — Overall evaluation model and API
- [execution-invariants.md](execution-invariants.md) — Semantic commitments
- [../language/control-flow.md](../language/control-flow.md) — Language-level control flow
