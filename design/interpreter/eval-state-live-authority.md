# KosEvalState live authority and chain-transition funnel

**Status:** Phase 1 **landed** (live authority + Tier-1 funnel); Phase 2 **open** (§10)  
**Audience:** Agents and humans implementing performance work on the evaluator  
**Repository root:** `sibilant-ng/`  
**Code survey:** `koskari/lib` as of 2026-05-26 (post Phase 1)  

**Related documents:**

| Document | Relationship |
|----------|----------------|
| [freeze-thaw-context-segments.md](freeze-thaw-context-segments.md) | Long-term freeze/thaw and C-local execution model |
| [../kos_eval_state_api_migration.md](../kos_eval_state_api_migration.md) | Opcode/API migration off `KosContext_*` macros (orthogonal surface cleanup) |
| [../EVALUATOR.md](../EVALUATOR.md) | Eval loop, prologue, transfer plans |
| [../api/tracing.md](../api/tracing.md) | `eval_state_sync` counters (`KOS_TRACE_EVAL_SYNC`) |
| [../INVARIANTS-AND-CLARIFICATIONS.md](../INVARIANTS-AND-CLARIFICATIONS.md) | Global execution invariants |
| [dyn-chain-eval-state-violations.md](dyn-chain-eval-state-violations.md) | Audit ledger: ctx vs `es` authority violations (grep-backed) |

---

## How to use this document

| If you need to… | Read |
|-----------------|------|
| Understand what is allowed to change | §1 (non-goals), §2 (goals) |
| Learn the authority model (`es` vs `ctx`) | §3 |
| See why the current code is slow | §4 |
| See what is already implemented | §0 (status), §6 (APIs), §10 (phases) |
| Know where to implement updates | §5 (choke points), §6 (APIs) |
| Match existing behavior exactly | §7 (correctness), §12 (verification) |
| Avoid infinite prologue/transfer loops | §11 |
| Verify your work | §12 |
| Remaining agent work | §14 (checklist) |

**Do not** use this document to change transfer decisions, transition kinds, plan steps, or node install/uninstall semantics. See §1.

---

## 0. Implementation status (Phase 1)

Phase 1 is **complete** and verified with **`tox -qe test`** (full pass, no hang). Transfer plan semantics, transition kinds, and install/uninstall behavior were **not** changed.

| Item | State | Location |
|------|-------|----------|
| Live authority during RUN | **Done** | `eval.h` KosEvalState comment; opcodes use `KosEvalState_*` |
| Tier-1 push / commit funnel | **Done** | `dynchain.c`: `kos_dyn_push`, `kos_dyn_push_noinstall`, `kos_dyn_commit_dyn_top` |
| Incremental retarget after push | **Done** | `static kos_dyn_retarget_after_push` |
| Incremental retarget after pop (+ fallback) | **Done** | `static kos_dyn_retarget_after_pop`; fallback when `new_top != parent(removed)` |
| Full chain rebuild | **Done** | `kos_dyn_rebuild_live_from_chain` (public in `dynchain.h`) |
| `es` first, ctx snapshot at retarget end | **Done** | `kos_dyn_apply_live`, `kos_dyn_snapshot_ctx_derived` |
| Freeze at eval exit | **Done** | `KosEvalState_FreezeDerivedToContext` in `eval_state.c`; `koskari_eval_cleanup` in `eval.c` |
| `PatchDynCaches` / `RefreshDynCaches` / `ApplyDynCaches` | **Removed** | Replaced by dynchain helpers above |
| Scatter `SyncFromContext` on `dyn_*.c` **success** paths | **Mostly removed** | Error / guard paths remain (§4.1, Tier 4) |
| Xfer step completion sync | **Removed** | Plan driver comment: steps leave `es` coherent via rebuild |
| Opcode `PUSH_*` post-push rebuild | **Kept intentionally** | Stack assigned after push; see §9.3 |

**Phase 2 (not done):** tail koscall via Tier-1 commit, Tier-2 xfer incremental retarget, straggler `dyn_top` / `ctx` reader audit, optional defer of ctx derived writes until freeze only. See §10.

---

## 1. Non-goals (behavioral freeze)

This refactor is **bookkeeping only**. The running program must behave identically.

### 1.1 Off limits

