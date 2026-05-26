# Dyn-chain vs KosEvalState authority violations

**Status:** Audit ledger (code survey)  
**Audience:** Agents and humans working on live-authority cleanup  
**Repository root:** `sibilant-ng/`  
**Code survey:** `koskari/lib` as of 2026-05-26, branch `eval_state_truth` (post P4/doc-truth pass)  

**Related documents:**

| Document | Relationship |
|----------|----------------|
| [eval-state-live-authority.md](eval-state-live-authority.md) | Target authority model and Phase 1/2 plan |
| [../api/tracing.md](../api/tracing.md) | `eval_state_sync` trace buckets |
| [freeze-thaw-context-segments.md](freeze-thaw-context-segments.md) | Long-term freeze/thaw direction |

---

## 1. Premises and scope

This document records violations of the live-authority model for dynamic-chain transitions. The premises below are the audit standard; entries in §2 name code that diverges from them.

### 1.1 Premises (audit standard)

| Rule | Meaning |
|------|---------|
| **P1 — `es` during RUN** | `es->frame`, `es->code`, `es->block`, `es->namespace`, `es->pc`, `es->acting_pc` are the live registers opcodes and hot helpers use. |
| **P2 — `ctx` derived between drives** | `ctx->frame_top`, `ctx->block`, `ctx->namespace` are revive snapshots. During RUN they are NULL on ctx (stolen onto `es`); restored at eval exit via `KosEvalState_FreezeToContext`. |
| **P3 — `ctx` structural between drives** | `ctx->dyn_top`, `plan`, `plan_stack` are revive snapshots. During RUN they are NULL on ctx; live chain/plan live on `es` only (`KosEvalState_DYN_TOP`, `KosEvalState_PLAN`). GC while RUNNING uses TLS active `es` in `KosContext_Traverse`. |
| **P4 — flush on frame identity only** | `KosEvalState_FlushLiveRegsToFrame` runs when pushing or removing a **frame** (when `es->frame` identity changes). Region push/pop (LOOP, TRY, FOREACH, WITH, …) with unchanged frame must not flush live PCs to the heap frame. |
| **P5 — no success-path repair sync** | `KosEvalState_SyncFromContext` (`ctx` derived → `es` full repair) belongs on error/guard paths only, not on successful Tier-1 push/pop retarget. |
| **P6 — no dual publish** | Successful chain edits update `es` live fields at choke points. Writing `ctx->frame_top` / `block` / `namespace` on every retarget duplicates authority during RUN. |

### 1.2 Alignment with eval-state-live-authority.md

[eval-state-live-authority.md §3.1](eval-state-live-authority.md) states `es` live fields are authoritative during RUN and `ctx->frame_top` / `block` / `namespace` are not.

§3.2 diagram (lines 131–132) says RUN updates **`es` live fields there only**.

§3.2 “Current (Phase 1)” block (lines 150–151) says snapshot of ctx derived fields **only at freeze**.

[eval-state-live-authority.md](eval-state-live-authority.md) §0 now marks **`es`-only live fields during RUN**; `kos_dyn_snapshot_ctx_derived` is removed (H1, H17 resolved).

### 1.3 Scope

- **In scope:** `koskari/lib/dynchain.c`, `koskari/lib/dynchain/dyn_*.c`, eval lifecycle (`eval_state.c`, `eval.c`, `eval_chain.c`, `eval_utils.c`), opcode flush sites, trace bucket names.
- **Out of scope:** Remediation designs, test commands, behavioral changes to transfer plans or install/uninstall semantics.

---

## 2. Heresy catalog

Each entry: **ID**, status, premise, evidence. **Resolved** entries kept for audit history.

### Resolved (code or docs updated)

| ID | Summary | Resolution |
|----|---------|------------|
| H1 | `kos_dyn_snapshot_ctx_derived` on every retarget | API removed; Tier-1 updates `es` only |
| H2 | Unconditional flush in `push_noinstall` | No pre-push flush; `apply_live` flushes on frame change only |
| H3 | Unconditional flush in `kos_dyn_push` | Same as H2 |
| H4 | Unconditional flush in `uninstall_core` | Frame-conditional flush (matches `commit_dyn_top`) |
| H5 | `kos_dyn_push` dual-publish path | Uses `KosEvalState_DYN_TOP`; no ctx snapshot |
| H6 | Install failure `SyncFromContext` | `dyn_top` rollback only |
| H8 | Rebuild always snapshots ctx | Rebuild is `apply_live` only |
| H9 | `SyncFromContext` call sites | Removed; grep zero |
| H10 | Pop mismatch → sync repair | Fail-fast `int` return |
| H11 | Tail koscall ctx derived writes | `do_frame_prep` tail is es-only |
| H12 | Delimited pop return full rebuild | `kos_dyn_load_live_regs_from_frame` after `pop_delim` when `es->frame == ret_frame` |
| H15 | Pop flush conditional vs push unconditional | Push path matches pop (no extra flush) |
| H16 | Stale push retarget comment | Docstring updated in `dynchain.c` |
| H17 | Authority §0 vs RUN-only `es` | Authority doc §0 row updated |
| H19 | Removed APIs, surviving behavior | Snapshot/sync APIs gone; rebuild has no snapshot |

