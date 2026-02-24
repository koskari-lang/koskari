# DeferCoroutine (`defer`) — single-shot await-handle

**Implementation status**: Implemented in `koskari/lib/defer.c` and `koskari/lib/defer.h`; wired into the evaluator (push_or_await skips defer, AWAIT opcode drives via defer_drive). Tests in `tests/lib/test_defer.py`.

## Purpose

Koskari's default async model performs **implicit greedy await** in selected evaluation sites (notably function call return paths). This is convenient for "async-by-default" programming, but it removes an important capability: sometimes code needs a **handle** to something that will eventually be awaited, rather than the awaited result.

`defer` introduces a **single-shot, awaitable adapter**:

* It **does not invoke** its target function at construction time.
* On **first explicit await**, it invokes the target exactly once.
* If that invocation yields an awaitable, it **delegates** to it.
* If that invocation yields a value or raises, it completes immediately.
* It is **excluded from implicit auto-await**, so it can be passed around as a handle.

This makes "anything awaitable", lazily, while preserving Koskari's flattened/greedy default for ordinary calls.

---

## Surface syntax and usage

### Construction

```lisp
(defer f a b c)
(defer f a b key: val other: 2)
```

`defer` accepts all argument types: positional and keyword. It produces a `DeferCoroutine` that stores `fn`, `args` (positional list), and `kwds` (keyword dict) for application. The target is not invoked at construction time; application happens on first explicit `await`.

### Intended usage patterns

**Pass a handle without triggering auto-await**:

```lisp
(let [h (defer do-work x y)]:
  (store-handle h)
  ...
  (await h))
```

**Force an otherwise-greedy call site to return a handle**:

```lisp
;; if (foo ...) would auto-await and produce a value,
;; (defer foo ...) produces a handle instead.
(let [h (defer foo 1 2 3)]:
  (await h))
```

**Adapter for sync-or-async**:

```lisp
;; `maybe-async` may return either a value or an awaitable.
(await (defer maybe-async arg))
```

---

## Key semantics

### Explicit await

`DeferCoroutine` is **awaitable**. The explicit `await` special/opcode must drive it.

### Implicit greedy await

`DeferCoroutine` is **not auto-awaited** by implicit/greedy await sites.

Concretely, auto-await predicates differ:

* `await` accepts `DeferCoroutine` as awaitable.
* implicit-greedy-await sites treat `DeferCoroutine` as a plain value (no await).

This is the central design rule that makes `defer` a transportable handle.

---

## Lifecycle model (state machine)

A `DeferCoroutine` transitions through a small set of states.

### States

* **PENDING**

  * Holds `fn`, `args`, and `kwds` for application.
  * Has not invoked `fn`.

* **DELEGATING(inner)**

  * `fn(*args, **kwds)` has been invoked exactly once.
  * The return value was awaitable (`inner`).
  * `DeferCoroutine` delegates await-driving behavior to `inner`.

* **DONE(value)**

  * Completed successfully.
  * `value` is cached.

* **FAILED(exc)**

  * Completed exceptionally.
  * `exc` is cached.

* **MOVED_FROM** (optional internal state)

  * Donor object after adoption (see "defer-of-defer collapse").
  * Any await attempts should deterministically raise.

### Transitions

On **first explicit await** while in **PENDING**:

1. Invoke `fn(*args, **kwds)` **exactly once**.
2. Immediately drop references to `fn`, `args`, and `kwds` after invocation (to avoid retention).
3. Normalize the return:

   * If return is `DeferCoroutine`: adopt/merge state (see below) and re-normalize.
   * Else if return is awaitable: transition to **DELEGATING(inner)**.
   * Else: transition to **DONE(value)**.
4. If invocation raises: transition to **FAILED(exc)**.

While in **DELEGATING(inner)**:

* Await-driving operations are forwarded to `inner`.
* If `inner` completes with `value`: transition to **DONE(value)**.
* If `inner` raises: transition to **FAILED(exc)**.

Terminal states (**DONE**, **FAILED**) are immutable.

### Multiple awaits

The object is **single-shot in invocation**, not single-consumption of result:

* `fn(*args, **kwds)` is invoked at most once.
* Subsequent awaits do **not** re-invoke `fn`.
* Subsequent awaits return cached `DONE(value)` or re-raise cached `FAILED(exc)`.

This yields deterministic "joinable handle" behavior after resolution.

---

## Defer-of-defer collapse (generalization)