- **When** to build or arm a `KosTransferPlan`
- **Which** `KosTransitionKind` / `kos_dyn_transition` walk to run
- **Transfer plan step order** (INSTALL, PUSH, unwind, inject, truncate, …)
- **`kos_dyn_install` / `kos_dyn_uninstall_handler`** semantics per node kind
- **Async wind/with phase machines** and return codes (`KOS_DYN_UNWIND_ASYNC_STARTED`, …)
- **Prologue control flow** (`KOS_OP_PROLOGUE`, `run_transfer_plan_to_completion`, `eval_phase_transfer`)
- **Opcode-visible** stack, namespace, frame, PC at boundaries (must match pre-refactor)

### 1.2 In scope

- **How** `KosEvalState` fields (`frame`, `code`, `block`, `namespace`, `pc`, `acting_pc`) are updated after chain edits that **already happen today**
- **Removing** redundant `KosEvalState_SyncFromContext` / full-chain `RefreshDynCaches` from scattered call sites
- **Centralizing** live-field updates in a small set of **choke-point** functions (DRY)
- **Optional:** deferring writes to `ctx->frame_top` / `block` / `namespace` until freeze boundaries, once no RUN-path reader depends on them being fresh

### 1.3 Agent rule

> If a diff changes a branch condition (“should we unwind?”, “which plan?”), a transition kind, or the sequence of chain mutations, it is **out of scope**. Only replace *how* live fields are recorded after mutations that already occur.

---

## 2. Goals

| Goal | Meaning |
|------|---------|
| **Correctness** | Already correct today; preserve observable behavior |
| **Performance** | Cut hot-path cost: fewer chain walks, fewer `ctx`↔`es` round-trips, less pointer chasing |
| **DRY** | One funnel for live-field updates on push/pop/commit, not ~130 `SyncFromContext` call sites |
| **Future work** | Clear `es`-authoritative model enables C-local stack optimizations (see freeze-thaw doc) |

**Success metrics:**

| Metric | Phase 1 | Phase 2 target |
|--------|---------|----------------|
| **`tox -qe test`** (no hang, full pass) | **Met** | Keep met |
| No `PatchDynCaches` / `RefreshDynCaches` in lib | **Met** | Keep met |
| `SyncFromContext` only at Tier-4 sites | **Partial** (~140 calls remain; many are error paths) | Full Tier-4 audit (§12.2) |
| `kos_dyn_rebuild_live_from_chain` only at Tier-2 + fallback | **Met** (grep) | Optional: reduce Tier-2 rebuild count |

**Performance follow-up (human-driven, after correctness lands):**

- Rebuild with `KOS_TRACE_EVAL_SYNC=1` and compare `eval_state_sync` in `get_trace_report()` ([tracing.md](../api/tracing.md)) to quantify time lost to dyn cache updates before vs after

---

## 3. Authority model

### 3.1 During `koskari_eval` (RUN)

| State | Authoritative? | Role |
|-------|----------------|------|
| `es->frame`, `es->code`, `es->block`, `es->namespace`, `es->pc`, `es->acting_pc` | **Yes — live work** | What opcodes and hot helpers read via `KosEvalState_*` |
| `ctx->dyn_top` | **Yes — structural** | Head of heap parent-linked chain; required for GC, walks, capture |
| `ctx->frame_top`, `ctx->block`, `ctx->namespace` | **No — freeze snapshot** | Stale during RUN is OK if nothing uses them for decisions; sync **from** `es` on freeze |
| `ctx->plan`, flags, bridge, inject, … | **Yes** | Unchanged by this refactor |

**Critical correction:** Comments and names that call `es` fields “caches” or “mirrors” of `ctx` describe the **old** mental model. For this work, treat them as **registers**. `KosContext` derived fields are the serialized copy used to **revive** the next `koskari_eval` entry.

### 3.2 Dataflow

```
KosContext_New / suspend
        │
        │  KosEvalState_Init (thaw: ctx + chain → es)
        ▼
   RUN: mutate chain via Tier-1 choke points;
        update es live fields there only
        │
        │  KosEvalState_FlushLiveRegsToFrame (frame-boundary)
        │  KosEvalState_FreezeDerivedToContext (new; eval exit — §6)
        ▼
   suspend / KosContext_Free
```

**Removed steady-state pattern (pre Phase 1):**

