# KosEvalState live authority and chain-transition funnel

**Status:** Implementation spec (bookkeeping refactor)  
**Audience:** Agents and humans implementing performance work on the evaluator  
**Repository root:** `sibilant-ng/`  
**Survey baseline:** `koskari/lib` as of 2026-05-26  

**Related documents:**

| Document | Relationship |
|----------|----------------|
| [freeze-thaw-context-segments.md](freeze-thaw-context-segments.md) | Long-term freeze/thaw and C-local execution model |
| [../kos_eval_state_api_migration.md](../kos_eval_state_api_migration.md) | Opcode/API migration off `KosContext_*` macros (orthogonal surface cleanup) |
| [../EVALUATOR.md](../EVALUATOR.md) | Eval loop, prologue, transfer plans |
| [../api/tracing.md](../api/tracing.md) | `eval_state_sync` counters (`KOS_TRACE_EVAL_SYNC`) |
| [../INVARIANTS-AND-CLARIFICATIONS.md](../INVARIANTS-AND-CLARIFICATIONS.md) | Global execution invariants |

---

## How to use this document

| If you need to… | Read |
|-----------------|------|
| Understand what is allowed to change | §1 (non-goals), §2 (goals) |
| Learn the authority model (`es` vs `ctx`) | §3 |
| See why the current code is slow | §4 |
| Know where to implement updates | §5 (choke points), §6 (proposed APIs) |
| Match existing behavior exactly | §7 (correctness), §12 (verification) |
| Avoid infinite prologue/transfer loops | §13 |
| Verify your work | §14 |
| Execute as an agent | §15 (checklist) |

**Do not** use this document to change transfer decisions, transition kinds, plan steps, or node install/uninstall semantics. See §1.

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

**Success metrics (after implementation):**

- **`tox -qe test` completes without hang and with full pass** — this is the **correctness oracle** (§7)
- Grep hygiene: `SyncFromContext` only at Tier-4 sites; `RefreshDynCaches` / rebuild only at Tier-2 + fallback (§12)

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

**Wrong (today’s steady-state pattern):**

```
mutate dyn_top → walk chain → write ctx->frame_top/block/namespace
              → ApplyDynCaches (ctx → es) → SyncFromContext (ctx → es again)
```

**Right (target):**

```
mutate dyn_top → update es live fields at choke point
              → (optional) snapshot ctx derived fields only at freeze
```

### 3.3 Opcode hot path

Opcodes already use `KosEvalState_BLOCK(es)`, `KosEvalState_FRAME(es)`, etc. (`koskari/lib/eval.h`). They must **not** gain chain logic or sync calls.

---

## 4. Current problems (baseline)

### 4.1 Scattered sync

Approximate `KosEvalState_SyncFromContext` call counts (grep `koskari/lib`, 2026-05-26):

| File | Calls |
|------|------:|
| `koskari/lib/dynchain.c` | 127 |
| `koskari/lib/dynchain/dyn_with.c` | 32 |
| `koskari/lib/dynchain/dyn_wind.c` | 13 |
| `koskari/lib/dynchain/dyn_next.c` | 7 |
| `koskari/lib/dynchain/dyn_yield_from.c` | 6 |
| `koskari/lib/dynchain/dyn_foreach.c` | 5 |
| `koskari/lib/eval/eval_chain.c` | 3 |
| Other `dyn_*.c` | 1 each (many files) |
| `koskari/lib/eval/eval_utils.c` | 1 |
| `koskari/lib/utils.c` | 1 |

Most success-path calls are **legacy belt-and-suspenders** from the pre-`KosEvalState` era.

### 4.2 Full refresh on every pop

`kos_dyn_commit_dyn_top` (`dynchain.c` ~563–572) always calls `KosEvalState_RefreshDynCaches`, which:

1. Walks from `ctx->dyn_top` toward parents
2. Writes `ctx->frame_top`, `ctx->block`, `ctx->namespace`
3. Calls `KosEvalState_ApplyDynCaches` (`ctx` → `es`)

Every `kos_dyn_pop_*` that ends in `commit_dyn_top` pays **O(chain depth)** even for a single-head pop of a node that only owned namespace.

### 4.3 Push is half-correct

`kos_dyn_push` / `kos_dyn_push_noinstall` use `KosEvalState_PatchDynCaches` (O(1)) but still write **`ctx` first**, then `ApplyDynCaches`. The logic should target **`es` first**; `ctx` snapshot is secondary.

