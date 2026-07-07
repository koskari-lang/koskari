# KosEvalState live authority and chain-transition funnel

**Status:** Phase 1 **landed** (live authority + Tier-1 funnel); Phase 2 **open** (§10)  
**Audience:** Agents and humans implementing performance work on the evaluator  
**Repository root:** `sibilant-ng/`  
**Code survey:** `koskari/lib` as of 2026-05-26 (post Phase 1)  

**Related documents:**

| Document | Relationship |
|----------|----------------|
| [freeze-thaw-context-segments.md](freeze-thaw-context-segments.md) | Long-term freeze/thaw and C-local execution model |
| [../archive/kos_eval_state_api_migration.md](../archive/kos_eval_state_api_migration.md) | Opcode/API migration off `KosContext_*` macros (archived ledger) |
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
| `es`-only live fields during RUN | **Done** | `kos_dyn_apply_live` at Tier-1 retarget; ctx derived via freeze only |
| Freeze at eval exit | **Done** | `KosEvalState_FreezeToContext` in `eval_state.c`; `koskari_eval_cleanup` in `eval.c` |
| `PatchDynCaches` / `RefreshDynCaches` / `ApplyDynCaches` | **Removed** | Replaced by dynchain helpers above |
| `KosEvalState_SyncFromContext` | **Removed** | Fail-fast pops; rebuild only on shape change (Tier 1–2) |
| Xfer step completion sync | **Removed** | Plan driver comment: steps leave `es` coherent via rebuild |
| WITH/COLLECT stack at node alloc | **Done** | `dyn_with.c` / `dyn_collect.c`; opcodes do not rebuild (§9.3) |
| Delimited pop return retarget | **Done** | `kos_dyn_finish_delimited_pop_return` load-or-rebuild after `pop_delim` |

**Phase 2 (storage relocation, done):** `es->dyn_top`, `plan`, `plan_stack` with steal-on-thaw / freeze-restore (`KosEvalState_ThawFromContext`, `KosEvalState_FreezeToContext`); `KosEvalState_DYN_TOP` never reads ctx during RUN; TLS active-`es` for `KosContext_Traverse` while `KOS_CTX_RUNNING`. Remaining open: Tier-2 xfer incremental retarget; §9.1/§9.2 straggler audits. See §10.

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
- **Removed** `KosEvalState_SyncFromContext`; use `kos_dyn_retarget_after_*` or `kos_dyn_rebuild_live_from_chain` only when the chain shape changes
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
| **DRY** | One funnel for live-field updates on push/pop/commit; no error-path full-chain rebuild |
| **Future work** | Clear `es`-authoritative model enables C-local stack optimizations (see freeze-thaw doc) |

**Success metrics:**

| Metric | Phase 1 | Phase 2 target |
|--------|---------|----------------|
| **`tox -qe test`** (no hang, full pass) | **Met** | Keep met |
| No `PatchDynCaches` / `RefreshDynCaches` in lib | **Met** | Keep met |
| No `SyncFromContext` in `koskari/lib` | **Met** | `grep` gate zero; mismatch pops return `-1` |
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
| Push retarget | `kos_dyn_retarget_after_push` updates **`es` only** via `kos_dyn_apply_live` |
| Removed APIs | `PatchDynCaches`, `RefreshDynCaches`, `ApplyDynCaches` deleted from `eval_state.c` / `eval.h` |
| `eval.h` wording | Updated: `es` authoritative during RUN; `ctx->frame_top` / `block` / `namespace` are revive snapshots |
| Scatter sync on pop/push success | Removed from most `dyn_*.c` success paths; **not** zeroed out globally |

### 4.3 `KosEvalState_SyncFromContext` (removed)

The repair API is **deleted** from `koskari/lib`. Structural hygiene gate:

```bash
grep -rn 'KosEvalState_SyncFromContext' koskari/lib
```

must return **no matches**. Mismatched `kos_dyn_pop_*` calls set `PyErr` and return `-1` without rebuilding live fields. Do **not** re-add success-path sync to “fix” tests.

---

