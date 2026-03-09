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
| TRY        | (none)                   | `kos_dyn_try_unwind`        | sync (drops stack block when BREAK/CONTINUE passes through) |
| FINALLY    | (none)                   | `kos_dyn_finally_unwind`    | sync (drops stack block; finally body is async in future) |
| LOOP       | (none)                   | `kos_dyn_loop_unwind`       | sync (drops stack block) |
| FOREACH    | (none)                   | `kos_dyn_foreach_unwind`    | sync (drops stack block) |
| YIELD_FROM | (none)                   | `kos_dyn_yieldfrom_unwind`  | sync       |

- **PARAM**: Wind applies bindings to the domain (save current, set new); unwind restores from saved. Used by normal push/pop and by the transfer plan (INSTALL runs wind for params in the segment; EXTENT_ACTION unwind runs param restore).
- **WIND**: Wind runs the before-thunk via `kos_callable_call` (normal call path); unwind runs the after-thunk if the extent was entered. Both can return `KOS_DYN_UNWIND_ASYNC_STARTED`; the evaluator runs the thunk as a normal frame. When the thunk returns, the RETURN opcode returns KOS_OP_RES_CONTROL_RETURNED; the evaluator's post_opcode hook calls `kos_dynnode_on_control_returned`, which resumes unwinding or advances the transfer plan.
- **NS**: Unwind restores the frame's namespace from the node's saved_ns. No wind helper (re-entry for continuations could add one later).
- **TRY / FINALLY**: These were previously represented as a single `KOS_DYN_REGION` kind with `KosRegionStyle`; they are now separate kinds. Unwind drops the stack block when BREAK/CONTINUE passes through them. The FINALLY body will run the finally code (async) in future work.
- **LOOP / FOREACH**: Also previously `KOS_DYN_REGION`. Unwind drops the stack block. The macro `KosDynNode_IS_REGION` matches all four (TRY, FINALLY, LOOP, FOREACH) for convenience.
- **YIELD_FROM**: Unwind closes the delegate so the underlying iterator is left in a consistent state.

---

## Unified API

Consumers use `kos_extent_run_wind(ctx, node)` and `kos_extent_run_unwind(ctx, node, out_awaitable)` which dispatch by node kind. Both return 0, -1, or `KOS_DYN_UNWIND_ASYNC_STARTED`. A single async return code is used for all extent kinds (WIND hooks, WITH exit); callers treat them identically.

## Consumers

1. **Simple path** (`kos_dyn_pop_top`): Calls `kos_extent_run_unwind` for WIND and WITH/DYN_WITH, then unlinks and frees (except when async; evaluator runs the thunk/exit as a normal frame and the RETURN opcode resumes unwinding).
2. **Transfer plan**: Uses `KOS_XFER_EXTENT_ACTION` steps with `(node, phase)` where phase is WIND or UNWIND. Step handler dispatches via `kos_extent_run_wind` or `kos_dynnode_run_transfer_unwind`. INSTALL runs `kos_extent_run_wind` for each PARAM and DYN_WITH in the installed segment.

---

## Unwind-in-progress state (simple path)

When `kos_dyn_unwind_to_frame` is used (e.g. by RETURN or by exception propagation) and a WIND with an after-hook is encountered, the unwind helper returns `KOS_DYN_UNWIND_ASYNC_STARTED`. Then:

- `kos_dyn_unwind_to_frame` sets `KosContext_UNWIND_TARGET_FRAME(ctx)` to the target frame and returns `KOS_DYN_UNWIND_ASYNC_STARTED`.
- The caller (RETURN or `try_local_exception_handler`) stashes any return value or restores the exception and returns so the eval loop continues.
- The evaluator runs the thunk as a normal frame (thunk was pushed by `kos_callable_call`).
- When the thunk returns, the RETURN opcode returns KOS_OP_RES_CONTROL_RETURNED. The evaluator's post_opcode hook invokes `kos_dynnode_on_control_returned`, which detaches the thunk frame and resumes unwinding (or advances the transfer plan). Any stashed return value is delivered when unwinding completes.

---

## Try/catch, finally

- **TRY**: PUSH_TRY and PUSH_FINALLY each push a KosStackBlock. Exception dispatch clears the block before jumping to the handler. The per-kind unwind helpers drop the stack block when BREAK/CONTINUE passes through. Catch is "find handler and jump" in the raise path, not in the generic unwind helper.
- **FINALLY**: Same stack block behavior as TRY. Unwind = run finally body (code at region's pc) then pop. Model as async: helper starts the body via the same hook runner; when the body ends, unwinding continues.

**With / dyn-with:** Context managers are specified in [with-dyn-with.md](with-dyn-with.md). Single-shot `with` (KOS_DYN_WITH) uses enter/exit and structural stack blocks; multi-shot `dyn-with` (KOS_DYN_DYN_WITH) participates in transfer-plan wind/unwind.

**Suspend during wind/unwind:** The evaluator supports suspension (await) at several points. **Thunk body may await:** Wind and unwind thunks are executed as normal bytecode (pushed via `kos_callable_call`); if a thunk contains AWAIT, the evaluator yields with `KOS_EV_AWAIT`. When the driver resumes, the same thunk continues. When the thunk returns (RETURN opcode returns KOS_OP_RES_CONTROL_RETURNED), `kos_dynnode_on_control_returned` resumes unwinding or advances the transfer plan. **Transfer plans:** When a wind or unwind step returns an awaitable (e.g. a future dyn-with exit step), the plan suspends (`KOS_XFER_SUSPENDED_AWAIT`); the driver gets the awaitable and later sends the result back; on resume the inject is fed into the plan and the plan continues. See [with-dyn-with.md](with-dyn-with.md) for transfer plan suspension and single-shot with async enter/exit. Exit convention (Python-like: exit(exc_type, exc_val, tb), truthy => swallow) is defined there.