### 4.4 Misleading documentation

`koskari/lib/eval.h` (~449–465) still says `KosContext` is “authoritative for dyn_top, frame_top, and block” during RUN. Update comments when implementing this spec (wording only in the same PR as behavior funnel).

---

## 5. Choke points (only places that may retarget live fields)

All live-field updates for chain shape changes must funnel through these tiers. **Do not** add `SyncFromContext` / `RefreshDynCaches` elsewhere.

### Tier 1 — Primary publishers (extend these)

| Symbol | File | Lines (approx.) | When used |
|--------|------|-----------------|-----------|
| `kos_dyn_push` | `koskari/lib/dynchain.c` | 539–555 | Push + `kos_dyn_install` |
| `kos_dyn_push_noinstall` | `koskari/lib/dynchain.c` | 578–591 | Region nodes (TRY, LOOP, …) |
| `kos_dyn_commit_dyn_top` | `koskari/lib/dynchain.c` | 563–572 | Pop, splice head, uninstall commit |
| `kos_dyn_uninstall` / `kos_dyn_uninstall_core` | `koskari/lib/dynchain.c` | 594–641 | Must **not** add extra sync on success |

**Implementation lives in:** `koskari/lib/eval/eval_state.c` (included into `eval.c`), declared in `koskari/lib/eval.h`.

**Callers that must stay thin:** Every `kos_dyn_push_*` / `kos_dyn_pop_*` in `koskari/lib/dynchain/dyn_*.c` — they only allocate/link and call Tier 1; **no** `SyncFromContext` on success after this work.

Examples of pop wrappers (already use `commit_dyn_top` only):

- `kos_dyn_pop_try` — `dynchain/dyn_try.c` ~91–104
- `kos_dyn_pop_with` — `dynchain/dyn_with.c` ~267–279
- `kos_dyn_pop_loop` — `dynchain/dyn_loop.c` ~100–113
- `kos_dyn_pop_frame` — `dynchain/dyn_frame.c` ~180–247

### Tier 2 — Bulk chain surgery (full recompute allowed)

Documented exceptions where **incremental pop/push rules are insufficient**. These may call a single `KosEvalState_RebuildLiveFromChain(es)` (rename of today’s refresh walk) **internally**:

| Site | File | Lines (approx.) | Why |
|------|------|-----------------|-----|
| `xfer_step_install` | `dynchain.c` | 3325–3408 | Replaces entire `dyn_top` segment |
| `xfer_step_push` | `dynchain.c` | 3418–3479 | Grafts segment; may refresh twice if resume stack grafted (~3369, ~3464) |
| `kos_dyn_deliver_*` / delim return | `dynchain.c` | ~4279–4282 | Direct `ctx->frame_top` assign + `pop_delim` + refresh |
| `kos_dyn_next` reattach | `dynchain/dyn_next.c` | ~130–141 | Conditional `commit` + refresh |
| `KosContext_New` bootstrap | `eval.c` | 972–976 | One-time `Init` + refresh |
| `kos_attach_agen_gen_flatten` path | `eval.c` | ~1588+, `dyn_gen.c` | Cross-context chain install |

**Rule:** Tier 2 sites should be **audited in one PR** or left on full rebuild until Tier 1 pop/push is proven. Do not add new Tier-2 sites.

### Tier 3 — Lifecycle (thaw / freeze)

| Site | File | Lines (approx.) | Role |
|------|------|-----------------|------|
| `KosEvalState_Init` | `eval/eval_state.c` | 27–49 | Thaw `es` from `ctx` + frame at entry |
| `koskari_eval` entry | `eval.c` | (search `KosEvalState_Init`) | Build stack `es` |
| `koskari_eval_cleanup` | `eval.c` | ~2639–2642 | `FlushLiveRegsToFrame` today; add **`FreezeDerivedToContext`** (§6) |
| `kos_eval_emit_raise_from_pyerr` | `eval.c` | ~616–617 | Flush before traceback when `es` given |

### Tier 4 — Error recovery only (`SyncFromContext`)

Keep `KosEvalState_SyncFromContext` **only** where the chain or `ctx` may be inconsistent after failure:

