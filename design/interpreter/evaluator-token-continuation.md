# Koskari Design: Evaluator Token + Continuation Application Model

**STATUS: IMPLEMENTED**

This document formalizes the design for continuation application, evaluator
identity, and cross-stack control transfer in Koskari.

---

## 1. Core Goal

Unify:

- Undelimited Continuations (Python-callable)
- Delimited Continuations (Koskari-only apply)
- Coroutine / Async Generator internal execution

Under a single evaluator-identity and apply model.

We must support:

1. Safe cross-C-stack continuation application (upwards-only when invoked from
   Python).
2. Direct, non-upwards continuation application when already inside the
   correct koskari_eval loop.
3. Strict prohibition of DelimitedContinuation application from Python.
4. Verifiable evaluator ownership and liveness.

---

## 2. Evaluator Identity Object

Introduce a refcounted PyObject:

```
KosEvaluatorToken
```

### Responsibilities

- Persistent identity of a koskari_eval domain/instance.
- Tracks whether the evaluator is currently active on this thread.
- Serves as ownership marker for Continuations and DelimitedContinuations.

### Required Fields

- `uint64_t cookie` — unique identity
- `KosDomain* domain` — runtime domain pointer (optional but recommended)
- `_Atomic int live_depth` — incremented on koskari_eval entry, decremented on
  exit
- (optional) thread identity — to prevent cross-thread apply

### Thread-Local State

Maintain TLS stack:

```
active_eval_stack = [token0, token1, ...]
```

On koskari_eval entry:

- push token
- token->live_depth++

On koskari_eval exit (all paths):

- pop token
- token->live_depth--

---

## 3. CapturedContext

CapturedContext is NOT a PyObject.

Represents snapshot of execution state:

- `Frame* frame_top`
- `Stack* value_stack`
- `Dyn* dyn_top`
- `KosEvaluatorToken* token`
- `uint64_t prompt_id` (for delimited only)

Two semantic categories:

1. **Living capture** (coroutines / async generators)

   - Context mutates in place.

2. **Templated capture** (continuations)

   - Apply clones frame/stack (COW-friendly).

---

## 4. Undelimited Continuation (Python-callable)

Python-visible object:

```
Continuation
```

Holds:

- CapturedContext cap
- KosEvaluatorToken* token

### `__call__` Semantics

If `token->live_depth == 0`:
  raise KoskariContinuationDead

Else:
  raise KoskariContinuationApply(target=token, cap=cap, args=...)

This is a BaseException subclass.

It forces unwinding of CPython frames until reaching the owning koskari_eval
loop.

---

## 5. koskari_eval Exception Handling

The koskari_eval loop must wrap its execution in a catch for:

```
KoskariContinuationApply
```

If `exc.target != current_token`:
  re-raise

Else:

- Extract CapturedContext + args
- Apply continuation into current live Context
- Resume evaluation

This enables upwards-only cross-C-stack application.

---

## 6. Direct Application Inside koskari_eval

CALL opcode must special-case:

- Continuation
- DelimitedContinuation

If `current_ctx->token == cont->token`:
  Apply directly (no exception path)

Else:
  Raise KoskariContinuationApply

Direct apply may perform full replace (undelimited) or splice (delimited).

---

## 7. DelimitedContinuation

Python-visible object:

```
DelimitedContinuation
```

Holds:

- CapturedContext cap
- KosEvaluatorToken* token
- uint64_t prompt_id

### `__call__`

Always:
  raise TypeError("DelimitedContinuation cannot be called from Python")

Application is only permitted via CALL opcode inside koskari_eval.

### Apply Validation

When applying:

1. `current_ctx->token == cap->token`
2. prompt_id exists in current dynamic chain

If either fails:
  raise KoskariContinuationError

---

## 8. Apply Modes

Apply operation must support:

1. **REPLACE_ALL** (undelimited)

   - Replace frame/dyn chain with captured state

2. **RELINK_DELIMITED**

   - Splice captured state at delimiter boundary

Templated captures always clone before application.

---

## 9. Safety Guarantees

- No continuation may apply into a dead evaluator.
- No continuation may cross domains.
- DelimitedContinuation may not be invoked from Python.
- Cross-stack continuation application is always upwards-only.
- Direct apply only occurs when already inside the owning evaluator.

---

## 10. CALL Opcode Responsibilities

CALL must detect:

- Continuation
- DelimitedContinuation
- KosCoroutine
- KosAsyncGen

And divert to the appropriate internal execution path instead of
PyObject_Call.

---

## 11. Design Invariant

Control transfer across C stack boundaries is performed exclusively via:

```
KoskariContinuationApply (BaseException)
```

All other continuation application occurs entirely within koskari_eval.

This preserves CPython stack safety while enabling full continuation semantics
inside Koskari.

---

## Related Documentation

- [callcc.md](callcc.md) — Delimited continuation implementation (shift/reset)
- [evaluator-restructure.md](evaluator-restructure.md) — DynNode chain restructure
- [evaluator.md](evaluator.md) — Overall evaluation model and API
- [execution-invariants.md](execution-invariants.md) — Semantic commitments
