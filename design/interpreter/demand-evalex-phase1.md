# Demand in-eval denial (phase 1)

This document specifies **phase 1** of demand-scoped evaluation: deny suspension
**inside the evaluator** when execution is under a demand frame marker, instead
of returning `KOS_EV_AWAIT` / `KOS_EV_YIELD` to an outer host loop that injects
`KoskariSyncViolation` after `koskari_eval` returns.

**Implementation status:** **Complete** (branch `demand_in_eval`). The inject
loop in `demand_sync_ex` has been removed; in-loop denial guards are in place at
all P0 and P1 sites. See
[archive/demand-sync-await-violation-at-site.md](../archive/demand-sync-await-violation-at-site.md)
for the historical proposal that phase 1 supersedes.

**Audience:** maintainers and agents implementing or reviewing this change.

**Related:** [demand-wrapper.md](../api/demand-wrapper.md) (Python `Demand`
adapter — **out of scope** for phase 1), [dunder-adapter.md](../api/dunder-adapter.md)
(host `demand_call` bridge), [evaluator.md](evaluator.md),
[EVALUATOR.md](../EVALUATOR.md) § pushing results and auto-await.

---

## 1. Goal (phase 1 only)

1. When Koskari runs on the **demand path** (`demand_call` / `demand_sync_ex`
   driving a `KosCallable`), any attempt to suspend (`KOS_EV_AWAIT` or
   `KOS_EV_YIELD`) becomes an **in-eval raise** of `KoskariSyncViolation` at
   the deny site so `TRY` / `finally` can run.
2. Uncaught, the exception propagates as **`KOS_EV_RAISE`** to the host; existing
   `demand_sync_impl` / `demand_call` error translation stays valid.
3. **Delete** the post-`koskari_eval` inject loop in `demand_sync_ex` once
   in-loop denial is complete.
4. **Do not** add `KOS_DYN_DEMAND`, `KOS_CTX_DEMAND`, or wrapper/composition
   changes in this phase.

Non-goals for phase 1: `DemandSyncGenerator` stepping policy, `define-demanding`
compile changes, bound-method eval install, tailcall-through-`Demand` wrapper,
Python `async def` driving inside `demand_call`.

---

## 2. Motivation

### 2.1 What is wrong today

`demand_sync_ex` builds a fresh `KosContext`, runs `koskari_eval` once, and if
the event is `KOS_EV_AWAIT` or `KOS_EV_YIELD`, runs a **second** `koskari_eval`
with `KoskariSyncViolation` thrown in (`demand.c`, `run_eval` inject loop).

Problems:

- Denial is **outside** the normal suspend path; the VM briefly marks itself
  suspended (`MarkSuspendedNotRunning`, `KOS_EV_AWAIT` in `out_ev`) before the
  host corrects it.
- Two eval drives per deny site; inject cap (`demand_sync_inject_cap`) is a
  safety net for a design that should not suspend at all.
- Koskari handlers at the await/yield site do not see the violation as a
  first-class in-eval fault on the **first** pass.

### 2.2 Target behavior

At every site that would emit `KOS_EV_AWAIT` or `KOS_EV_YIELD`, if execution is
**in demand**, **do not suspend**. Set `PyErr` to `KoskariSyncViolation` (or
equivalent) and return through the existing opcode error / `eval_event_raise`
path. Never set `KOS_EV_AWAIT` / `KOS_EV_YIELD` for demand-scoped runs.

After phase 1, `demand_sync_ex` is:

```text
build frame with KOS_FRAME_DEMAND
ctx = KosContext_New(frame)
r = koskari_eval(ctx, &ev, NULL, NULL, NULL)
handle KOS_EV_RETURN / KOS_EV_RAISE only
KOS_EV_AWAIT / KOS_EV_YIELD → assert-unreachable or defensive RuntimeError
```

---

## 3. Demand extent model (phase 1)

### 3.1 Frame marker only

Use the existing unused **`frame_flags`** field on `KosDynFrame`
(`dynchain.h`, `u.frame.frame_flags`; accessor `KosDynFrame_FLAGS`).

Define:

```c
#define KOS_FRAME_DEMAND  (1u << 0)
```

