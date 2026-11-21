# DeferCoroutine (`defer`) — single-shot await-handle

**Implementation status**: Implemented in `koskari/lib/defer.c` and `koskari/lib/defer.h`; wired into the evaluator through the await suite (implicit sites take a defer as a plain value; a Koskari await hands a delegating defer's inner on to that inner's own await kind when it has one, spending the handle, and otherwise drives the defer through `__await__`; a Python await drives through `__await__`, which consumes the handle). Tests in `tests/lib/test_defer.py`.

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

`DeferCoroutine` is **awaitable**. Python's `await` drives it through `__await__`, which consumes the handle (see the lifecycle model below). Koskari's AWAIT opcode takes it on an await frame through the type's await kind: a DELEGATING handle whose inner carries an await suite of its own, a Koskari coroutine or an async generator stepper, is spent and the inner continues on the awaiter's chain under its own kind; any other handle is driven through `__await__` on that frame.

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
  * `value` is held until the first drive consumes it.

* **ITERATING(iter)**

  * The handle has started its delegation (`__await__` or a first
    `send`/`throw` on a DELEGATING defer) and holds the iterator obtained
    from `inner.__await__()`. Drives forward to it with yield-from rules.

* **SPENT**

  * The handle has been driven to completion (or closed) and is
    consumed. No payload.
  * Any further await/send/throw raises `RuntimeError("cannot reuse
    already awaited defer")`, matching finished-coroutine semantics.
    `close` on a SPENT handle stays a quiet no-op.

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

### The coroutine protocol (drivable handle)

`DeferCoroutine` carries the full coroutine protocol, so it passes the
structural `collections.abc.Coroutine` subclass hook and
`asyncio.iscoroutine` (note `inspect.iscoroutine`, which checks the
concrete type, stays False). asyncio's strict surfaces
(`asyncio.create_task`, `asyncio.run`, `run_coroutine_threadsafe`)
accept a defer directly, and `Task.get_coro()` is the defer itself.

* `__await__`: starts a DELEGATING handle's delegation (DELEGATING ->
  ITERATING; on failure it stays DELEGATING and the error propagates) and
  returns **self**. DONE and ITERATING also return self. SPENT/MOVED_FROM raise
  RuntimeError. `tp_iternext` exists (send(None) semantics) so
  CPython's GET_AWAITABLE iterator check passes; there is deliberately
  NO `tp_iter`, so `iter(d)` and koskari iteration constructs reject
  the handle instead of silently driving it. `yield-from` explicitly
  rejects defers alongside coroutines (YIELD_FROM.c).
* `send(v)`: DONE consumes -- v must be None (else the just-started
  TypeError), raises `StopIteration(value)` built as a real instance,
  transitions SPENT. DELEGATING starts its delegation lazily, then
  forwards like ITERATING: use the delegate's `send` when present; a
  None send falls back to `tp_iternext`; a non-None send without a send
  method is a TypeError (state unchanged). Completion (StopIteration) or
  any raise from the delegate consumes the handle (SPENT) and propagates.
* `throw(typ[, val[, tb]])`: DONE drops the value, goes SPENT, and
  raises. DELEGATING starts its delegation lazily, then forwards to the
  delegate's `throw` (single-arg calls forward single-arg). A throw-less
  delegate follows yield-from rules: on GeneratorExit its `close` is
  called first; either way the handle goes SPENT and the exception
  raises at the defer. SPENT/MOVED_FROM raise the reuse RuntimeError.
* `close()`: quiet no-op on SPENT/MOVED_FROM; consumes DONE. For
  DELEGATING/ITERATING it delegates to the payload's `close` when
  present (an unstarted inner coroutine closes without the
  never-awaited warning) and PROPAGATES errors from it -- that is what
  surfaces "coroutine ignored GeneratorExit". The handle is consumed
  either way.

Method lookups on the delegate distinguish lookup-AttributeError from an
AttributeError raised inside the delegated body (lookup-then-call), so a
real error is never mistaken for a missing method.

### Multiple drives

The object is single-shot in both directions:

* `fn(*args, **kwds)` is invoked at most once (always at construction).
* The result is consumed by at most one drive to completion; further
  use raises `RuntimeError`, the same behavior as a finished coroutine.

A joinable, multi-await handle is a **Task** (`create-task`) or Future,
not a defer. See [defer-task-future-matrix.md](../archive/defer-task-future-matrix.md).

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
* **ITERATING(iter)**: `self` becomes ITERATING(iter) and resumes the
  drive where the donor left off.