```
mutate dyn_top → walk chain → write ctx->frame_top/block/namespace
              → ApplyDynCaches (ctx → es) → SyncFromContext (ctx → es again)
```

**Current (Phase 1):**

```
mutate dyn_top → update es live fields at choke point
              → (optional) snapshot ctx derived fields only at freeze
```

### 3.3 Opcode hot path

Opcodes already use `KosEvalState_BLOCK(es)`, `KosEvalState_FRAME(es)`, etc. (`koskari/lib/eval.h`). They must **not** gain chain logic or sync calls.

---

## 4. Problems addressed

### 4.1 Baseline (pre Phase 1)

- **~130+** `KosEvalState_SyncFromContext` calls, many on success paths in `dyn_*.c` and `dynchain.c`
- Every `kos_dyn_commit_dyn_top` paid a **full chain walk** via `KosEvalState_RefreshDynCaches`
- Push used `KosEvalState_PatchDynCaches` writing **`ctx` first**, then `ApplyDynCaches`
- `eval.h` described `ctx` derived fields as authoritative during RUN

### 4.2 After Phase 1

| Problem | Resolution |
|---------|------------|
| Full refresh on every pop | `kos_dyn_commit_dyn_top` calls `kos_dyn_retarget_after_pop`: **O(1)** when the removed node did not own live slots, or only owned slots that need a bounded parent walk; **full rebuild** when `new_top != KosDynNode_PARENT(removed)` (splice, gen install, xfer-shaped edits) |
| Push retarget | `kos_dyn_retarget_after_push` updates **`es` first**, then `kos_dyn_snapshot_ctx_derived` |
| Removed APIs | `PatchDynCaches`, `RefreshDynCaches`, `ApplyDynCaches` deleted from `eval_state.c` / `eval.h` |
| `eval.h` wording | Updated: `es` authoritative during RUN; `ctx->frame_top` / `block` / `namespace` are revive snapshots |
| Scatter sync on pop/push success | Removed from most `dyn_*.c` success paths; **not** zeroed out globally |

### 4.3 Remaining `SyncFromContext` (grep `koskari/lib`, post Phase 1)

Approximate call counts (many are **error / guard** paths, not success-path belt-and-suspenders):

| File | Calls (approx.) |
|------|----------------:|
| `koskari/lib/dynchain.c` | 89 |
| `koskari/lib/dynchain/dyn_with.c` | 19 |
| `koskari/lib/dynchain/dyn_next.c` | 7 |
| `koskari/lib/dynchain/dyn_wind.c` | 5 |
| `koskari/lib/dynchain/dyn_yield_from.c` | 4 |
| `koskari/lib/dynchain/dyn_gen.c` | 4 |
| `koskari/lib/dynchain/dyn_foreach.c` | 3 |
| `koskari/lib/eval/eval_chain.c` | 2 |
| `koskari/lib/eval/eval_utils.c` | 1 (`do_frame_prep` tail reset — §9.1) |
| Other `dyn_*.c`, `utils.c` | 1 each |

Phase 2 goal: confirm each remaining site is Tier-4 (§5) or replace with retarget/freeze; do **not** re-add success-path sync to “fix” tests.

---

## 5. Choke points (only places that may retarget live fields)

All live-field updates for chain shape changes must funnel through these tiers. **Do not** add `SyncFromContext` or ad-hoc full-chain walks elsewhere.

### Tier 1 — Primary publishers (**implemented**)

| Symbol | File | Lines (approx.) | When used |
|--------|------|-----------------|-----------|
| `kos_dyn_push` | `dynchain.c` | ~859–879 | Push + `kos_dyn_install` → `kos_dyn_retarget_after_push` |
| `kos_dyn_push_noinstall` | `dynchain.c` | ~904–920 | Region nodes (TRY, LOOP, …) → retarget after push |
| `kos_dyn_commit_dyn_top` | `dynchain.c` | ~892–901 | Pop, splice head, uninstall commit → `kos_dyn_retarget_after_pop` |
| `kos_dyn_uninstall` / `kos_dyn_uninstall_core` | `dynchain.c` | ~924+ | No extra sync on success; `SyncFromContext` on failure only |

**Retarget implementation:** `static` helpers in `koskari/lib/dynchain.c` (§6). **`kos_dyn_rebuild_live_from_chain`** is the only exported rebuild API (`dynchain.h`).