### Open (follow-up work)

| ID | Premises | Detail | Evidence |
|----|----------|--------|----------|
| H7 | Observability | Trace ends use `KOS_TEVS_patch_dyn_caches` / `KOS_TEVS_refresh_dyn_caches` though APIs are removed | `dynchain.c` retarget/rebuild trace ends |
| H13 | P2, P6 | Re-verify `eval_chain.c` for ctx derived writes during transition | `grep KosContext_FRAME_TOP` in `eval_chain.c` |
| H14 | P2 | Lifecycle exceptions: `KosContext_New` sets `frame_top` at bootstrap; agen flatten NULLs consumer ctx fields | `eval.c` ~963, ~1621 |
| H18 | Performance | LOOP push/pop dyn node + stack alloc cost per `while` extent | `PUSH_LOOP` / `POP_LOOP` opcodes |
| H20 | P4 | Unconditional flush before splice rewire in `kos_dyn_unlink_next_node`, `kos_dyn_unlink_yield_from`; `kos_dyn_gen_install` flushes before graft | `dyn_next.c` ~125, `dyn_yield_from.c` ~278, `dyn_gen.c` ~114 |
| H20b | P4 | Double flush when frame identity changes: `commit_dyn_top` + `apply_live` both may flush | `dynchain.c` ~856, ~548 |

---

## 3. Phase 1 omissions

| Item | Status |
|------|--------|
| Push-side unconditional flush | **Done** — `push_noinstall` has no pre-push flush |
| Tier-4 `SyncFromContext` | **Done** — API removed |
| Opcode PUSH_* post-push rebuild | **Done** — stack at node alloc |
| Splice-helper unconditional flush | **Open** — see H20 |

---

## 4. Appendix A — `KosEvalState_SyncFromContext` inventory

**Total call sites in `koskari/lib/**/*.c`:** **0** (API removed).

Historical per-file counts (pre-removal) are omitted. Gate:

```bash
grep -rn 'KosEvalState_SyncFromContext' koskari/lib
```

---

## 5. Appendix B — `KosEvalState_FlushLiveRegsToFrame` inventory

| File | Line | Notes |
|------|------|--------|
| `dynchain.c` | 548 | `kos_dyn_apply_live` when `ef != ft` |
| `dynchain.c` | 856 | `kos_dyn_commit_dyn_top` when removed == `es->frame` |
| `dynchain.c` | 885 | `kos_dyn_uninstall_core` when node == `es->frame` |
| `eval.c` | 633 | `kos_eval_emit_raise_from_pyerr` |
| `eval.c` | 2675 | `koskari_eval_cleanup` |
| `dyn_yield_from.c` | 210, 278 | deliver yield; unlink splice (H20) |
| `dyn_next.c` | 125 | unlink splice (H20) |
| `dyn_gen.c` | 114, 179 | gen install / unwind exhausted |
| `opcode/YIELD_FROM.c` | 181, 449 | opcode handler |

No flush in `kos_dyn_push` or `kos_dyn_push_noinstall`.

---

## 6. Appendix C — `ctx` derived field writes outside freeze/thaw

| File | Line | Assignment | Path |
|------|------|------------|------|
| `eval_state.c` | 107–113 | all three | `KosEvalState_FreezeToContext` (lifecycle OK) |
| `eval_state.c` | 61–68 | steal to `es` | `KosEvalState_ThawFromContext` (lifecycle OK) |
| `eval.c` | 963 | `FRAME_TOP` | `KosContext_New` bootstrap (lifecycle OK) |
| `eval.c` | 1621 | `FRAME_TOP = NULL` | agen flatten attach |

`do_frame_prep` tail path does not write ctx derived fields.

---

## 7. Appendix D — `kos_dyn_snapshot_ctx_derived`

**Removed.** No call sites in `koskari/lib`.

---

## 8. Verification commands (regenerate inventories)

```bash
grep -rn 'KosEvalState_SyncFromContext\|kos_dyn_snapshot_ctx_derived\|PatchDynCaches\|RefreshDynCaches\|ApplyDynCaches' koskari/lib
grep -rn 'KosEvalState_FlushLiveRegsToFrame' koskari/lib
grep -rn 'KosContext_FRAME_TOP\|KosContext_BLOCK\|KosContext_NAMESPACE' koskari/lib | grep '='
grep -rn 'kos_dyn_rebuild_live_from_chain' koskari/lib
```

---

## 9. Document maintenance

- Re-run §8 commands after any dynchain or eval-state change.
- Add heresy IDs; do not renumber existing IDs without cause.
- This document is audit-only; remediation belongs in separate plans.
