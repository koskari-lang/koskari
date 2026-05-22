# DeferCoroutine (`defer`) — single-shot await-handle

**Implementation status**: Implemented in `koskari/lib/defer.c` and `koskari/lib/defer.h`; wired into the evaluator (push_or_await skips defer, AWAIT opcode drives via `KosDefer_Start`). Tests in `tests/lib/test_defer.py`.

## Purpose

Koskari's default async model performs **implicit greedy await** in selected evaluation sites (notably function call return paths). This is convenient for "async-by-default" programming, but it removes an important capability: sometimes code needs a **handle** to something that will eventually be awaited, rather than the awaited result.

`defer` introduces a **single-shot, awaitable adapter**:

* It **invokes** its target function at **construction** time (eager call).
* If that invocation yields an awaitable, the defer **delegates** to it until explicit await completes.
* If that invocation yields a plain value or raises, the defer is **terminal** immediately (DONE or FAILED).
* It is **excluded from implicit auto-await**, so it can be passed around as a handle even when the underlying call already ran.

This preserves Koskari's flattened/greedy default for ordinary calls while still allowing deferred **await** of async results.

---

## Surface syntax and usage

### Construction

```lisp
(defer f a b c)
(defer f a b key: val other: 2)
```

`defer` accepts all argument types: positional and keyword. It produces a `DeferCoroutine` that stores no pending call — `fn(*args, **kwds)` runs **during** `defer(...)` construction via a path that does not trigger implicit auto-await on the return value. The object is born in **DELEGATING**, **DONE**, or **FAILED** (or **MOVED_FROM** after adoption).

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

`DeferCoroutine` is **awaitable**. The explicit `await` special/opcode must drive it via `KosDefer_Start` (and `KosDefer_FeedResult` / `KosDefer_FeedException` when delegating).

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

  * Completed successfully at construction (sync result) or after await finished delegation.
  * `value` is cached.

* **FAILED(exc)**

  * Completed exceptionally at construction or after delegated await raised.
  * `exc` is cached.

* **MOVED_FROM** (internal state)

  * Donor object after adoption (see "defer-of-defer collapse").
  * Any await attempts should deterministically raise.

### Transitions at construction

On **`defer(fn, *args, **kwds)`** (via `KosDefer_New`):

1. Invoke `fn(*args, **kwds)` **exactly once** (no stored `fn` / `args` / `kwds` afterward).
2. Normalize the return:

   * If return is `DeferCoroutine`: adopt/merge state (see below) and re-normalize.
   * Else if return is awaitable: transition to **DELEGATING(inner)**.
   * Else: transition to **DONE(value)**.
3. If invocation raises: construction fails with that exception (no defer object).

### Transitions on explicit await

While in **DELEGATING(inner)**:

* `KosDefer_Start` suspends with `inner` as the yielded awaitable.
* If `inner` completes with `value`: **FeedResult** → **DONE(value)**.
* If `inner` raises: **FeedException** → **FAILED(exc)**.

While in **DONE** or **FAILED**:

* `KosDefer_Start` returns the cached value or re-raises immediately.

Terminal states (**DONE**, **FAILED**) are immutable after delegation completes.

### Multiple awaits

The object is **single-shot in invocation** (at construction), not single-consumption of result:

* `fn(*args, **kwds)` is invoked at most once (always at construction).
* Subsequent awaits do **not** re-invoke `fn`.
* Subsequent awaits return cached **DONE(value)** or re-raise cached **FAILED(exc)**.

This yields deterministic "joinable handle" behavior after resolution.

---

## Defer-of-defer collapse (generalization)

If a deferred invocation returns another `DeferCoroutine`, the outer `DeferCoroutine` **collapses** rather than wrapping.

### Rule

When normalizing the result of the construction-time invocation:

* If `r` is a `DeferCoroutine`, perform `adopt(self, r)`:

  * Move `r`'s state into `self` (O(1) pointer/state transfer).
  * Transition `r` to **MOVED_FROM**.
  * Continue normalization on `self` (may be DELEGATING/DONE/FAILED).

### Adoption behavior by donor state

If donor `r` is:

* **DELEGATING(inner)**: `self` becomes DELEGATING(inner).
* **DONE(value)**: `self` becomes DONE(value).
* **FAILED(exc)**: `self` becomes FAILED(exc).

