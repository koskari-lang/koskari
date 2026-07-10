# DeferCoroutine (`defer`) — single-shot await-handle

**Implementation status**: Implemented in `koskari/lib/defer.c` and `koskari/lib/defer.h`; wired into the evaluator (push_or_await skips defer; AWAIT and Python await both drive through `__await__`, which consumes the handle). Tests in `tests/lib/test_defer.py`.

## Purpose

Koskari's default async model performs **implicit greedy await** in selected evaluation sites (notably function call return paths). This is convenient for "async-by-default" programming, but it removes an important capability: sometimes code needs a **handle** to something that will eventually be awaited, rather than the awaited result.

`defer` introduces a **single-shot, awaitable adapter**:

* It **invokes** its target function at **construction** time (eager call).
* If that invocation yields an awaitable, the defer **delegates** to it until explicit await completes.
* If that invocation yields a plain value, the defer is born **DONE**; if it raises, construction fails with that exception.
* It is **excluded from implicit auto-await**, so it can be passed around as a handle even when the underlying call already ran.

This preserves Koskari's flattened/greedy default for ordinary calls while still allowing deferred **await** of async results.

---

## Surface syntax and usage

### Construction

```lisp
(defer f a b c)
(defer f a b key: val other: 2)
```

`defer` accepts all argument types: positional and keyword. It produces a `DeferCoroutine` that stores no pending call — `fn(*args, **kwds)` runs **during** `defer(...)` construction via a path that does not trigger implicit auto-await on the return value. The object is born in **DELEGATING** or **DONE** (or adopts its donor's state, see defer-of-defer collapse).

### Intended usage patterns

**Pass a handle without triggering auto-await** (call already happened; await later):

```lisp
(let [h (defer do-work x y)]:
  (store-handle h)
  ...
  (await h))
```

**Force an otherwise-greedy call site to return a handle**:

```lisp
;; if (foo ...) would auto-await and produce a value,
;; (defer foo ...) produces a handle instead (foo already ran).
(let [h (defer foo 1 2 3)]:
  (await h))
```

**Adapter for sync-or-async**:

```lisp
;; `maybe-async` may return either a value or an awaitable.
;; defer invokes it immediately; await completes delegation if needed.
(await (defer maybe-async arg))
```

---

## Key semantics

### Explicit await

`DeferCoroutine` is **awaitable**. Explicit await — koskari's AWAIT opcode and Python's `await` alike — drives it through `__await__`, which consumes the handle (see the lifecycle model below).

### Implicit greedy await

`DeferCoroutine` is **not auto-awaited** by implicit/greedy await sites.

Concretely, auto-await predicates differ:

* `await` accepts `DeferCoroutine` as awaitable.
* implicit-greedy-await sites treat `DeferCoroutine` as a plain value (no await).

This is the central design rule that makes `defer` a transportable handle.

---

## Lifecycle model (state machine)

A `DeferCoroutine` has no **PENDING** state. Construction runs the call and normalizes the result immediately.

### States

* **DELEGATING(inner)**

  * `fn(*args, **kwds)` was invoked at construction.
  * The return value was awaitable (`inner`).
  * `DeferCoroutine` delegates await-driving behavior to `inner`.

* **DONE(value)**

  * Completed successfully at construction (sync result).
  * `value` is held until the first await consumes it.

* **SPENT**

  * The handle has been awaited once and is consumed. No payload.
  * Any further await raises `RuntimeError("cannot reuse already awaited
    defer")`, matching finished-coroutine semantics.

* **MOVED_FROM** (internal state)

  * Donor object after adoption (see "defer-of-defer collapse").
  * Any await attempts deterministically raise (`"cannot await defer
    after move"`).

Construction failure raises from `defer(...)` directly; there is no
FAILED state and no cached exception.

### Transitions at construction

On **`defer(fn, *args, **kwds)`** (via `KosDefer_New`):

1. Invoke `fn(*args, **kwds)` **exactly once** (no stored `fn` / `args` / `kwds` afterward).
2. Normalize the return:

   * If return is `DeferCoroutine`: adopt/merge state (see below) and re-normalize.
   * Else if return is awaitable: transition to **DELEGATING(inner)**.
   * Else: transition to **DONE(value)**.
3. If invocation raises: construction fails with that exception (no defer object).

### Transitions on await (delegate-and-die)

Await always arrives through the single `__await__` slot (`defer_await`),
whether from Python or from koskari's AWAIT opcode
(`PyObject_GetAwaitable`).

While in **DELEGATING(inner)**:

* `__await__` obtains `inner.__await__()` (on failure the defer stays
  DELEGATING and the error propagates), drops `inner`, transitions to
  **SPENT**, and hands the iterator to the awaiting machinery. The defer
  plays no further part; results and exceptions flow from `inner` to the
  awaiter directly.

While in **DONE(value)**:

* `__await__` transfers `value` into a trivial one-shot iterator
  (StopIteration(value)) and transitions to **SPENT**.

While in **SPENT** or **MOVED_FROM**:

* `__await__` raises `RuntimeError` immediately.

### Multiple awaits

The object is single-shot in both directions:

* `fn(*args, **kwds)` is invoked at most once (always at construction).
* The result is consumed by at most one await; a second await raises
  `RuntimeError`, the same behavior as a finished coroutine.

A joinable, multi-await handle is a **Task** (`create-task`) or Future,
not a defer. See [defer-task-future-matrix.md](../defer-task-future-matrix.md).

---

## Defer-of-defer collapse (generalization)

If a deferred invocation returns another `DeferCoroutine`, the outer `DeferCoroutine` **collapses** rather than wrapping.

### Rule

When normalizing the result of the construction-time invocation:

* If `r` is a `DeferCoroutine`, perform `adopt(self, r)`:

  * Move `r`'s state into `self` (O(1) pointer/state transfer).
  * Transition `r` to **MOVED_FROM**.
  * Continue normalization on `self` (may be DELEGATING/DONE/SPENT/MOVED_FROM).

### Adoption behavior by donor state

If donor `r` is:

* **DELEGATING(inner)**: `self` becomes DELEGATING(inner).
* **DONE(value)**: `self` becomes DONE(value).
* **SPENT**: `self` becomes SPENT (as dead as the donor).
* **MOVED_FROM**: `self` becomes MOVED_FROM (nothing to adopt).

### Rationale

* Prevents "wrapper onion" effects and preserves constant overhead.
* Preserves the single-shot guarantee: no additional invocations are introduced.
* Makes nested defers normalize to a single handle.

---

## Integration with `AWAIT` opcode

The AWAIT opcode treats a defer as a generic awaitable: it calls
`PyObject_GetAwaitable`, which lands in `__await__` and consumes the
handle. From that point the eval's ordinary eager-step machinery drives
the obtained iterator; the defer itself is out of the picture. No
defer-specific opcode support exists or is needed.

### Important constraint

The construction-time invocation must not pass through a path that performs implicit greedy await on its return value.

* Delegation and explicit await then handle awaitables correctly.
* Auto-await at CALL boundaries must skip `DeferCoroutine` results.

---

## Memory / retention rules

`DeferCoroutine` never retains `fn`, `args`, or `kwds` after construction:

* Only retain:

  * `inner` while delegating, or
  * `value` while DONE (until the first await transfers it out).

SPENT and MOVED_FROM hold nothing. This prevents pinning argument graphs
after the eager call completes, and releases the payload at first await.

---

## Exceptions and cancellation

### Synchronous exceptions

If `fn(*args, **kwds)` raises during construction:

* the exception propagates from `defer(...)`; no defer object is returned.

### Awaited exceptions

If the delegated-to inner awaitable raises, the exception flows to the
awaiter directly; the defer is already SPENT and records nothing.

### Cancellation / close / throw

Once awaited, the awaiter holds the inner's iterator and cancellation,
throw, and close follow the ordinary awaitable protocol against it. An
un-awaited DELEGATING defer that is garbage-collected drops its inner
(closing an unstarted coroutine).

---

## Debugging / introspection (recommended)

Minimal helpful behaviors:

* `repr` includes state:

  * `<defer delegating inner=...>`
  * `<defer done>`
  * `<defer spent>`
  * `<defer moved_from>`

---

## Invariants

1. **Single invocation**: `fn(*args, **kwds)` is invoked at most once, at construction.
2. **No implicit wake-up**: implicit/greedy await sites do not auto-await `DeferCoroutine`.
3. **Explicit await consumes**: the first await hands off the payload and transitions to SPENT.
4. **Single consumption**: a second await raises `RuntimeError`, like a finished coroutine.
5. **Adoption is O(1)**: defer-of-defer collapse performs state transfer without allocation.
6. **No call retention**: `fn`, `args`, and `kwds` are never stored on the defer object.

---

## Implementation sketch (VM-side)

* Object type: `DeferCoroutine` (`KosDefer`)

  * fields:

    * state tag (DELEGATING | DONE | SPENT | MOVED_FROM)
    * union payload:

      * delegating: inner
      * done: value
      * spent / moved_from: (empty)

* Helpers:

  * `KosDefer_Check` / `KosDefer_CheckExact`
  * `defer_adopt(self, other)` (in defer.c)
  * `KosDefer_New(fn, args, kwds)` — invoke once, normalize
  * `defer_await` (`__await__` slot) — the single await entry; consumes

* Implicit call auto-await path:

  * `if is_awaitable(r) && !is_defer(r) && !isfuture(r) => auto-await`
    (see [defer-task-future-matrix.md](../defer-task-future-matrix.md)
    for the future exclusion)

* `AWAIT` opcode:

  * generic awaitable handling; `PyObject_GetAwaitable` reaches
    `__await__`, no defer-specific support

---

## Open questions (explicitly deferred)

* Whether there is a separate surface form for capturing call-site dynamic context (default: await-site context).

(Resolved 2026-07-10: `MOVED_FROM` donors raise on await; the DONE-caching
multi-await design was dropped in favor of single-shot delegate-and-die,
with Tasks/Futures as the multi-await handles.)