**Lifecycle in eval TU:** `KosEvalState_Init`, `KosEvalState_SyncFromContext` (repair), `KosEvalState_FreezeDerivedToContext` in `eval/eval_state.c`; declared in `eval.h`.

**Callers:** `kos_dyn_push_*` / `kos_dyn_pop_*` in `dynchain/dyn_*.c` allocate/link and call Tier 1; success paths must not call `SyncFromContext` (Phase 1 applied).

Examples of pop wrappers (already use `commit_dyn_top` only):

- `kos_dyn_pop_try` — `dynchain/dyn_try.c` ~91–104
- `kos_dyn_pop_with` — `dynchain/dyn_with.c` ~267–279
- `kos_dyn_pop_loop` — `dynchain/dyn_loop.c` ~100–113
- `kos_dyn_pop_frame` — `dynchain/dyn_frame.c` ~180–247

### Tier 2 — Bulk chain surgery (full recompute; **current behavior**)

Sites where **incremental pop/push rules are insufficient**. Each calls **`kos_dyn_rebuild_live_from_chain(es)`** (may run twice per xfer step when resume stack is grafted):

| Site | File | Lines (approx.) | Why |
|------|------|-----------------|-----|
| `xfer_step_install` | `dynchain.c` | ~3626–3708 | Replaces entire `dyn_top` segment; rebuild after stack graft ~3670 |
| `xfer_step_push` | `dynchain.c` | ~3719–3779 | Grafts segment onto `dyn_top`; rebuild after stack graft ~3765 |
| `kos_dyn_finish_delimited_pop_return` | `dynchain.c` | ~4574–4577 | Sets `ctx->frame_top`, may `pop_delim`, then rebuild |
| `kos_dyn_unlink_next_node` (non–dyn_top) | `dyn_next.c` | ~130–141 | Splices parent links without `commit_dyn_top` |
| `KosContext_New` bootstrap | `eval.c` | ~975 | After `KosEvalState_Init` |
| `KosEvalState_TailKoscallReplaceFrame` | `eval_utils.c` | ~549–562 | Manual `dyn_top` publish (Phase 2: fold into Tier 1) |
| `PUSH_WITH` / `PUSH_DYN_WITH` / `PUSH_COLLECT` | `opcode/*.c` | ~64–78 | Stack block assigned on node **after** push retarget |
| Gen install / xfer fallback inside pop retarget | `dynchain.c` | `kos_dyn_retarget_after_pop` | `new_top != parent(removed)` |

**Rule:** Do not add new Tier-2 sites. Optional Phase 2 work: incremental retarget for xfer INSTALL/PUSH (high risk; defer until tail koscall uses Tier 1).

### Tier 3 — Lifecycle (thaw / freeze)

| Site | File | Lines (approx.) | Role |
|------|------|-----------------|------|
| `KosEvalState_Init` | `eval/eval_state.c` | 27–49 | Thaw `es` from `ctx` + frame at entry |
| `koskari_eval` entry | `eval.c` | (search `KosEvalState_Init`) | Build stack `es` |
| `koskari_eval_cleanup` | `eval.c` | ~2641 | `FlushLiveRegsToFrame` then **`KosEvalState_FreezeDerivedToContext`** |
| `kos_eval_emit_raise_from_pyerr` | `eval.c` | ~616–617 | Flush before traceback when `es` given |

### Tier 4 — Error recovery only (`SyncFromContext`)

Keep `KosEvalState_SyncFromContext` **only** where the chain or `ctx` may be inconsistent after failure:

| Site | Reason |
|------|--------|
| `kos_dyn_push` install failure | `dynchain.c` ~875 |
| `kos_dyn_uninstall` failure | `dynchain.c` (handlers) |
| `KosEvalState_Transition` / `kos_dyn_transition` on `-1` | `eval/eval_chain.c` ~166; migration doc D3 |
| Straggler `dyn_top` writes until Phase F folded | See §9 |

---

## 6. Live retarget API (implemented)

Phase 1 placed retarget logic in the **dynchain translation unit** (`dynchain.c`), not as public `KosEvalState_*` retarget entry points. Only **`kos_dyn_rebuild_live_from_chain`** is exported from `dynchain.h` for bootstrap, tail koscall, Tier-2 paths, and opcodes.

