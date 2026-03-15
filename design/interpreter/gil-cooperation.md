# GIL Cooperation: Domain-TLS Counter and Timer

This document describes a design for voluntary GIL release in the Koskari evaluator and related C code, using a **counter + timer** policy with **Domain** as the storage for cooperation state and the existing **domain TLS** for zero-argument, call-site-friendly APIs.

**Status**: Design proposal. Current implementation uses a simple opcode-interval counter (every 255 dispatches) and an on-exit release; this design generalizes to time-based fairness and cooperation in GIL-free zones.

---

## 1. Goals

- **Be kind to other threads**: Allow other Python threads to run periodically during long eval runs and during long stretches of C work that do not touch Python objects.
- **Low overhead**: The hot path (no release) should cost at most a decrement, a compare, and a branch; timer reads only when the counter hits zero.
- **No API burden**: Call sites should not need to pass context or domain; cooperation should be callable from anywhere that runs under the evaluator (eval loop, dynchain, stack, etc.).
- **Time-based fairness**: When we do cooperate, the decision should consider wall time (e.g. "every ~5 ms") so that heavy opcodes do not starve other threads.

---

## 2. Overview

- **Storage**: Cooperation state (counter, last-release timestamp, interval) lives on the **Domain**. The Domain is the evaluator-level owner of this policy; it is already the ambient context and is unique per evaluator scope.
- **Resolving the domain**: `maybe_cooperate()` and `ensure_gil()` take **no arguments**. They use `kos_domain_current()` to obtain the active domain (which in turn uses the existing TLS scope stack). When not in an eval (no current domain), they no-op.
- **Counter + timer hybrid**: Every **N** calls to `maybe_cooperate()`, we check a **monotonic timer**. If at least `cooperate_interval_ns` has passed since the last release, we release the GIL (and optionally continue executing in the current function without re-acquiring until we hit a boundary). The counter amortizes the cost of the timer check: the hot path is decrement + compare + branch; the timer is read only every N calls.
- **Liberal placement**: `maybe_cooperate()` is called in many places—not only at the eval loop boundary but inside GIL-free or mostly-GIL-free zones (chain walks, transfer plan build, stack copy). When the timer fires in one of those zones, we release and keep doing C work; other threads run while we continue. We only re-acquire at a boundary (e.g. `ensure_gil()` or the next use of a PyObject* / Python API).

---

## 3. State and API

### 3.1 Cooperation state (on Domain)

Stored in or alongside the Domain (e.g. new fields on the KosDomain C struct, or a small C struct pointed to by the domain for C-only state):

- **counter** (unsigned, e.g. 0..255): Decremented (or incremented) each time `maybe_cooperate()` is called. When it reaches 0, we run the "check timer and maybe release" logic, then reset to N.
- **last_cooperate_ts**: Monotonic timestamp (nanoseconds) of the last time we released the GIL. Used to enforce a minimum interval between releases.
- **cooperate_interval_ns**: Configurable interval (e.g. 5_000_000 for 5 ms). Only release if `now - last_cooperate_ts >= cooperate_interval_ns`.

All state is C-side; no Python API is used to read or write it.

### 3.2 Public API (no arguments)

- **`maybe_cooperate()`**: Call at cooperation points. Decrements the counter; when counter hits 0, reads monotonic time, and if the interval has elapsed, releases the GIL (Py_BEGIN_ALLOW_THREADS). Does **not** re-acquire; the caller continues. If no current domain, or counter not zero, or interval not elapsed, returns without doing anything. Resets counter to N after a release.
- **`ensure_gil()`**: Call when entering a section that must hold the GIL (or after re-acquiring). Re-acquires the GIL if the current thread had released it (Py_END_ALLOW_THREADS in the paired sense—see below), and sets `last_cooperate_ts = now` so the next N `maybe_cooperate()` calls will not release until the interval has passed again. If no current domain, no-op.