### Rationale

* Prevents "wrapper onion" effects and preserves constant overhead.
* Preserves the single-shot guarantee: no additional invocations are introduced.
* Makes nested defers normalize to a single handle.

---

## Integration with `AWAIT` opcode (flattened Koskari-native fast path)

The `AWAIT` opcode/special should be able to **flatten** Koskari-native coroutines in-context (no generic bridging overhead).

### Conceptual algorithm

When executing `AWAIT` on operand `x`:

1. If `x` is `DeferCoroutine`:

   * Call `KosDefer_Start` (no construction-time call — already done).
   * If DONE/FAILED: return/raise immediately.
   * If DELEGATING(inner): set `x = inner` and proceed.

2. Now `x` is the concrete await target:

   * If `x` is a Koskari-native coroutine/frame:

     * enter flattened VM evaluation until it yields/suspends or completes.
   * Else:

     * use the generic await bridge (Python awaitable protocol or equivalent).

3. On completion (when the original operand was a delegating defer):

   * **FeedResult** or **FeedException** on the defer wrapper.
   * Return value or raise exception.

### Important constraint

The construction-time invocation must not pass through a path that performs implicit greedy await on its return value.

* Delegation and explicit await then handle awaitables correctly.
* Auto-await at CALL boundaries must skip `DeferCoroutine` results.

---

## Memory / retention rules

`DeferCoroutine` never retains `fn`, `args`, or `kwds` after construction:

* Only retain:

  * `inner` while delegating, or
  * cached terminal `value` / `exc`.

This prevents pinning argument graphs after the eager call completes.

---

## Exceptions and cancellation

### Synchronous exceptions

If `fn(*args, **kwds)` raises during construction:

* the exception propagates from `defer(...)`; no defer object is returned.

### Awaited exceptions

If delegating to an inner awaitable that raises:

* transition to FAILED(exc)
* raise exc on await

### Cancellation / close / throw (if supported)

If the await protocol supports cancellation or throwing into awaits:

* While DELEGATING(inner):

  * forward cancellation/throw/close to `inner`.
  * cache the resulting terminal state on the defer wrapper.

(The precise cancellation object/type is runtime-defined.)

---

## Debugging / introspection (recommended)

Minimal helpful behaviors:

* `repr` includes state:

  * `<defer delegating inner=...>`
  * `<defer done>`
  * `<defer failed exc=...>`
  * `<defer moved_from>`

Optional:

* `done?`
* `result()` / `exception()` for already-terminal objects

---

## Invariants

1. **Single invocation**: `fn(*args, **kwds)` is invoked at most once, at construction.
2. **No implicit wake-up**: implicit/greedy await sites do not auto-await `DeferCoroutine`.
3. **Explicit await drives**: explicit `await` drives delegation or returns cached terminal state.
4. **Terminal immutability**: DONE/FAILED are immutable and cached after delegation completes.
5. **Adoption is O(1)**: defer-of-defer collapse performs state transfer without allocation.
6. **No call retention**: `fn`, `args`, and `kwds` are never stored on the defer object.

---

## Implementation sketch (VM-side)

* Object type: `DeferCoroutine` (`KosDefer`)

  * fields:

    * state tag (DELEGATING | DONE | FAILED | MOVED_FROM)
    * union payload:

      * delegating: inner
      * done: value
      * failed: exc_type, exc

* Helpers:

  * `KosDefer_Check` / `KosDefer_CheckExact`
  * `defer_adopt(self, other)` (in defer.c)
  * `KosDefer_New(fn, args, kwds)` — invoke once, normalize
  * `KosDefer_Start` — await entry
  * `KosDefer_FeedResult` / `KosDefer_FeedException`

* Implicit call auto-await path:

  * `if is_awaitable(r) && !is_defer(r) => auto-await`

* `AWAIT` opcode:

  * recognize and drive `DeferCoroutine` via `KosDefer_Start`
  * feed terminal state back into defer when inner completes

---

## Open questions (explicitly deferred)

* Whether `MOVED_FROM` donors raise or mirror terminal state (recommended: raise to catch misuse).
* Whether there is a separate surface form for capturing call-site dynamic context (default: await-site context).