If a deferred invocation returns another `DeferCoroutine`, the outer `DeferCoroutine` **collapses** rather than wrapping.

### Rule

When normalizing the result of the deferred invocation:

* If `r` is a `DeferCoroutine`, perform `adopt(self, r)`:

  * Move `r`'s state into `self` (O(1) pointer/state transfer).
  * Transition `r` to **MOVED_FROM**.
  * Continue normalization on `self` (may be PENDING/DELEGATING/DONE/FAILED).

### Adoption behavior by donor state

If donor `r` is:

* **PENDING**: move donor's `fn`, `args`, and `kwds` into `self`; `self` remains effectively PENDING.
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

   * If PENDING: invoke the deferred call using a **raw-call primitive** that does not trigger implicit auto-await.
   * Normalize the return (including defer-of-defer adoption).
   * If DONE/FAILED: return/raise immediately.
   * If DELEGATING(inner): set `x = inner` and proceed.

2. Now `x` is the concrete await target:

   * If `x` is a Koskari-native coroutine/frame:

     * enter flattened VM evaluation until it yields/suspends or completes.
   * Else:

     * use the generic await bridge (Python awaitable protocol or equivalent).

3. On completion:

   * If the original awaited object was a `DeferCoroutine`, cache terminal DONE/FAILED on the defer wrapper.
   * Return value or raise exception.

### Important constraint

The "invoke deferred call" step must not pass through a path that performs implicit greedy await on its return value.

* This is why the deferred invocation uses a raw-call primitive.
* Delegation and explicit await then handle awaitables correctly.

---

## Memory / retention rules

To keep `defer` safe as a long-lived handle:

* After first invocation, clear stored `fn`, `args`, and `kwds` immediately.
* Only retain:

  * `inner` while delegating, or
  * cached terminal `value` / `exc`.

This prevents `DeferCoroutine` from pinning argument graphs indefinitely.

---

## Exceptions and cancellation

### Synchronous exceptions

If `fn(*args, **kwds)` raises during first await:

* transition to FAILED(exc)
* raise exc

### Awaited exceptions

If delegating to an inner awaitable that raises:

* transition to FAILED(exc)
* raise exc

### Cancellation / close / throw (if supported)

If the await protocol supports cancellation or throwing into awaits:

* While PENDING:

  * if cancellation is delivered before invocation, cancellation should be terminal (FAILED(Cancelled)).

* While DELEGATING(inner):

  * forward cancellation/throw/close to `inner`.
  * cache the resulting terminal state.

(The precise cancellation object/type is runtime-defined.)

---

## Debugging / introspection (recommended)

Minimal helpful behaviors:

* `repr` includes state:

  * `<defer pending fn=... args=N kwds=K>` (or similar)
  * `<defer delegating inner=...>`
  * `<defer done>`
  * `<defer failed exc=...>`

Optional:

* `started?`, `done?`
* `result()` / `exception()` for already-terminal objects

---

## Invariants

1. **Single invocation**: `fn(*args, **kwds)` is invoked at most once.
2. **No implicit wake-up**: implicit/greedy await sites do not auto-await `DeferCoroutine`.
3. **Explicit await drives**: explicit `await` always drives `DeferCoroutine`.
4. **Terminal immutability**: DONE/FAILED are immutable and cached.
5. **Adoption is O(1)**: defer-of-defer collapse performs state transfer without allocation.
6. **No retention after start**: after first invocation, `fn`, `args`, and `kwds` references are cleared.

---

## Implementation sketch (VM-side)

* New object type: `DeferCoroutine`

  * fields:

    * state tag
    * union payload:

      * pending: (fn, args, kwds)
      * delegating: inner
      * done: value
      * failed: exc

* Add helper functions:

  * `is_defer(obj)`
  * `defer_adopt(self, other)`
  * `defer_start(self)` (invokes fn with stored args/kwds once, normalizes, clears fn/args/kwds)

* Modify implicit call auto-await path:

  * `if is_awaitable(r) && !is_defer(r) => auto-await`

* Extend `AWAIT` opcode:

  * recognize and drive `DeferCoroutine`
  * normalize defers (including adoption)
  * fast-path Koskari-native coroutine awaits

---

## Open questions (explicitly deferred)

* Whether `MOVED_FROM` donors raise or mirror terminal state (recommended: raise to catch misuse).
* Whether there is a separate surface form for capturing call-site dynamic context (default: await-site context).