**Note on pairing**: Py_BEGIN_ALLOW_THREADS and Py_END_ALLOW_THREADS must be in the same C block (they share `_save`). So the intended pattern is: in a GIL-free zone we call `maybe_cooperate()` which may do **only** Py_BEGIN_ALLOW_THREADS and return; later, when we are about to touch Python again, we call a function that does Py_END_ALLOW_THREADS (and timer reset). That implies the "release" path in `maybe_cooperate()` cannot be a bare Py_BEGIN_ALLOW_THREADS unless we have a matching Py_END_ALLOW_THREADS at a single known place (e.g. at the start of the next eval loop iteration or at `ensure_gil()`). So either: (a) we use a single "cooperate" block (Py_BEGIN_ALLOW_THREADS; Py_END_ALLOW_THREADS) in one place (e.g. at the loop boundary only), or (b) we implement a custom GIL release that does not use the macro pair (e.g. call the internal C API that the macros use) so we can release in one place and re-acquire in another. The design document should note this; the current implementation uses (a) at the loop boundary. For true "release and continue in GIL-free zone," (b) or an equivalent mechanism would be required and is left as a follow-on.

### 3.3 Resolving the domain

- **`kos_domain_current()`** already returns the active domain when we are inside an eval (TLS holds the current context, which holds the domain). So `maybe_cooperate()` and `ensure_gil()` call `kos_domain_current()` once; if NULL, return. Otherwise, they access the domain’s cooperation state (via a C accessor or inline struct fields). No need to pass context or domain through the call chain.

---

## 4. Cost–benefit analysis of options

### 4.1 Counter-only vs timer-only vs hybrid

| Option            | Hot-path cost              | When we release              | Benefit                          |
|-------------------|----------------------------|------------------------------|----------------------------------|
| **Counter-only**  | Decrement + compare (1–3 ns) | Every N dispatches            | Predictable, very cheap; no clock read. |
| **Timer-only**    | Monotonic clock read (~20–50 ns) every call | When interval elapsed         | Fair in wall time; cost on every call. |
| **Hybrid**        | Decrement + compare (1–3 ns); clock only every N calls | Every N calls **and** interval elapsed | Cheap hot path + time-based fairness. |

**Recommendation**: Hybrid. The counter keeps the hot path cheap; the timer, checked only every N calls, adds fairness without per-call clock cost.

### 4.2 Storage: Context vs Domain vs TLS-only

| Option       | Pros                                       | Cons                                           |
|-------------|--------------------------------------------|------------------------------------------------|
| **Context** | One per eval; natural for "this run."      | Must pass ctx into every cooperation call site (dynchain, stack, etc.). |
| **Domain**  | Evaluator-level policy; already in TLS via `kos_domain_current()`; no extra args at call sites. | Domain struct or C extension must hold state. |
| **TLS-only**| No change to Domain or Context.            | Another TLS slot; need to bind it to a domain/context at eval entry so "current" is well-defined. |

**Recommendation**: Domain. Policy lives on the Domain; `kos_domain_current()` already provides "current domain" with no extra parameters, so `maybe_cooperate()` and `ensure_gil()` can remain zero-argument.

### 4.3 Inlined header vs out-of-line function

| Option              | Pros                           | Cons                                   |
|---------------------|--------------------------------|----------------------------------------|
| **Static inline in header** | No call overhead; logic in one header. | Header must see Domain/state; every TU that includes it compiles the logic. State still shared (e.g. domain->state). |
| **Out-of-line in .c**      | Single implementation; state in .c or in Domain. | One call instruction per cooperation point (~few ns). |

**Recommendation**: Out-of-line in a single .c file (e.g. `domain.c` or a small `gil_cooperate.c`). Call overhead is a few nanoseconds per call and avoids threading a special header through the codebase; state lives on the Domain and is accessed via `kos_domain_current()`.

### 4.4 Release-and-continue vs release–reacquire in same block

| Option                          | Pros                                       | Cons                                                                 |
|---------------------------------|--------------------------------------------|----------------------------------------------------------------------|
| **Same block** (current style)  | Correct with standard Py_BEGIN_/END_ALLOW_THREADS; no custom GIL handling. | Other threads only run for a brief window; we do not "do work" while GIL is released. |
| **Release and continue**       | We can run long GIL-free stretches (chain walk, plan fill) with GIL released; better fairness. | Requires release in one place and re-acquire in another; standard macros require same block. Would need internal API or different pattern. |