### 6.1 `static kos_dyn_apply_live(es, ft, bl, ns)`

Writes live `frame`, `block`, `namespace` (and `code` / PCs on frame change) on **`es`**. On frame pointer change, flushes live PCs to the outgoing frame before loading from the new frame. Call only from other retarget helpers after `dyn_top` is correct.

### 6.2 `static kos_dyn_snapshot_ctx_derived(es)`

Copies `es->frame` / `block` / `namespace` into `ctx->frame_top` / `block` / `namespace` after each successful retarget or rebuild. Keeps revive snapshots aligned during RUN for readers that still use `ctx`; **not** a substitute for freeze at eval exit.

### 6.3 `static kos_dyn_walk_live_from(es, start, …)`

Parent walk from `start` for innermost frame, stack slot, and namespace. Used by incremental pop retarget and by rebuild.

### 6.4 `static kos_dyn_retarget_after_push(es, new_node)`

**Replaces:** `KosEvalState_PatchDynCaches`. O(1) after single-node push when `new_node == dyn_top`. Frame nodes replace frame/code/block/ns; other nodes update only slots they carry. With `KOS_DEBUG_PATCH_DYN_CACHES`, compares against full rebuild in debug builds.

### 6.5 `static kos_dyn_retarget_after_pop(es, removed, new_top)`

**Replaces:** full refresh inside `kos_dyn_commit_dyn_top` for the common case.

**Critical fallback (implemented):** if `!removed` or `new_top != KosDynNode_PARENT(removed)`, call **`kos_dyn_rebuild_live_from_chain`** (required for gen install, splices, xfer-shaped head changes).

Otherwise update only slots the removed node owned; one `kos_dyn_walk_live_from` when needed.

### 6.6 `void kos_dyn_rebuild_live_from_chain(es)` (public)

Full walk from `ctx->dyn_top`, then `kos_dyn_apply_live` + `kos_dyn_snapshot_ctx_derived`. Tier 2 and pop fallback only on hot paths.

### 6.7 `void KosEvalState_FreezeDerivedToContext(es)` (`eval_state.c`)

Called from `koskari_eval_cleanup` after `FlushLiveRegsToFrame`:

```text
ctx->frame_top = es->frame;
ctx->block     = es->block;
ctx->namespace = es->namespace;
```

### 6.8 Removed / retained eval TU APIs

| API | Fate (Phase 1) |
|-----|----------------|
| `KosEvalState_SyncFromContext` | **Retained** — Tier 4 repair only |
| `KosEvalState_PatchDynCaches` | **Removed** |
| `KosEvalState_RefreshDynCaches` | **Removed** |
| `KosEvalState_ApplyDynCaches` | **Removed** (logic in `kos_dyn_apply_live`) |
| `KosEvalState_Init` | **Retained** — thaw from `ctx` at eval entry |

---

## 7. Correctness oracle and debugging tools

### 7.1 Primary oracle: tests

**If `tox -qe test` finishes without hanging and all tests pass, the refactor is correct enough to ship** from a behavioral standpoint.

This suite exercises transfers, wind/with async paths, continuations, generators, and opcode combinations that are impractical to duplicate with inline cache comparisons. A **hang** usually means a prologue/transfer loop or inconsistent `es` vs `dyn_top` (§11) — treat hangs as hard failures, not flakes.

Run from repository root `sibilant-ng/`:

```bash
tox -qe test
```

For a narrower check during development:

```bash
tox -qe py -- tests/...
```

(do not invoke `python`/`pytest` directly; use tox per repository `setup.cfg`.)

### 7.2 What failures likely mean

| Symptom | Likely cause (in scope of this refactor) |
|---------|------------------------------------------|
| Hang in transfer/prologue tests | Accidental transfer or unwind behavior change; retarget ran at wrong time (e.g. before async pop completes) |
| Wrong stack/ns/frame in opcode tests | Live fields updated incorrectly; or a **reader still uses `KosContext_*` derived fields** during RUN instead of `KosEvalState_*` |
| Intermittent pass/fail | Less common; suspect ordering of flush vs chain mutation |

**Out-of-scope mistakes** (§1) show up the same way: changed plan steps, transition kinds, or install/uninstall semantics. Do not “fix” those by adding `SyncFromContext` everywhere.