| Site | Reason |
|------|--------|
| `kos_dyn_push` install failure | `dynchain.c` ~550 |
| `kos_dyn_uninstall` failure | `dynchain.c` ~638 |
| `KosEvalState_Transition` / `kos_dyn_transition` on `-1` | `eval/eval_chain.c` ~166; migration doc D3 |
| Straggler `dyn_top` writes until Phase F folded | See §9 |

---

## 6. Proposed APIs (implement in `eval_state.c`)

Names are suggestions; pick consistent `KosEvalState_*` prefixes.

### 6.1 `KosEvalState_RetargetAfterPush(es, new_top)`

**Replaces:** `KosEvalState_PatchDynCaches` + the `ctx`-first writes inside it.

**Preconditions:** `new_top == KosContext_DYN_TOP(ctx)`; flush already done by caller.

**Behavior (must match today’s `PatchDynCaches` + `ApplyDynCaches`):**

- If `KosDynNode_IS_FRAME(new_top)`:
  - `es->frame` = frame; `es->code` = frame code; load `pc`/`acting_pc` from frame (or keep live if same frame — see `ApplyDynCaches`)
  - `es->block` = `&KosDynNode_STACK(new_top)`
  - `es->namespace` = `KosDynNode_NS(new_top)` or frame ns
- Else:
  - If `KosDynNode_NS(new_top)`: `es->namespace` = that ns
  - If `KosDynNode_STACK(new_top)`: `es->block` = `&KosDynNode_STACK(new_top)`
  - Do **not** clear frame/code if not a frame node

**Do not** change whether `kos_dyn_install` runs (still `kos_dyn_push` only).

### 6.2 `KosEvalState_RetargetAfterPop(es, removed, new_top)`

**Replaces:** full `RefreshDynCaches` inside `kos_dyn_commit_dyn_top` for the common case.

**Preconditions:** `removed` was old `dyn_top`; `new_top` is parent (or NULL); flush already done.

**Algorithm:**

For each live slot, update **only if the removed node was the current provider**:

| Slot | Removed was provider when… | Action |
|------|---------------------------|--------|
| `es->block` | `es->block == &KosDynNode_STACK(removed)` (and removed had stack) | Walk `new_top`, parents until `KosDynNode_STACK(n)`; assign `es->block` or NULL |
| `es->namespace` | `es->namespace == KosDynNode_NS(removed)` (and removed had ns) | Same walk for `KosDynNode_NS` |
| `es->frame` / `es->code` / PCs | `es->frame == (KosDynFrame *)removed` | Walk for nearest `KOS_DYN_FRAME`; on change, flush outgoing frame PCs to heap, load from new frame |

If multiple slots need parent walk, **one walk** can fill all three (same termination as `RefreshDynCaches`: stop when frame, block, ns all resolved or chain ends).

**Fallback:** If `removed` is NULL or pop is not single-head (caller knows splice), call `KosEvalState_RebuildLiveFromChain(es)` (full walk — today’s `RefreshDynCaches` logic targeting **`es` first**, then optional ctx snapshot).

### 6.3 `KosEvalState_RebuildLiveFromChain(es)`

**Equivalent to:** today’s `KosEvalState_RefreshDynCaches` + `ApplyDynCaches`, but:

1. Walk sets **`es`** fields (and optionally `ctx` snapshot at end)
2. Used only from Tier 2 and fallback

Semantically equivalent to today’s full refresh path; useful as a **reference implementation** when implementing incremental retarget, not as the gate for “ship it.”

### 6.4 `KosEvalState_FreezeDerivedToContext(es)`

**New.** Called from `koskari_eval_cleanup` (and any path that leaves RUN while context stays alive):

```text
ctx->frame_top = es->frame;
ctx->block     = es->block;
ctx->namespace = es->namespace;
```

After `FlushLiveRegsToFrame`. Ensures next `Init` sees consistent snapshot.

### 6.5 Deprecate / shrink

| API | Fate |
|-----|------|
| `KosEvalState_SyncFromContext` | Tier 4 only; document forbidden elsewhere |
| `KosEvalState_PatchDynCaches` | Rename → `RetargetAfterPush` or thin alias |
| `KosEvalState_RefreshDynCaches` | Rename → `RebuildLiveFromChain`; Tier 2 + fallback only |
| `KosEvalState_ApplyDynCaches` | Inline into retarget helpers or keep as frame-change PC logic only |

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

When `es->frame` pointer changes, flush live `pc`/`acting_pc` to the **outgoing** heap frame, then load from the new frame (`KosEvalState_ApplyDynCaches` today, ~258–273 in `eval_state.c`). Violations surface as wrong PC or wrong code object in tests, not via debug flags alone.

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

