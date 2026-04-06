# Proposal: Raise KoskariSyncViolation at the await site (demand path)

**Archived.** The approach described here is **implemented** for
`demand_sync` / `Koskari_DemandSyncEx` when driving a **KosCallable** via
`run_eval`: on `KOS_EV_AWAIT` or `KOS_EV_YIELD`, `demand_sync_ex` injects
`KoskariSyncViolation` with a second `koskari_eval` (throw into the suspended
context) so `TRY`/`finally` can run. See `koskari/lib/demand.c` (`demand_sync_ex`).
**DemandSyncGenerator** stepping still surfaces await at the iterator boundary;
that path was explicitly out of scope for the in-eval inject.

---

This document originally proposed a change to how **await denial** works when
Koskari runs under a **demand** (no-suspend) host context: e.g. `demand_sync`,
slot shims that call `Koskari_DemandSyncEx`, and stepping a
**DemandSyncGenerator**.

**Status** (historical): proposal; now superseded by the implementation above.

---

## 1. Motivation

Today, when demand-scoped evaluation would suspend on a Python awaitable, the
evaluator returns a **`KOS_EV_AWAIT`** boundary event to the host adapter. The
adapter discards the temporary context and raises **`KoskariSyncViolation`**
from the Python/C wrapper (e.g. `demand_sync_impl`), **after** `koskari_eval`
has finished.

That design is correct for enforcing “no suspension” at the bridge, but it
does **not** give Koskari code running **inside** the demand call a chance to
handle the situation with normal exception machinery: **`try` / `except` /
`finally`**, cleanup, or a **synchronous fallback** when an await is denied.

The proposal is to surface the same failure as a **normal in-evaluator raise**
at the **instruction site** where suspension would have been signaled
(including **auto-await after `CALL`** when the callable’s result is awaitable —
**the same raise site** as an explicit **`AWAIT`** opcode). Uncaught, the
exception propagates up Koskari frames and is delivered to the host as
**`KOS_EV_RAISE`**, which existing demand adapters already translate to Python.
In the common case (no handler), behavior matches today’s outcome: failure
reaches the **call site** with `KoskariSyncViolation`, but with a traceback
anchored at the await (or post-call) site and with **intervening `finally`
blocks** and **local `except` handlers** able to run when authors provide them.

---

## 2. Current behavior (summary)

- **`push_or_await`** (and the **`AWAIT`** opcode path) may set **`KOS_EV_AWAIT`**
  and stop **`koskari_eval`** for the demand driver.
- **`demand_sync_ex`** maps **`KOS_EV_AWAIT`** to a suspension return code,
  frees the context, and does **not** set **`PyErr`** for that case.
- **`demand_sync_impl`** then raises **`KoskariSyncViolation`** at the **host**
  boundary.

No Koskari **`TRY`** region sees the event as an exception.

---

## 3. Proposed behavior

- Introduce a **per-context (or per–demand-root) flag**: execution is in a
  **no-suspend / demand** mode where an await must not suspend the evaluator
  toward an outer async driver.
- When **`push_or_await`** would otherwise produce **`KOS_EV_AWAIT`** **and**
  that flag is set, **do not** return **`KOS_EV_AWAIT`** to the demand driver.
  Instead, treat the condition like any other evaluator fault: set an
  exception (**`KoskariSyncViolation`**, or a Koskari-visible equivalent wired
  to it) and run the **existing raise / unwind / `TRY`** path so the “raise”
  appears **at the await site**.
- **Explicit `AWAIT`** and **auto-await after `CALL`** (coroutine or other
  awaitable result) **share this path** — one conceptual “await denied” site
  for both.
- **`demand_sync` / `Koskari_DemandSyncEx`**: after **`koskari_eval`**, **`KOS_EV_AWAIT`**
  for a plain demand invocation should become **unreachable** if every suspend
  attempt is converted in-eval. The adapter should handle **`KOS_EV_RAISE`**
  as today and **avoid duplicating** **`KoskariSyncViolation`** if **`PyErr`**
  is already set from the in-eval path.

---

## 4. Scope: demand_sync and DemandSyncGenerator

The same rule should apply to **any** evaluation advanced under demand-style
rules, including:

- **One-shot `demand_sync(callable, ...)`** (non-yielding KosFunction /
  Expression path).
- **`DemandSyncGenerator`**: each step that drives the underlying async
  generator / Koskari body should carry the same **no-suspend** semantics for
  **await**. If the body hits a denied await, the violation is raised **in
  that evaluation** before control returns to the Python iterator protocol, so
  **generator-local** cleanup and handlers can run where applicable.

Exact wiring (where the flag is stored on **`KosContext`** vs. inherited across
internal installs) is an implementation detail; the **user-visible** contract
is: **await denial is a catchable/unwindable condition inside Koskari** for
demand-scoped runs, not only a post-hoc error from the wrapper.

---

## 5. Semantics

- **`finally`**: runs on unwind like any other exception.
- **Nested calls**: an uncaught violation propagates through callers; a callee
  may catch and recover or re-raise.
- **Host boundary**: if the exception is not handled in Koskari, **`demand_sync`**
  (or equivalent) still fails, but the failure is **observed** as propagation
  from **`koskari_eval`** via **`KOS_EV_RAISE`**, not via a synthetic raise only
  in **`demand_sync_impl`**.

---

## 6. Compatibility and migration

- **Tracebacks** will differ: they will include the **await (or post-call)
  frame** and any Koskari handlers, not only the **`demand_sync`** frame.
- **Tests** that assert **`KoskariSyncViolation`** only at the outer wrapper may
  need updates.
- **`Koskari_DemandSyncEx`** C API: today **`1`** means “suspended.” After the
  change, demand-scoped eval may **never** return that code for an await;
  callers should be reviewed for reliance on **`1`** vs. **`-1`** with
  **`PyErr`** set.

---

## 7. Open questions

- Payload on the exception (e.g. whether to attach the awaitable or a message
  string for debugging).
- Whether **`demand_next`** (sync iteration boundary) should adopt an analogous
  in-eval story for consistency, or remain as specified in
  [dunder-adapter.md](dunder-adapter.md) until a follow-up.

---

## 8. Related documents

- [dunder-adapter.md](dunder-adapter.md) — `demand_sync`, `KoskariSyncViolation`,
  DemandSyncGenerator overview.
- [import-system.md](import-system.md) — demand path for module loading.
