# Koskari Execution Model — Invariants & Decisions

This document records the agreed execution semantics, invariants, and boundary
policies for Koskari, intended for IDE/agent ingestion. These are *design
commitments*, not exploratory notes.

---

## 1. Dual Execution Perspectives (Invariant)

Koskari execution is observed from **two distinct perspectives**:

1. **Koskari View** — when Koskari bytecode evaluates and calls other Koskari
   functions.
2. **Python View** — when Python code calls into a Koskari callable.

These perspectives are projections over a *single evaluator*, not separate
runtimes.

---

## 2. Single Evaluator, Heap-Resident Stack (Invariant)

* Koskari is a bytecode-based evaluator implemented as a native C extension
  running inside CPython.
* Koskari owns its **entire execution stack** as heap-resident frame objects.
* The evaluator is internally **flattening (stackless)**: nested Koskari calls
  do not consume the C call stack; execution state is represented by
  heap-allocated linked structures. Dynamic nodes and frames are interleaved
  into a heap-allocated linked list representing execution history.
* The evaluator runs inside **one C call** per drive/resume.
* Koskari never relies on CPython's recursive `ceval` re-entry for its own
  control flow.
* The evaluator is driven by `koskari_eval()`, which is invoked when a
  KosCoroutine or KosPyAsyncGen is driven (e.g. via Python `await` or `send`).
  KosCoroutines are created by calling a KosExpression, KosFunction, or
  KosPyContinuation (each returns a new KosCoroutine).

This enables flattening of async suspension without C-stack growth.

---

## 3. Callable Surface Classification (Invariant)

Every Koskari callable has an explicit surface classification determining what
Python sees when it is called:

* **Sync Function** → returns a value or raises
* **Async Function** → returns an awaitable (coroutine-like)
* **Generator Function** → returns a generator
* **Async Generator Function** → returns an async generator

Python-visible behavior is *entirely determined* by this classification.

---

## 4. Internal Completion Signals (Invariant)

The evaluator produces **internal completion signals** that do not directly
correspond to CPython opcodes:

* `RETURN(value)` — computation completed
* `YIELD(value)` — generator yield
* `AWAIT(awaitable)` — suspension awaiting dependency
* `RAISE(exception)` — error propagation

These signals are consumed by the Koskari driver or wrapper layer.

---

## 5. AWAIT Is a VM Suspension, Not a Yield (Invariant)

* `AWAIT` **always suspends the entire Koskari VM immediately**.
* No partial unwinding or nested evaluator calls occur.
* Control returns directly to the current boundary:
  * a Python coroutine wrapper, or
  * a sync adapter runner.

This is a deliberate divergence from CPython's `YIELD_FROM` ladder.

---

## 6. Flattened Async Semantics (Invariant)

* Koskari async suspension is **flattened**:
  * no recursive C calls per `await`
  * no rebuilding of interpreter ladders on resume
* Resume re-enters the evaluator once, with the **entire Koskari stack intact**.

Await nesting depth affects heap frames only, never the C stack.

---

## 7. Python Awaitable Interop (Invariant)

* Koskari may await **Python awaitables** (coroutines, futures, etc.).
* These are treated as opaque state machines.
* Koskari does **not** recurse into CPython's await machinery.

Instead, Python awaitables are *iteratively stepped* by the boundary wrapper.

---

## 8. Await Probe Optimization (Decision → Invariant)

When Koskari encounters `AWAIT(py_awaitable)`:

1. The runtime obtains the await-iterator (`__await__`).
2. It performs **one speculative step** (`send(None)`).

Outcomes:

* **Immediate completion** (`StopIteration(value)`):
  * No suspension occurs.
  * `value` is injected back into the Koskari VM.

* **Yielded value**:
  * Suspension occurs.
  * The yielded object is passed to the Python event loop.
  * The await-iterator is stored for later resumption.

This preserves Python semantics while avoiding unnecessary event-loop hops.

---

## 9. Single Wrapper, Iterative Driving (Invariant)

From Python's perspective:

* Each async Koskari call produces **one wrapper object**.
* That wrapper:
  * resumes the Koskari VM
  * yields Python awaitables outward when needed
  * receives results/exceptions back

There is no recursive Python coroutine chain per nested Koskari `await`.

---

## 10. Sync Adapter Policy (Invariant)

* Blocking execution is **explicitly opt-in**.
* Sync execution is performed by a **dedicated event loop** created by Koskari.
* This loop drives the Koskari awaitable to completion.

Rules:

* If no Python event loop is running: use a local loop in-thread.
* If a loop is already running: use a helper thread with its own loop.
* Sync execution never happens implicitly.

---

## 11. Ownership and Re-entrancy (Invariant)

* A Koskari frame/awaitable has **exactly one active driver** at a time.
* Concurrent or re-entrant resume attempts are errors.
* Wrapper or runner objects enforce this invariant.

---

## 12. Generator vs Async Generator Separation (Invariant)

* Generators are **not awaitable**.
* Async generators are **not awaitable**.
* Async functions return awaitables.

Yielding and awaiting are disjoint control paths internally and at the surface.

---

## 12a. Control Flow (Invariant)

* Koskari supports **local-only jumps** restricted to the current block context.
* Local-only jumps implement continue, break, conditional control flow, and
  try/except/finally.