### 7.3 `KOS_DEBUG_*` — catastrophic failure tooling, not the gate

Build-time flags such as **`KOS_DEBUG_PATCH_DYN_CACHES`** (see `eval_state.c` ~214–230) compare incremental patch vs full refresh on **ctx** derived fields. They exist for **verbose diagnosis in gdb-oriented builds** when something has already gone badly wrong — not as the acceptance test for each change.

Use when:

- Tests fail or hang and you need to see whether incremental retarget diverged from a full rebuild on the same `dyn_top`
- You suspect a missed `KosEvalState_*` accessor still reading stale `ctx->frame_top` / `block` / `namespace`

Do **not** require extending debug compares or running debug builds for every PR if the full test suite is green.

### 7.4 Tracing — performance diagnosis after correctness

**`KOS_TRACE_EVAL_SYNC`** and `eval_state_sync` in `get_trace_report()` ([tracing.md](../api/tracing.md)) measure call counts and wall time per sync API. Use **after** tests pass to confirm `sync_from_context` and `refresh_dyn_caches` traffic dropped — not as a substitute for functional tests.

### 7.5 Frame change rule (unchanged)

When `es->frame` pointer changes, flush live `pc`/`acting_pc` to the **outgoing** heap frame, then load from the new frame (`kos_dyn_apply_live` in `dynchain.c`). Violations surface as wrong PC or wrong code object in tests, not via debug flags alone.

---

## 8. What nodes affect live fields

Generic rules (see `KosDynNode_STACK`, `KosDynNode_NS`, `KosDynNode_IS_FRAME` in `dynchain.h` ~2127–2156):

| Field on `es` | Innermost provider on chain |
|---------------|----------------------------|
| `block` | Nearest node from `dyn_top` with non-NULL `KosDynNode_STACK` |
| `namespace` | Nearest with non-NULL `KosDynNode_NS` |
| `frame` / `code` / PCs | Nearest `KOS_DYN_FRAME` |

**Kinds that commonly allocate an operand stack** at creation (non-exhaustive; grep `KosDynNode_STACK` in `dynchain/`):

- `KOS_DYN_FRAME`, `KOS_DYN_TRY`, `KOS_DYN_FINALLY`, `KOS_DYN_LOOP`, `KOS_DYN_FOREACH`, `KOS_DYN_PROMPT`, `KOS_DYN_WITH`, …

**Kinds that commonly set namespace:**

- `KOS_DYN_FRAME`, `KOS_DYN_NS`, `KOS_DYN_COLLECT`, …

**Kinds that do not change frame** when pushed alone:

- TRY, FINALLY, LOOP, FOREACH, WIND, WITH, PROMPT, DELIM, etc. (unless also a frame)

Do **not** special-case kinds in Tier 1 beyond the generic STACK/NS/FRAME macros — that preserves behavior and stays DRY.

---

## 9. Straggler inventory (must not bypass Tier 1)

### 9.1 Direct `KosContext_DYN_TOP(ctx) =` assignments

| File | Notes |
|------|--------|
| `dynchain.c` | `kos_dyn_push`, `commit_dyn_top`, xfer INSTALL/PUSH, raise path ~2964, rewind ~1081 |
| `eval.c` | `KosContext_New` ~946; agen flatten ~1588; attach paths ~947 |
| `eval/eval_utils.c` | `KosEvalState_TailKoscallReplaceFrame` ~549–560 — **should** call Tier-1 commit helper after refactor |
| `dynchain/dyn_next.c` | ~130 conditional |

**Gate (from migration doc):** `rg 'KosContext_DYN_TOP\([^)]+\)\s*=' koskari/lib` → only `dynchain.c`, `eval.c`, and documented exceptions.

### 9.2 Mid-run readers of `ctx->frame_top` / `block` / `namespace`

| File | Usage |
|------|--------|
| `eval/eval_state.c` | Init, sync, refresh (implementation) |
| `eval/eval_chain.c` | ~263 namespace assign |
| `dynchain.c` | ~309 globals helper; ~4279 delim return |
| `eval.c` | `KosContext_New` ~947; agen ~1589 |
| `domain.c` | ~310 `KosContext_NAMESPACE` for scope display |

