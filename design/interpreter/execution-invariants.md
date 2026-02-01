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

## 2. Single Evaluator, Stackful VM (Invariant)

* Koskari owns its **entire execution stack** as heap-resident frame objects.
* The evaluator runs inside **one C call** per drive/resume.
* Koskari never relies on CPython's recursive `ceval` re-entry for its own
  control flow.

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

## 13. Error and Cancellation Propagation (Decision)

* Python exceptions raised inside Koskari propagate outward normally.
* Cancellation (including Ctrl-C in sync mode) injects a cancellation exception
  into the Koskari VM.
* Python exception chaining (`__cause__`, `__context__`) is preserved across
  boundaries.

---

## 14. Non-Goals / Explicit Exclusions

* No implicit event loop creation on normal calls.
* No reliance on CPython frame recursion for Koskari semantics.
* No unification of `yield` and `await` signals.

---

## Summary Invariant

**Koskari owns control flow.**
Python observes execution only through explicit, protocol-correct projections
(value, generator, async generator, awaitable), while Koskari internally
executes with a single flattened evaluator and explicit suspension semantics.

---

## Related Documentation

* [evaluator.md](evaluator.md) — Legacy evaluation model from python-sibilant
  and sibilant-ceval
* [callcc.md](callcc.md) — Call/cc implementation design
* [../language/async.md](../language/async.md) — Async and coroutine language
  features
* [../language/generators.md](../language/generators.md) — Generator language
  features