**Recommendation**: Start with same-block (release then immediately re-acquire at loop boundary or in `ensure_gil()`). Document release-and-continue as a future improvement if we introduce a way to re-acquire at a different site than release.

---

## 5. Candidate call sites for `maybe_cooperate()`

Places identified as good candidates (low or no PyObject/refcount work; or single point per iteration):

### 5.1 Eval loop (already in use)

- **Loop boundary (on_normal / next_insn)**: Once per opcode dispatch. Current implementation already does a counter-based release every 255 iterations here; would become one call to `maybe_cooperate()`.
- **Before final event (eval exit)**: Once per return/yield/raise/await. Optional extra cooperation point so other threads get a slice before control returns to the caller.

### 5.2 Dynchain (GIL-free or mostly-GIL-free stretches)

- **After `kos_compute_lca`**: Pure chain walk (mark, find, clear); no Py*. One call after LCA is computed.
- **Inside `kos_build_transfer_plan`**: Between the LCA + count + fill steps and before `xfer_plan_fill_inject_action` (which does Py_INCREF). The stretch from ~4007–4034 is counter updates and plan filling; one or two `maybe_cooperate()` calls in the middle.
- **In `xfer_plan_fill_unwind_actions`**: Loop over nodes filling the plan; no refcount. One call per N iterations or one at the end.
- **In `xfer_plan_fill_wind_actions`**: Same idea; loop with PyMem only.
- **In `kos_clone_chain_segment`**: Between clone steps (each clone does Py* inside); optional call in the linking loop.

### 5.3 Stack

- **In `KosStackBlock_DeepCopy`**: After the "collect pointers into vals[]" loop (no refcount) and before the PushRef loop (which does INCREF). One call in that gap.

### 5.4 Other

- **`kos_update_frame_top`**: Pure walk to first FRAME; no Py*. One call.
- **`kos_find_node_by_id`**: Pure walk; no Py*. Optional if called in hot paths.

Placing `maybe_cooperate()` in these zones means that when the counter hits zero and the timer allows, we may release in the middle of real C work (e.g. plan filling or chain walk), giving other threads more useful time. With same-block release–reacquire, the benefit is still bounded to one window per N calls, but the call sites are ready for a future release-and-continue mode.

---

## 6. Invariants and safety

- **Only release when we do not hold references we will use without the GIL.** In GIL-free zones we only move pointers or walk C structs; we must not touch PyObject* refcounts or Python API until after `ensure_gil()` or re-entry into a section that holds the GIL.
- **Re-acquire before any PyObject* or Python API use.** Every path that touches refcounts or calls Python must be after a re-acquire (or never have released).
- **Domain is stable.** The domain of the active context does not change mid-eval; using it for cooperation state is safe.
- **No cooperation when there is no current domain.** If `kos_domain_current()` returns NULL (e.g. we are not inside an eval), `maybe_cooperate()` and `ensure_gil()` no-op so they are safe to call from any code path that might run outside an eval.

---

## 7. Implementation notes

- **Monotonic time**: Use a portable monotonic clock (e.g. `clock_gettime(CLOCK_MONOTONIC)` on POSIX, or Python’s `PyTime_Monotonic()` if available and linked). Store nanoseconds (or a consistent unit) for `last_cooperate_ts` and `cooperate_interval_ns`.
- **Default N**: 255 (or 256) for the counter reset, matching the current opcode-interval behavior.
- **Default interval**: 5 ms (5_000_000 ns) as a starting value; can be made configurable later (e.g. via Domain or environment).
- **Domain struct extension**: Add C-only fields (or a pointer to a small `KosDomainCooperateState` struct) for counter, last_ts, and interval. Not exposed to Python unless we add a future API for tuning.

---

## 8. References

- [domain.md](domain.md) — Koskari Domain and TLS
- [evaluator.md](evaluator.md) — Evaluator and eval loop structure
- `koskari/lib/eval.c` — Current GIL release (counter at loop boundary, on-exit)
- `koskari/lib/domain.c` — `kos_domain_current()`, TLS scope push/pop