Audit each: switch to `KosEvalState_*` or document as post-freeze only.

### 9.3 Opcode post-push rebuild (**intentional**)

| File | Line (approx.) |
|------|----------------|
| `opcode/PUSH_WITH.c` | ~77 |
| `opcode/PUSH_DYN_WITH.c` | ~78 |
| `opcode/PUSH_COLLECT.c` | ~64 |

Each calls **`kos_dyn_rebuild_live_from_chain`** after assigning the operand stack on the dyn node **post-push**. Tier-1 `kos_dyn_retarget_after_push` runs before the stack slot exists, so these opcodes are **not** redundant; Phase 2 may add a narrow “stack slot now live” retarget instead of a full walk.

### 9.4 Module globals (`GLOBALS` / `IMPORT_REL`)

Resolved at opcode time by walking `KosNamespace_PARENT` from
`KosDynFrame_NS(KosEvalState_FRAME(es))` to the outermost parent. Not cached on
`KosContext`; not derived from `dyn_top` or inner `PUSH_NS` scope.

---

## 10. Phased implementation order

### Phase 1 (**done**)

| Step | Work | Status |
|------|------|--------|
| 1 | `kos_dyn_retarget_after_push` / `kos_dyn_retarget_after_pop`; wire push + `commit_dyn_top` | **Done** |
| 2 | Remove `SyncFromContext` from `dyn_*.c` success paths | **Done** (error paths remain) |
| 3 | Remove xfer plan-driver sync when steps leave `es` coherent | **Done** (`dynchain.c` ~4035–4038) |
| 4 | `KosEvalState_FreezeDerivedToContext` at eval cleanup | **Done** |
| — | Remove `PatchDynCaches` / `RefreshDynCaches` / `ApplyDynCaches` | **Done** |
| — | Update `eval.h` live-authority comments | **Done** |
| — | `tox -qe test` oracle | **Passed** (2724+ tests, suite complete) |

### Phase 2 (**open**)

| Step | Work | Risk |
|------|------|------|
| 5 | Fold `KosEvalState_TailKoscallReplaceFrame` into `kos_dyn_commit_dyn_top` (or equivalent Tier-1 publish); drop manual `dyn_top` + double rebuild | Medium |
| 5b | Replace `do_frame_prep` tail `SyncFromContext` with `KosEvalState_ResetFrameForTail` (or retarget in place) | Low |
| 6 | Tier 2 xfer INSTALL/PUSH: optional incremental retarget vs full rebuild | High — defer |
| 7 | Straggler audit: `dyn_top` assigns (§9.1), `ctx` derived readers (§9.2) | Medium |
| 8 | Opcode PUSH_* : narrow retarget after stack assign (optional) | Low |
| 9 | Tier-4-only `SyncFromContext` grep audit (§12.2) | Low |
| — | Optional: defer ctx derived writes to freeze-only once no RUN reader needs them | Low (follow-on) |

---

## 11. Transfer and prologue invariants (do not break)

These are **not** modified by this refactor but are the main source of **infinite loops** when `es` and `dyn_top` disagree.

1. **Plan action index** advances only on successful step completion; failed INSTALL/PUSH must not leave `dyn_top` pointing at freed memory.
2. **`KOS_OP_PROLOGUE()`** loop: `run_transfer_plan_to_completion` (`eval/eval_chain.c` ~358+) must see the same `es->frame` / `es->block` the plan expects for resume PC and resume stack graft.
3. **Async wind/with:** On `KOS_DYN_UNWIND_ASYNC_STARTED`, do not retarget `es` as if pop completed — `kos_dyn_uninstall_core` returns before `commit_dyn_top`.
4. **`kos_dyn_transition`:** Walkers assume current `es` matches `dyn_top`; changing retarget timing must not run **before** handlers that still believe the old top is active.
5. **Generator flatten:** Consumer `es` installs gen chain; agen `ctx->dyn_top` cleared (`eval.c` ~1588) — two contexts; do not sync across them incorrectly.

---

## 12. Verification

### 12.1 Required (correctness)

| Check | Command |
|-------|---------|
| **Full suite (oracle)** | `tox -qe test` — must complete **without hang** and **all pass** |

### 12.2 Required (structural hygiene before merge)

