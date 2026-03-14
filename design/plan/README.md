# Generator Flatten Implementation Plan

## Goals

1. **Python-driven (agen)**: `__anext__` / `asend` drive via `koskari_eval(agen->ctx)`. On yield: freeze (store active on GEN, release GEN to agen). On resume: install GEN's active onto ctx.

2. **Koskari-flattened**: NEXT/ITER_NEXT on KosPyAsyncGen install GEN (not just frame) onto consumer's chain. Same freeze on yield; value flows to consumer in-loop.

3. **Unified install helper**: One function `kos_dyn_gen_install(ctx, gen)` usable for both agen revive and koskari flatten.

4. **Refcount on GEN**: Manual acquire/release. On freeze we unlink (do not pop); release balances install's acquire. Owner holds ref between yields.

## Current State (Broken)

- Koskari-to-koskari: attaches frame directly; GEN never on consumer chain; inconsistent `agen->ctx` after attach.
- Python-to-koskari: works (chain stays intact).
- No `active` field; no freeze/release; no install helper.

## Architecture Overview

```
Activation:
  - agen: revive GEN, attach active to ctx
  - koskari: install GEN onto consumer ctx

Execution:
  - Resume context with gen frame

Yield:
  - Unwind to GEN
  - Store active onto GEN
  - Release GEN (refcount, no free)
  - Agen owns GEN
```

**Key invariant:** GEN is the unit of install/release. GEN carries `active` (the suspended frame). The agen owns GEN between yields.

**Refcount flow:** Create (refcount 1). At creation: acquire (owner holds). Install: acquire (chain holds). Freeze: release (chain releases; we manually unlink, do not use kos_dyn_pop_top). Net: owner holds 1 ref between yields.

## Critical Semantics

### GEN Free and Chain Recursion

The chain stored in GEN.active has structure: `active (top) -> ... -> Frame -> GEN`. GEN is the root; the active node is the top of the suspended chain. When freeing GEN:

1. Clear `KosDynGen_ACTIVE(gen) = NULL` first to prevent re-entry if recursion occurs.
2. Call `kos_dynchain_free(active, (KosDynNode *) gen)` — frees from active down to but *not including* GEN. This avoids double-free: we are already in the middle of freeing GEN.
3. Then free the GEN node itself.

### Agen Dealloc When Frozen

When an agen is freed (GC/dealloc) while frozen (agen->gen set, GEN.active holds the suspended chain, ctx chain empty), we emulate Python's close behavior:

1. **Primary path: throw-based unwind** — Install GEN onto a demand-based context, inject GeneratorExit via throw, run koskari_eval. Normal pop/unwind runs (finally blocks, wind after-thunks, WITH __exit__, etc.). The cleanup code cannot await; we run in a demand context.

2. **Fallback: pure cleanup** — If the unwind path attempts to suspend (await or yield), we cannot honor it. Abort the throw-based run and fall back to a pure cleanup pass: `kos_dynchain_free(active, gen)`, free GEN, free ctx. No further user code runs.

3. **Ideal outcome** — Nodes cleanly unwind and pop; we need the fallback for when we do not get lucky.

## Debugging

Use `tox -e lldb` which sets `VERBOSE_DEBUG=1` and `DEBUG=1`, enabling `SAY`/`SAYF` macros (see `koskari/lib/debug.h`). Add `SAYF(...)` at key points (revive_yield, kos_dyn_gen_install, kos_dyn_gen_freeze_and_release, eval_phase_entry). Run targeted tests with `-s` to avoid output capture:
```
tox -e lldb -- -v -s tests/lib/opcode/test_yield.py
```

## Implementation Pitfalls

- **Freeze uses manual unlink, not kos_dyn_pop_top** — Do not pop; manually set `ctx->dyn_top = saved_parent` and call `kos_dyngen_release(gen)` to balance install's acquire.
- **send_val NULL on anext** — `eval_phase_entry` must call `revive_yield(ctx, Py_None, ...)` when `send_val==NULL && SUSP_YIELD`; do not skip.
- **Stack double-push** — Install pushes Py_None; revive_yield must pop it before pushing send_val.
- **demand_gen needs resume_gen** — Freeze must set `KosContext_RESUME_GEN(ctx) = gen` for any owner (agen or KosDemandSyncGenerator).

## Phase Index

| Phase | Focus                            | Depends On |
|-------|----------------------------------|------------|
| 1     | GEN structure (active, refcount) | —          |
| 2     | Refcount machinery               | 1          |
| 3     | Agen ownership of GEN            | 1          |
| 4     | Freeze behavior on yield         | 2, 3       |
| 5     | Install helper                   | 1          |
| 6     | Revive integration (agen path)   | 4, 5       |
| 7     | Koskari flatten (NEXT/ITER_NEXT) | 4, 5       |
| 8     | Agen dealloc when frozen         | 7          |
| 9     | Gen wind/unwind (dyn-wind at install/freeze) | 7   |