**Semantics:** a frame with `KOS_FRAME_DEMAND` marks the **entry** of a demand
extent. Nested callees pushed above it do **not** need the marker; they are
still under demand while a marked ancestor remains on the parent chain.

**Set the marker** on the root frame in `demand_sync_ex` when building the
callee-as-root frame (`kos_dynframe_new` for `KosExpression` / non-yielding
`KosFunction`). Do **not** set it on generator / `DemandGenerator` paths unless
explicitly decided (see §8).

### 3.2 No context flag (phase 1)

Do **not** add `KOS_CTX_DEMAND` on `KosContext` in phase 1. At suspend sites,
derive “in demand” by walking **ancestor dyn nodes** from the live frame.

Rationale (from design discussion):

- Avoid activate/pop sync hooks between frame marker and context mirror.
- Suspend checks are rare (freeze boundaries); O(depth) walk is acceptable.
- If we later need a dedicated `KOS_DYN_DEMAND` dyn node **and** a fast context
  flag, introduce **both together** when the dyn node lands.

### 3.3 No `KOS_DYN_DEMAND` dyn node (phase 1)

Partial demand paths that do not align with a full frame activation should use
a **thunk** and `demand-call`:

```koskari
(demand-call { ... })
```

If a non-frame-aligned demand extent is ever required, add `KOS_DYN_DEMAND` in a
later phase; do not block phase 1 on it.

### 3.4 Detecting “in demand”

```c
/**
 * Walk parent chain from the live frame. Return 1 if any ancestor FRAME has
 * KOS_FRAME_DEMAND.
 */
int kos_eval_in_demand(KosEvalState *es);
```

Start from `(KosDynNode *) KosEvalState_FRAME(es)`, follow `KosDynNode_PARENT`.
Only test nodes where `KosDynNode_IS_FRAME(n)`; non-frame nodes (TRY, WITH,
YIELD_FROM, …) are transparent.

**Why not only the top frame?** Nested calls push child frames without the
marker; the marked demand-root frame stays below on the parent chain.

**On pop:** no demand-specific hook — the live frame changes and the walk
reflects the new extent automatically.

---

## 4. Deny helper

Centralize in eval (suggested: `eval_utils.c` or a small `demand_eval.c`
included from `eval.c` / linked from `demand.c` for the exception object).

```c
/**
 * If kos_eval_in_demand(es), set KoskariSyncViolation on PyErr and return 1.
 * Otherwise return 0.
 *
 * msg: short English string ("async suspension is not allowed in demand_sync"
 * or a shared constant matching existing tests).
 */
int kos_eval_demand_deny_suspend(KosEvalState *es, const char *msg);
```

**Exception object:** reuse `KoskariSyncViolation` from `demand.c`. Expose a
narrow C API for eval, e.g. `void kos_demand_set_sync_violation(const char *msg)`
in `demand.h`, so eval does not duplicate `PyErr_NewException` registration.

**Do not** emit `KOS_EV_AWAIT` and convert in the dispatch switch — deny
**before** `KosEvalState_MarkSuspendedNotRunning` and before constructing
the `KosOpResult`.

**Propagation:** callers that would suspend should check deny first; on deny
call `kos_demand_set_sync_violation`, set `PyErr`, and return `EVAL_RAISE()`.
`cg_handle_op_result` routes `KOS_RES_RAISE` to `eval_event_raise` →
`eval_boundary_raise_dispatch` → normal unwind / `TRY`.

---

## 5. Guard sites (exhaustive for phase 1)

Any path that sets `KOS_EV_AWAIT` or stops with yield suspension must call
`kos_eval_demand_deny_suspend` first.