## 5. Choke points (only places that may retarget live fields)

All live-field updates for chain shape changes must funnel through these tiers. **Do not** add `SyncFromContext` or ad-hoc full-chain walks elsewhere.

### Tier 1 — Primary publishers (**implemented**)

| Symbol | File | Lines (approx.) | When used |
|--------|------|-----------------|-----------|
| `kos_dyn_push` | `dynchain.c` | ~859–879 | Push + `kos_dyn_install` → `kos_dyn_retarget_after_push` |
| `kos_dyn_push_noinstall` | `dynchain.c` | ~904–920 | Region nodes (TRY, LOOP, …) → retarget after push |
| `kos_dyn_commit_dyn_top` | `dynchain.c` | ~892–901 | Pop, splice head, uninstall commit → `kos_dyn_retarget_after_pop`; no pre-flush (H20b) |
| `kos_dyn_uninstall_core` (via `kos_dyn_pop_*` entry points) | `dynchain.c` | ~1070+ | Handler then commit; frame flush only in `apply_live` via retarget (H20b) |

**Retarget implementation:** `static` helpers in `koskari/lib/dynchain.c` (§6). **`kos_dyn_rebuild_live_from_chain`** is the only exported rebuild API (`dynchain.h`).

**Lifecycle in eval TU:** `KosEvalState_Init`, `KosEvalState_ThawFromContext`, `KosEvalState_FreezeToContext` in `eval/eval_state.c`; declared in `eval.h`.

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
| `xfer_step_install` | `dynchain/xfer_plan.c` | static | Replaces entire `dyn_top` segment via `xfer_step_land_segment` (`graft_parent == NULL`) |
| `xfer_step_push` | `dynchain/xfer_plan.c` | static | Grafts segment onto `dyn_top` via `xfer_step_land_segment` (non-NULL `graft_parent`) |
| `kos_dyn_finish_delimited_pop_return` | `dynchain.c` | ~4301+ | After `pop_delim`: `kos_dyn_load_live_regs_from_frame` when `es->frame == ret_frame`, else rebuild; else-branch when DELIM not dyn_top |
| `kos_dyn_unlink_next_node` (non–dyn_top) | `dyn_next.c` | ~130–141 | Splices parent links without `commit_dyn_top`; no pre-flush before rewire (P4; H20) |
| `KosContext_New` bootstrap | `eval.c` | ~985 | Thaw/freeze bootstrap after `KosEvalState_Init` |
| `do_frame_prep` tail recycle | `eval_utils.c` | ~383–390 | In-place `es` frame reset (no ctx derived writes) |
| Gen install / xfer fallback inside pop retarget | `dynchain.c` | `kos_dyn_retarget_after_pop` | `new_top != parent(removed)` |

**Rule:** Do not add new Tier-2 sites. Optional Phase 2 work: incremental retarget for xfer INSTALL/PUSH (high risk; defer until tail koscall uses Tier 1).

### Tier 3 — Lifecycle (thaw / freeze)

| Site | File | Lines (approx.) | Role |
|------|------|-----------------|------|
| `KosEvalState_Init` | `eval/eval_state.c` | 27–49 | Thaw `es` from `ctx` + frame at entry |
| `koskari_eval` entry | `eval.c` | (search `KosEvalState_Init`) | Build stack `es` |
| `koskari_eval_cleanup` | `eval.c` | ~2641 | `FlushLiveRegsToFrame` then **`KosEvalState_FreezeDerivedToContext`** |
| `kos_eval_emit_raise_from_pyerr` | `eval.c` | ~616–617 | Flush before traceback when `es` given |

### Tier 4 — Removed (`SyncFromContext` audit complete)

`KosEvalState_SyncFromContext` is deleted. Detected inconsistencies (`dyn_top !=` expected node, validation failure before useful work) set `PyErr` and return `-1` without rebuilding live fields. `kos_dyn_push` rolls back `dyn_top` on install failure. `kos_dyn_pop_*` helpers return `int`. Non-top yield-from cleanup uses `kos_dyn_unlink_yield_from`.

---