* **SPENT**: `self` becomes SPENT (as dead as the donor).
* **MOVED_FROM**: `self` becomes MOVED_FROM (nothing to adopt).

### Rationale

* Prevents "wrapper onion" effects and preserves constant overhead.
* Preserves the single-shot guarantee: no additional invocations are introduced.
* Makes nested defers normalize to a single handle.

---

## Integration with `AWAIT` opcode

The AWAIT opcode treats a defer as a generic awaitable: it calls
`PyObject_GetAwaitable`, which arrives at `__await__`, starts the handle's
delegation, and returns the defer itself as the drive target. The eval's
ordinary eager-step machinery then drives it via `send`, which forwards
into the held iterator. No defer-specific opcode support exists or is needed.

### Important constraint

The construction-time invocation must not pass through a path that performs implicit greedy await on its return value.

* Delegation and explicit await then handle awaitables correctly.
* Auto-await at CALL boundaries must skip `DeferCoroutine` results.

---

## Memory / retention rules

`DeferCoroutine` never retains `fn`, `args`, or `kwds` after construction:

* Only retain:

  * `inner` while delegating,
  * `iter` while iterating, or
  * `value` while DONE (until the first drive consumes it).

SPENT and MOVED_FROM hold nothing. This prevents holding argument graphs
after the eager call completes, and releases the payload at completion.

---

## Exceptions and cancellation

### Synchronous exceptions

If `fn(*args, **kwds)` raises during construction:

* the exception propagates from `defer(...)`; no defer object is returned.

### Awaited exceptions

If the delegated-to inner awaitable raises, the exception flows to the
awaiter directly; the defer is already SPENT and records nothing.

### Cancellation / close / throw

The defer itself is the drive target: `Task.cancel()` arrives as a
`throw(CancelledError)` on the defer and forwards into the held
iterator; `close()` likewise delegates. An un-awaited DELEGATING defer
that is garbage-collected drops its inner (closing an unstarted
coroutine).

---

## Debugging / introspection (recommended)

Minimal helpful behaviors:

* `repr` includes state:

  * `<koskari defer delegating inner=...>`
  * `<koskari defer done>`
  * `<koskari defer iterating>`
  * `<koskari defer spent>`
  * `<koskari defer moved_from>`

---

## Invariants

1. **Single invocation**: `fn(*args, **kwds)` is invoked at most once, at construction.
2. **No implicit wake-up**: implicit/greedy await sites do not auto-await `DeferCoroutine`.
3. **Driving consumes**: completion, failure, or close transitions to SPENT.
4. **Single consumption**: reuse raises `RuntimeError`, like a finished coroutine.
5. **Adoption is O(1)**: defer-of-defer collapse performs state transfer without allocation.
6. **No call retention**: `fn`, `args`, and `kwds` are never stored on the defer object.
7. **Coroutine-shaped**: passes `asyncio.iscoroutine` structurally; not
   iterable (`iter(d)` and `yield-from` reject it).

---

## Implementation sketch (VM-side)

* Object type: `DeferCoroutine` (`KosDefer`)

  * fields:

    * state tag (DELEGATING | DONE | ITERATING | SPENT | MOVED_FROM)
    * union payload:

      * delegating: inner
      * done: value
      * iterating: iter
      * spent / moved_from: (empty)

* Helpers:

  * `KosDefer_Check` / `KosDefer_CheckExact`
  * `defer_adopt(self, other)` (in defer.c)
  * `KosDefer_New(fn, args, kwds)` — invoke once, normalize
  * `defer_await` (`__await__` slot) — arm and return self
  * `defer_do_send` / `defer_forward_send` / `defer_forward_throw` —
    the drive core with yield-from delegation rules

* Implicit call auto-await path:

  * `if is_awaitable(r) && !is_defer(r) && !isfuture(r) => auto-await`
    (see [defer-task-future-matrix.md](../archive/defer-task-future-matrix.md)
    for the future exclusion)

* `AWAIT` opcode:

  * generic awaitable handling; `PyObject_GetAwaitable` reaches
    `__await__`, no defer-specific support; `yield-from` rejects
    defers explicitly (YIELD_FROM.c yield_from_check_coro)

---

## Open questions (explicitly deferred)

* Whether there is a separate surface form for capturing call-site dynamic context (default: await-site context).

(Resolved 2026-07-10: `MOVED_FROM` donors raise on await; the DONE-caching
multi-await design was dropped in favor of single-shot delegate-and-die,
with Tasks/Futures as the multi-await handles.)