| Priority | Location | File | Notes |
| -------- | -------- | ---- | ----- |
| **P0** | `push_or_await_common` | `eval/eval_utils.c` | Auto-await after `CALL`; `kos_await_dispatch` / `AWAIT` arg 1 |
| **P0** | `op_push_or_await` | `eval/eval_utils.c` | Opcode peephole duplicate of above |
| **P0** | `AWAIT` arg == 0 | `opcode/AWAIT.c` | Bare `(await)` cooperative suspend |
| **P0** | `next_from_python_async_iter` | `eval/eval_utils.c` | `NEXT` / `ITER_NEXT` over Python async iter |
| **P1** | `YIELD_FROM` eager await | `opcode/YIELD_FROM.c` | ~line where `KOS_EV_AWAIT` is set after `EagerStepAwaitable` |
| **P1** | `YIELD` opcode | `opcode/YIELD.c` | Generator yield — `demand_sync` inject loop handles today |
| **P2** | `eval_event_yield` entry | `eval.c` | Belt-and-suspenders if opcode path missed |
| **P2** | `eval_event_await` entry | `eval.c` | Demand-AWAIT denial: `kos_eval_in_demand` check → `kos_demand_set_sync_violation` → `eval_event_raise` (was `eval_phase_post_opcode_tail`; that function was deleted in Phase 6) |

Search command for agents:

```bash
rg 'KOS_EV_AWAIT|KOS_EV_YIELD' koskari/lib --glob '*.c'
```

Re-run after edits; any new suspend site needs the guard.

---

## 6. `demand_sync_ex` changes (`demand.c`)

### 6.1 Set frame marker

After `kos_dynframe_new(...)` for the `run_eval` path (Expression and
non-yielding Function), before `KosContext_New(frame)`:

```c
KosDynFrame_FLAGS(frame) |= KOS_FRAME_DEMAND;
```

### 6.2 Remove inject loop

Delete the `for (;;)` block in `run_eval` that:

1. Tests `KOS_EV_AWAIT` / `KOS_EV_YIELD`
2. Discards payload
3. Builds `KoskariSyncViolation` and calls `koskari_eval` again

Replace with a single `koskari_eval` call.

### 6.3 Post-eval switch

- `KOS_EV_RETURN` / `KOS_EV_RAISE`: unchanged.
- `KOS_EV_AWAIT` / `KOS_EV_YIELD`: treat as bug (defensive `RuntimeError` or
  `assert` in debug builds) — should be unreachable if guards are complete.
- Update the module docstring at top of `demand.c` (currently claims inject is
  the implementation).

### 6.4 `demand_sync_impl` defensive `r == 1`

May become dead code; keep briefly or remove with a comment.

---

## 7. Tail-call preservation

`kos_dynframe_reset_for_tail` (`dynchain/dyn_frame.c`) currently does:

```c
KosDynFrame_FLAGS(frame) = 0;
```

**Change:** preserve demand marker across tail reset:

```c
KosDynFrame_FLAGS(frame) &= KOS_FRAME_DEMAND;
```

(or save/restore the demand bit explicitly). Without this, tail calls inside a
demand extent lose the marker on the reused frame object and `kos_eval_in_demand`
would falsely return 0.

`kos_dynframe_clone` already copies `KosDynFrame_FLAGS` — no change needed for
continuation capture in phase 1.

---

## 8. Out of scope / follow-ups

| Topic | Phase 1 | Later |
| ----- | ------- | ----- |
| `DemandSyncGenerator` / `demand_gen_step` | Keep boundary raise on AWAIT | In-eval deny when gen ctx has demand marker |
| Yielding `KosFunction` → `DemandGenerator` | Unchanged | May set marker on gen frame if needed |
| `KOS_CTX_DEMAND` + activate/pop sync | No | With `KOS_DYN_DEMAND` if needed |
| `KOS_DYN_DEMAND` dyn node | No | Non-frame extents; thunk first |
| `demand-call` inside async eval | No | Set marker on installed frame |
| `define-demanding` / `Demand` wrapper | No | [demand-wrapper.md](../api/demand-wrapper.md) |
| `kos_install_boundmethod` unwrapping `Demand` | No | Separate fix |
| `on-demand?` in `_conditionals.ki` | No | Dyn walk or primitive |
| Plain Python `async def` in `demand_call` | **Done** | `demand_step_python_result` in `demand.c` |

### 8.1 Plain Python `demand_call` — eager-await mimic — **Complete**

Implemented as `demand_step_python_result` in `koskari/lib/demand.c` (branch
`demand_in_eval`). Called from both plain-Python branches of `demand_sync_ex`
(initial non-`KosCallable` check and post-`BoundMethod`-unwrap check). Uses
`__await__()` + `send(None)` + `.throw(KoskariSyncViolation)` on suspension,
consistent with the in-eval `eager_step_awaitable` policy. No `KosContext` is
created.