| Check | Command |
|-------|---------|
| No removed refresh APIs | `rg 'PatchDynCaches|RefreshDynCaches|ApplyDynCaches' koskari/lib` — **no matches** |
| Rebuild call sites | `rg 'kos_dyn_rebuild_live_from_chain' koskari/lib` — Tier 2, bootstrap, tail koscall, opcodes, pop fallback only |
| Sync call sites | `rg 'KosEvalState_SyncFromContext' koskari/lib` — Tier-4 only (Phase 2 audit; ~140 today) |
| dyn_top assigns | `rg 'KosContext_DYN_TOP\([^)]+\)\s*=' koskari/lib` — per §9.1 |

### 12.3 Optional (after tests green; performance)

| Check | Command |
|-------|---------|
| Trace | Rebuild with `KOS_TRACE_EVAL_SYNC=1`; compare `eval_state_sync` before/after ([tracing.md](../api/tracing.md)) |

### 12.4 Optional (only when debugging hard failures)

| Check | Notes |
|-------|--------|
| `KOS_DEBUG_PATCH_DYN_CACHES` | gdb-oriented build; drift vs full refresh — see §7.3 |

---

## 13. Key source files (quick index)

| Path | Role |
|------|------|
| `koskari/lib/eval.h` | `KosContext`, `KosEvalState`, macros, sync API declarations |
| `koskari/lib/eval/eval_state.c` | Init, Sync (repair), Freeze, stack-owner helpers |
| `koskari/lib/dynchain.c` | Tier 1 push/commit; **retarget/rebuild** helpers; xfer; transition |
| `koskari/lib/eval.c` | `koskari_eval`, cleanup, `KosContext_New`, agen attach |
| `koskari/lib/eval/eval_chain.c` | `kos_dyn_transition`, `run_transfer_plan_to_completion` |
| `koskari/lib/eval/eval_utils.c` | Tail koscall |
| `koskari/lib/dynchain.h` | Public `kos_dyn_*`; `kos_dyn_rebuild_live_from_chain`, `kos_dyn_commit_dyn_top` |
| `koskari/lib/dynchain/dyn_*.c` | Per-kind push/pop (no success-path scatter sync) |
| `koskari/lib/tracing.h` | `KOS_TRACE_EVAL_SYNC`, metric ids |
| `design/api/tracing.md` | Report shape for `eval_state_sync` |

---

## 14. Agent execution checklist

### Phase 1 (complete)

- [x] Read §1 — no transfer/transition logic changes
- [x] Implement retarget helpers in `dynchain.c` (§6)
- [x] Wire §5 Tier 1 (`kos_dyn_push`, `commit_dyn_top`, `push_noinstall`)
- [x] Run **`tox -qe test`** — full pass, no hang
- [x] Grep: no `PatchDynCaches` / `RefreshDynCaches` / `ApplyDynCaches`
- [x] Remove `SyncFromContext` from `dyn_*.c` success paths
- [x] `KosEvalState_FreezeDerivedToContext` at cleanup
- [x] Update `eval.h` comments (§3)
- [x] Keep opcode PUSH_* rebuild where stack is post-push (§9.3)

### Phase 2 (remaining)

- [ ] Fold tail koscall into Tier 1 (§10 step 5)
- [ ] Fix `do_frame_prep` tail reset without `SyncFromContext` (§9.1)
- [ ] Audit §9.1 `dyn_top` assigns and §9.2 `ctx` derived readers
- [ ] Optional: xfer INSTALL/PUSH incremental retarget (§10 step 6)
- [ ] Tier-4-only `SyncFromContext` grep audit (§12.2)
- [ ] Do **not** “optimize” transfer plans or re-add success-path sync to fix tests
- [ ] Performance: human runs `KOS_TRACE_EVAL_SYNC` comparison (§7.4, §12.3)

---

## Revision history

| Date | Change |
|------|--------|
| 2026-05-26 | Initial spec: live authority, choke points, parity constraints, locations |
| 2026-05-26 | §7/§12/§14: tests (no hang, full pass) as correctness oracle; tracing post-hoc; `KOS_DEBUG_*` for catastrophic diagnosis only |
| 2026-05-26 | Post Phase 1: §0 status, §4–§6 implemented APIs, §9.3 opcode rebuild, §10 split phases, §12–§14 aligned with `koskari/lib` |