## 6. Live retarget API (implemented)

Phase 1 placed retarget logic in the **dynchain translation unit** (`dynchain.c`), not as public `KosEvalState_*` retarget entry points. Only **`kos_dyn_rebuild_live_from_chain`** is exported from `dynchain.h` for bootstrap, tail koscall, Tier-2 paths, and opcodes.

### 6.1 `static kos_dyn_apply_live(es, ft, bl, ns)`

Writes live `frame`, `block`, `namespace` (and `code` / PCs on frame change) on **`es`**. On frame pointer change, flushes live PCs to the outgoing frame before loading from the new frame. Call only from other retarget helpers after `dyn_top` is correct.

### 6.2 `static kos_dyn_walk_live_from(es, start, …)`

Parent walk from `start` for innermost frame, stack slot, and namespace. Used by incremental pop retarget and by rebuild.

### 6.3 `static kos_dyn_retarget_after_push(es, new_node)`

**Replaces:** `KosEvalState_PatchDynCaches`. O(1) after single-node push when `new_node == dyn_top`. Frame nodes replace frame/code/block/ns; other nodes update only slots they carry. With `KOS_DEBUG_PATCH_DYN_CACHES`, compares against full rebuild in debug builds.

### 6.4 `static kos_dyn_retarget_after_pop(es, removed, new_top)`

**Replaces:** full refresh inside `kos_dyn_commit_dyn_top` for the common case.

**Critical fallback (implemented):** if `!removed` or `new_top != KosDynNode_PARENT(removed)`, call **`kos_dyn_rebuild_live_from_chain`** (required for gen install, splices, xfer-shaped head changes).

Otherwise update only slots the removed node owned; one `kos_dyn_walk_live_from` when needed. When the removed node owned `es->frame`, **`kos_dyn_apply_live`** flushes live PCs to the outgoing heap frame (H20b: not in `commit_dyn_top` or `uninstall_core`).

### 6.5 `void kos_dyn_rebuild_live_from_chain(es)` (public)

Full walk from `KosEvalState_DYN_TOP(es)`, then `kos_dyn_apply_live`. Tier 2 and pop fallback only on hot paths.

### 6.6 `void KosEvalState_FreezeDerivedToContext(es)` (`eval_state.c`)

Called from `koskari_eval_cleanup` after `FlushLiveRegsToFrame`:

```text
ctx->frame_top = es->frame;
ctx->block     = es->block;
ctx->namespace = es->namespace;
```

### 6.7 Removed / retained eval TU APIs

| API | Fate (Phase 1) |
|-----|----------------|
| `KosEvalState_SyncFromContext` | **Removed** |
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

**`KOS_TRACE_EVAL_SYNC`** and `eval_state_sync` in `get_trace_report()` ([tracing.md](../api/tracing.md)) measure call counts and wall time per retarget/rebuild/flush sites. Use **after** tests pass to compare `retarget_after_push` and `rebuild_live_from_chain` traffic — not as a substitute for functional tests.

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

### 9.1 Direct `KosContext_DYN_TOP(ctx) =` assignments (audit 2026-05-26)

| File | Line (approx.) | Role |
|------|----------------|------|
| `eval/eval_state.c` | thaw/freeze | Steal/restore `dyn_top` between drives |
| `eval.c` | ~962 | `KosContext_New` bootstrap |
| `eval.c` | ~1008, ~1620 | Context free; agen flatten clears agen ctx chain head |

During RUN, `KosEvalState_DYN_TOP(es)` is authoritative; no assigns in `dynchain.c` or `dynchain/`.

**Gate:** `grep -rnE 'KosContext_DYN_TOP\([^)]+\)[[:space:]]*=' koskari/lib` → rows above only.

### 9.2 Mid-run readers of `ctx->frame_top` / `block` / `namespace` (audit 2026-05-26)

| File | Usage |
|------|--------|
| `eval/eval_state.c` | Thaw steals derived fields onto `es`; freeze restores to `ctx` |
| `eval.c` | `KosContext_New` bootstrap ~963; agen flatten ~1621 |
| `domain.c` | ~347–350 snapshot: TLS `active_es` namespace first, else frozen `ctx` field |