Tests added to `TestDemandSyncPlainPython`:
- `test_python_coroutine_completes_immediately`
- `test_python_coroutine_suspends_raises_violation`
- `test_python_coroutine_catches_violation_and_returns`

Call flattening for `Demand(KosCallable)` from bytecode is the next open item —
see [demand-evalex-phase2.md](demand-evalex-phase2.md).

---

## 9. Tests

Primary suite: `tests/lib/test_demand_sync.py`

- `test_yield_violation`, await violation cases — should still raise
  `KoskariSyncViolation`; tracebacks may anchor at **in-eval** sites (update
  expectations if they asserted wrapper-only frames).
- **Add:** Koskari `try` / `except KoskariSyncViolation` inside demand-called
  code proves `finally` runs on deny.
- **Add:** nested call under demand (caller demand-root, callee without marker)
  still denies await.
- Run: `tox -qe py -- tests/lib/test_demand_sync.py`

After tail-reset fix, add or extend compile/specials tail-call test under demand
if one exists; otherwise a small `build_expr` repro in `test_demand_sync.py`.

---

## 10. Implementation checklist — **All complete** (branch `demand_in_eval`)

1. [x] `#define KOS_FRAME_DEMAND` in `dynchain.h`.
2. [x] `kos_eval_in_demand` (O(1), reads current frame) + `kos_eval_demand_deny_suspend`; `kos_demand_set_sync_violation` in `demand.h`/`demand.c`.
3. [x] Guards at P0 sites: `push_or_await_common`, `op_push_or_await`, `next_from_python_async_iter`, bare `AWAIT` (arg==0).
4. [x] Guards at P1 sites: `YIELD.c`, `YIELD_FROM.c` (`yield_from_emit_yield` and async-delegate path).
5. [x] `KosDynFrame_FLAGS(frame) |= KOS_FRAME_DEMAND` at `run_eval:` in `demand_sync_ex`.
6. [x] Inject loop removed from `demand_sync_ex`; `KOS_EV_AWAIT`/`KOS_EV_YIELD` post-eval case is now a bug-report `RuntimeError`.
7. [x] `kos_dynframe_reset_for_tail`: `= 0` → `&= KOS_FRAME_DEMAND`. Child frames inherit flag in `kos_dyn_push_frame`.
8. [x] `demand.c` module docstring and phase1.md implementation status updated.
9. [x] `tox -qe py -- tests/lib/test_demand_sync.py` passes (27 tests).
10. [x] `demand-wrapper.md` status updated; phase 2 cross-linked.

---

## 11. Future direction (not phase 1)

Longer-term design (recorded for context, **do not implement now**):

- **Eval flag + dyn extent:** demand as dynamic extent with optional
  `KOS_CTX_DEMAND` mirror for O(1) checks when `KOS_DYN_DEMAND` exists.
- **Wrapper compression:** `bound ∘ demand ∘ fn` as single bound install with
  demand marker; `Demand` Python adapter remains for `tp_call` / `async def`
  interop.
- **Tailcall through demand:** requires `KosCallable` install path, not wrapper
  `PyObject_Call` detour — separate from phase 1.

---

## 12. File index

| File | Change |
| ---- | ------ |
| `koskari/lib/dynchain.h` | `KOS_FRAME_DEMAND` |
| `koskari/lib/dynchain/dyn_frame.c` | tail-reset preserve |
| `koskari/lib/eval/eval_utils.c` | `kos_eval_in_demand`, deny helper, P0 guards |
| `koskari/lib/eval.c` | `kos_await_dispatch` path covered via utils; optional P2 |
| `koskari/lib/eval.h` | declare helpers if public |
| `koskari/lib/opcode/AWAIT.c` | bare-await guard |
| `koskari/lib/opcode/YIELD.c` | yield guard (P1) |
| `koskari/lib/opcode/YIELD_FROM.c` | async delegate guard (P1) |
| `koskari/lib/demand.c` | marker on root frame; delete inject loop |
| `koskari/lib/demand.h` | `kos_demand_set_sync_violation` or similar |
| `tests/lib/test_demand_sync.py` | extend for try/finally, nested call |
