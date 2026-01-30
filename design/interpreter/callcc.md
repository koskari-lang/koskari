# Call/CC Implementation Design

## Problem Statement

The `call/cc` (call-with-current-continuation) feature was the blocking issue in sibilant-ceval. The challenge is supporting both:
- **Bounded continuations**: Continuations that can only be invoked within their original execution context
- **Unbounded continuations**: Continuations that can be invoked from anywhere, even after the original context has exited

## From sibilant-ceval Ideas

### Context Boundaries

**Ceval Context**: Thread-local stack of evaluation contexts
- Each context holds current frame stack
- Boundaries between separate ceval calls (koskari → native → koskari)
- Contexts track their frame and stack state

### Continuation Capture

**Capturing a continuation**:
1. Make copy of all koskari ceval stacks and frames at that point
2. Denote boundaries between separate ceval calls
3. Store frame state, stack state, and context boundary

### Continuation Invocation

**Rejoining a continuation**:
1. **Same boundary**: Drop current frames/stacks, restore from continuation
2. **Parent boundary**: Invalidate current boundary, raise special exception
   - Propagates to owning boundary
   - Resets state and restores from continuation

### Continuation Invalidation

**Continuation becomes invalid** when:
- Its ceval boundary exits
- Cannot re-return from CPython/native function calls
- Boundary that invoked the ceval is gone

## Proposed Architecture

### CevalContext

Thread-local stack of evaluation contexts:
```python
class CevalContext:
    frames: List[Frame]
    stack: Stack
    boundary_id: int  # Unique identifier for this boundary
    parent: Optional[CevalContext]
```

### Continuation

```python
class Continuation:
    frames: List[Frame]  # Snapshot of frames
    stack: Stack  # Snapshot of stack
    boundary_id: int  # Context boundary where captured
    valid: bool  # Whether still valid
```

### Call/CC Operation

1. **Capture**: Create Continuation with current context state
2. **Invoke**: 
   - Check if same boundary → restore directly
   - Check if parent boundary → raise boundary exception
   - Check if invalid → raise error

## Challenges

### JIT Compilation

- How do continuations work with JIT'd functions?
- Must JIT keep using KosStack/KosNamespace model?
- Or can we work with native stack frames?

### Performance

- Continuation capture is expensive (copying frames/stacks)
- Need efficient representation
- Consider lazy copying or copy-on-write

### Integration

- How do continuations interact with:
  - Generators?
  - Coroutines?
  - Exception handling?
  - Context managers?

## Migration Notes

### To Design

- CevalContext implementation
- Continuation representation
- Boundary tracking
- Invocation mechanism
- Invalidation strategy
- Integration with existing features

### To Research

- Scheme implementations of call/cc
- Delimited continuations (shift/reset)
- Continuation-passing style (CPS) transformation
- Performance characteristics

### Open Questions

- Should we support delimited continuations instead?
- Can we implement efficiently without copying everything?
- How to handle native/Python function boundaries?
- Should continuations be first-class or special form?
