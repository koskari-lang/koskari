# Unified extent wind/unwind model

This document describes the **unified wind/unwind** abstraction for dynamic extents on the Koskari dynamic chain. The same helpers are used for (1) simple upward flow (return/raise) and (2) transfer (continuation) flow so that behavior is identical regardless of how control leaves an extent.

---

## Contract

- **Wind (enter)**: `extent_wind(ctx, node)` establishes the extent (e.g. apply param bindings, run wind before hook). Returns 0 on success, -1 on error, or `KOS_DYN_UNWIND_ASYNC_STARTED` if async (e.g. started a before-hook; caller must not assume the extent is "entered" until the hook completes).
- **Unwind (exit)**: `extent_unwind(ctx, node)` tears down the extent (e.g. restore param bindings, run wind after hook). Returns 0 when teardown is complete, -1 on error, or `KOS_DYN_UNWIND_ASYNC_STARTED` if async exit code was started (caller must not pop the node until the hook completes).

Helpers perform only semantic work; the caller unlinks and frees the node (or, for async unwind, defers the pop until after the hook runs).

**Sync vs async**: Sync helpers (param restore, ns restore, yield_from close) do work and return. Async helpers (wind after, wind before, and later finally body / with __exit__) start a hook or body and return `KOS_DYN_UNWIND_ASYNC_STARTED`; the evaluator runs the code then continues unwinding (or continues the transfer plan).

---

## Per-kind table

| Kind        | Wind helper              | Unwind helper               | Sync/async |
|------------|--------------------------|-----------------------------|------------|
| PARAM      | `kos_dyn_param_wind`     | `kos_dyn_param_unwind`      | sync       |
| WIND       | `kos_dyn_wind_wind`      | `kos_dyn_wind_unwind`       | async      |
| NS         | (none; set at push)      | `kos_dyn_ns_unwind`         | sync       |
| REGION     | (none)                   | `kos_dyn_region_unwind`     | sync (no-op for now; FINALLY will be async) |
| YIELD_FROM | (none)                   | `kos_dyn_yieldfrom_unwind`  | sync       |

- **PARAM**: Wind applies bindings to the domain (save current, set new); unwind restores from saved. Used by normal push/pop and by the transfer plan (INSTALL runs wind for params in the segment; PARAM_RESTORE runs unwind).
- **WIND**: Wind runs the before-hook (code/pc) via `kos_begin_hook`; unwind runs the after-hook if the extent was entered. Both can return `KOS_DYN_UNWIND_ASYNC_STARTED`; the evaluator runs the hook then continues.
- **NS**: Unwind restores the frame's namespace from the node's saved_ns. No wind helper (re-entry for continuations could add one later).
- **REGION**: Unwind is currently a no-op. FINALLY will run the finally body (async). TRY/LOOP/FOREACH have no teardown.
- **YIELD_FROM**: Unwind closes the delegate so the underlying iterator is left in a consistent state.

---

## Consumers

1. **Simple path** (`kos_dyn_pop_top`): For each node kind, the pop path calls the corresponding `kos_dyn_<kind>_unwind(ctx, top)` then unlinks and frees (except for WIND when unwind returns `KOS_DYN_UNWIND_ASYNC_STARTED`; then the evaluator runs the after-hook and resumes unwinding from END_WIND).
2. **Transfer plan**: Unwind phase uses RUN_HOOK (wind after) and PARAM_RESTORE; step handlers call `kos_dyn_param_unwind` and `kos_begin_hook` (same as wind helpers). INSTALL runs `kos_dyn_param_wind` for each PARAM in the installed segment. Wind-before phase uses RUN_HOOK with before code/pc.

---

## Unwind-in-progress state (simple path)

When `kos_dyn_unwind_to_frame` is used (e.g. by RETURN or by exception propagation) and a WIND with an after-hook is encountered, the unwind helper returns `KOS_DYN_UNWIND_ASYNC_STARTED`. Then:

- `kos_dyn_unwind_to_frame` sets `KosContext_UNWIND_TARGET_FRAME(ctx)` to the target frame and returns `KOS_DYN_UNWIND_ASYNC_STARTED`.
- The caller (RETURN or `try_local_exception_handler`) stashes any return value or restores the exception and returns so the eval loop continues.
- The evaluator runs from the hook runner's code/pc (when `kos_hook_active(ctx)`).
- When the hook hits END_WIND, `kos_end_hook` is called, then unwinding is resumed via `kos_dyn_unwind_to_frame(ctx, unwind_target_frame)`. When that completes, any stashed return value is delivered (in END_WIND) or exception handling continues.

---

## Future: try/catch, finally

- **TRY**: Unwind remains no-op (just pop). Catch is "find handler and jump" in the raise path, not in the generic unwind helper.
- **FINALLY**: Unwind = run finally body (code at region's pc) then pop. Model as async: helper starts the body via the same hook runner; when the body ends, unwinding continues.

**With / dyn-with:** Context managers are specified in [with-dyn-with.md](with-dyn-with.md). Single-shot `with` (KOS_DYN_WITH) uses enter/exit and structural stack blocks; multi-shot `dyn-with` (KOS_DYN_DYN_WITH) participates in transfer-plan wind/unwind. Exit convention (Python-like: exit(exc_type, exc_val, tb), truthy => swallow) is defined there.