No mid-RUN derived writes in `eval_chain.c` or `dynchain.c` (H13). Opcodes use `KosEvalState_*` during RUN.

### 9.3 WITH / COLLECT operand stacks (**at node allocation**)

`kos_dyn_with_allocate` and `kos_dyncollect_new` allocate `KosDynNode_STACK` when the node is created (same pattern as TRY). **`PUSH_WITH`**, **`PUSH_DYN_WITH`**, and **`PUSH_COLLECT`** do not call `kos_dyn_rebuild_live_from_chain`; Tier-1 `kos_dyn_retarget_after_push` sees the stack slot after push.

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

### Phase 2 (**partial**)

| Step | Work | Status |
|------|------|--------|
| 5 | Tail koscall / `do_frame_prep` ctx derived writes | **Done** — es-only tail recycle in `eval_utils.c` |
| 5b | `SyncFromContext` removal | **Done** — grep gate zero |
| — | Delimited pop return incremental retarget | **Done** — `kos_dyn_load_live_regs_from_frame` |
| 6 | Tier 2 xfer INSTALL/PUSH: incremental retarget vs full rebuild | **Open** — high risk, defer |
| 7 | Straggler audit: splice-helper unconditional flush (`dyn_next`, `dyn_yield_from`, `dyn_gen` install) | **Done** — H20; pre-splice flush removed |
| 8 | Straggler audit: `dyn_top` assigns (§9.1), `ctx` derived readers (§9.2) | **Done** — grep pass 2026-05-26; H13/H14 |
| — | Trace bucket rename (`patch_dyn_caches` labels) | **Done** — H7 |

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
| No removed refresh APIs | `grep -rn 'PatchDynCaches\|RefreshDynCaches\|ApplyDynCaches' koskari/lib` — **no matches** |
| Rebuild call sites | `grep -rn 'kos_dyn_rebuild_live_from_chain' koskari/lib` — Tier 2, bootstrap, tail koscall, opcodes, pop fallback only |
| Sync call sites | `grep -rn 'KosEvalState_SyncFromContext' koskari/lib` — **no matches** |
| dyn_top on ctx during RUN | `grep -rn 'KosContext_DYN_TOP' koskari/lib/dynchain` — **no matches** |
| dyn_top assigns | `grep -rnE 'KosContext_DYN_TOP\([^)]+\)[[:space:]]*=' koskari/lib` — per §9.1 |

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
| `koskari/lib/eval/eval_state.c` | Init, Thaw, Freeze, stack-owner helpers |
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
- [x] WITH/COLLECT stack at node alloc; no opcode post-push rebuild (§9.3)

### Phase 2 (remaining)

- [x] Tail `do_frame_prep` es-only (no ctx derived writes during RUN)
- [x] `SyncFromContext` removed; fail-fast pops (§12.2)
- [x] Delimited pop return load-or-rebuild after `pop_delim`
- [x] Native harness thaw/freeze documented (`tests/native/README.md`)
- [x] Audit splice-helper unconditional flush (violations doc H20 follow-up)
- [x] Audit §9.1 `dyn_top` assigns and §9.2 `ctx` derived readers
- [ ] Optional: xfer INSTALL/PUSH incremental retarget (§10 step 6)
- [ ] Do **not** “optimize” transfer plans or re-add success-path sync to fix tests
- [ ] Performance: human runs `KOS_TRACE_EVAL_SYNC` comparison (§7.4, §12.3)

---

## Revision history

| Date | Change |
|------|--------|
| 2026-05-26 | Initial spec: live authority, choke points, parity constraints, locations |
| 2026-05-26 | §7/§12/§14: tests (no hang, full pass) as correctness oracle; tracing post-hoc; `KOS_DEBUG_*` for catastrophic diagnosis only |
| 2026-05-26 | Post Phase 1: §0 status, §4–§6 implemented APIs, §9.3 opcode rebuild, §10 split phases, §12–§14 aligned with `koskari/lib` |