* **Non-local jumps** across block or dynamic extents are not permitted except
  via continuations.

---

## 13. Error and Cancellation Propagation (Decision)

* Python exceptions raised inside Koskari propagate outward normally.
* Cancellation (including Ctrl-C in sync mode) injects a cancellation exception
  into the Koskari VM.
* Python exception chaining (`__cause__`, `__context__`) is preserved across
  boundaries.

---

## 14. Python Boundary Constraint (Invariant)

* **Delimited continuations**: Captured continuations (shift) **MUST NOT include
  Python/C call stack segments**. Delimited capture may not cross the CPython C
  calling-convention boundary.
* **Full continuations**: May cross the CPython boundary only in the *upwards*
  direction (from callee toward caller). Crossing is implemented by raising a
  specialized `BaseException`, forcing a single return through intervening
  Python/C frames until the owning Koskari evaluator loop is reached.
* The dynamic chain tracks Python boundaries via `KOS_DYN_PYCALL` nodes.
* `shift` capture is **rejected** if any `PYCALL` boundary exists between the
  current position and the target prompt.
* Python and C frames are **not** embedded in the Koskari dynamic chain; only
  Koskari frames are re-enterable.

**Rationale**: Python call frames cannot be cloned or resumed. Including them
would create non-resumable continuations.

**Implementation**: `kos_validate_capture()` walks from `dyn_top` to the prompt,
checking for `PYCALL` nodes.

---

## 15. Unified Dynamic Chain (Invariant)

* All execution context (frames, prompts, winds, regions) is tracked in a
  **single unified dynamic chain**.
* The chain is a linked list of `KosDynNode` structures with typed payloads.
* Walking the chain provides complete dynamic extent information.

**Node types**:
* `KOS_DYN_FRAME` — function activation
* `KOS_DYN_PROMPT` — reset delimiter
* `KOS_DYN_WIND` — dynamic-wind extent
* `KOS_DYN_REGION` — block-local control (try, with, loop)
* `KOS_DYN_PYCALL` — Python/C boundary marker

---

## 16. VM-Native Wind/Unwind Hooks (Invariant)

* Wind/unwind hooks are **bytecode blocks** (implemented as null-arity Koskari
  callables that compile to bytecode), not Python callables.
* Hooks execute within the VM via `KosHookRunner` with isolated operand stacks.
* Hooks run in their own frames and capture lexical context via closure.
* Hooks terminate with `END_WIND` sentinel opcode.
* Hooks cannot observe or modify the enclosing frame's stack.
* **Local jumps and wind/unwind are distinct**: local jumps do *not* invoke
  wind/unwind handlers; continuation transfer *does* invoke them.

**Rationale**: This allows hooks to participate in continuation capture and
avoids Python/C boundary crossings during wind/unwind.

---

## 17. Continuation Semantics (Invariant)

* Koskari implements both **delimited continuations** (reset/shift) and
  **reusable full continuations** (call/cc).
* Delimited continuations are restricted to the same evaluator context.
* Continuations capture a reference to their owning evaluator context.
* Each evaluator context records an explicit lifecycle state (at least: active,
  sleeping/await/yield, exited). It is an error to apply a continuation whose
  owning context state does not permit invocation. Attempting to invoke an
  unusable continuation (delimited or full) raises an exception at the
  invocation site.
* Delimited continuations capture from the current point up to (but not
  including) the nearest matching prompt.
* Captured continuations are **multi-shot** by default.
* Continuation invocation executes proper wind/unwind hooks in correct order.

---

## 18. Transfer Plan Atomicity (Invariant)

* Continuation application is orchestrated via a **transfer plan**.
* The plan executes atomically with respect to normal VM execution.
* The VM does not interleave opcode dispatch with mid-transfer state.

**Exception**: Hooks may suspend on AWAIT, in which case the transfer plan is
suspended along with the context.

---

## 19. Chain Version Tracking (Invariant)

* The context maintains a `chain_version` counter.
* The counter increments on every chain modification (push/pop any node).
* Transfer plans record the version at creation time.
* Nested transfers that modify the chain are detectable via version mismatch.

**Use**: Enables safe resumption of suspended transfer plans after nested
continuation invocations.

---

## 20. Non-Goals / Explicit Exclusions

* No implicit event loop creation on normal calls.
* No reliance on CPython frame recursion for Koskari semantics.
* No unification of `yield` and `await` signals.
* No Python-callable wind/unwind hooks.

---

## Summary Invariant

**Koskari owns control flow.**
Python observes execution only through explicit, protocol-correct projections
(value, generator, async generator, awaitable), while Koskari internally
executes with a single flattened evaluator, a unified dynamic chain, and
delimited continuation semantics.

---

## Related Documentation

* [evaluator.md](evaluator.md) — Evaluation model and VM structures
* [callcc.md](callcc.md) — Delimited continuation implementation
* [evaluator-restructure.md](evaluator-restructure.md) — Design document for
  the DynNode chain restructure
* [../language/async.md](../language/async.md) — Async and coroutine language
  features
* [../language/generators.md](../language/generators.md) — Generator language
  features
* [../language/control-flow.md](../language/control-flow.md) — Control flow
  including reset/shift and dynamic-wind