### 9.3 Opcode-level refresh (remove after Tier 1 push funnel)

| File | Line |
|------|------|
| `opcode/PUSH_WITH.c` | ~77 |
| `opcode/PUSH_DYN_WITH.c` | ~78 |
| `opcode/PUSH_COLLECT.c` | ~64 |

These run **after** `kos_dyn_push_*` already patched caches — redundant once push funnel is authoritative on `es`.

### 9.4 `globals_frame` / `globals_cache_version`

Still updated on frame identity change (xfer INSTALL/PUSH, `kos_dyn_globals_frame`). **Separate** from live `es` fields; keep behavior, do not conflate with block/namespace retarget.

---

## 10. Phased implementation order (suggested)

| Step | Work | Risk |
|------|------|------|
| 1 | Add `RetargetAfterPush` / `RetargetAfterPop`; wire `commit_dyn_top` + pushes; `tox -qe test` | Medium |
| 2 | Remove `SyncFromContext` from `dyn_*.c` success paths (mechanical grep) | Low if step 1 correct |
| 3 | Remove xfer async `SyncFromContext` where comment says es coherent (`dynchain.c` ~3680–3694) | Medium — test shift/return |
| 4 | Add `FreezeDerivedToContext` at eval cleanup; narrow `Init` thaw | Low |
| 5 | Fold `TailKoscallReplaceFrame` into Tier 1 | Medium |
| 6 | Tier 2 xfer INSTALL/PUSH: optional incremental later | High — defer |

**One PR preference:** Step 1–2 together with full `tox -qe test`.

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
| Sync call sites remaining | `rg 'KosEvalState_SyncFromContext' koskari/lib` — only Tier-4 |
| Refresh outside Tier 2 | `rg 'KosEvalState_RefreshDynCaches' koskari/lib` — only Tier-2, bootstrap, fallback |
| dyn_top assigns | `rg 'KosContext_DYN_TOP\([^)]+\)\s*=' koskari/lib` — per §9 |

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
| `koskari/lib/eval/eval_state.c` | Init, Sync, Refresh, Patch, Apply — **primary edit target** |
| `koskari/lib/eval.c` | `koskari_eval`, cleanup, `KosContext_New`, agen attach |
| `koskari/lib/eval/eval_chain.c` | `kos_dyn_transition`, `run_transfer_plan_to_completion` |
| `koskari/lib/eval/eval_utils.c` | Tail koscall |
| `koskari/lib/dynchain.c` | Tier 1 push/commit/uninstall; xfer steps; transition |
| `koskari/lib/dynchain.h` | Public `kos_dyn_*` API |
| `koskari/lib/dynchain/dyn_*.c` | Per-kind push/pop — remove scatter sync |
| `koskari/lib/tracing.h` | `KOS_TRACE_EVAL_SYNC`, metric ids |
| `design/api/tracing.md` | Report shape for `eval_state_sync` |

---

## 14. Agent execution checklist

- [ ] Read §1 — confirm no transfer/transition logic changes
- [ ] Implement §6 APIs in `eval_state.c` only (initially)
- [ ] Wire §5 Tier 1 (`dynchain.c` push/commit/push_noinstall)
- [ ] Run **`tox -qe test`** — must not hang; full pass (§7.1)
- [ ] Run §12.2 grep hygiene
- [ ] Grep-remove `SyncFromContext` from success paths in `dyn_*.c` (§4.1 list)
- [ ] Remove opcode redundant refresh (§9.3)
- [ ] Add `FreezeDerivedToContext` at cleanup (§6.4)
- [ ] Update `eval.h` comments to live-authority model (§3)
- [ ] Do **not** “optimize” transfer plans or combine unwind steps
- [ ] On hang or wrong behavior: §7.2 triage; optional `KOS_DEBUG_*` / gdb (§7.3) — not required when tests are green
- [ ] Performance: human runs trace comparison after merge (§7.4, §12.3)

---

## Revision history

| Date | Change |
|------|--------|
| 2026-05-26 | Initial spec: live authority, choke points, parity constraints, locations |
| 2026-05-26 | §7/§12/§14: tests (no hang, full pass) as correctness oracle; tracing post-hoc; `KOS_DEBUG_*` for catastrophic diagnosis only |
