# Phase 9: Gen Wind/Unwind (dynamic-wind at install/freeze)

## Objective

When installing a generator onto a chain (resume) or freezing it (yield), run the same wind/unwind semantics as the transfer plan uses for continuations. Treat yield as a delimited continuation: leaving the gen extent must run after-thunks; entering (install/resume) must run before-thunks. This ensures `dynamic-wind` regions in generator bodies behave correctly across suspend/resume.

## Background

**Current behavior**: `kos_dyn_gen_install` only links the chain; it does not run `kos_extent_run_wind` for WIND, PARAM, or DYN_WITH nodes in the installed segment. `kos_dyn_gen_freeze_and_release` does a manual unlink; it does not run `kos_extent_run_unwind` for extent nodes between the top and GEN. As a result, `(dynamic-wind before body after)` that spans a yield will not run `after` on yield or `before` on resume.

**Transfer plan semantics** (from callcc.md, extent-wind-unwind.md):
- On continuation invoke: (1) unwind from current to LCA (run after-thunks); (2) INSTALL splice; (3) wind from LCA to target (run before-thunks); (4) INJECT.
- Wind thunks run when entering a region; unwind thunks run when leaving (normal exit, exception, or transfer).

**Generator analogy**: Yield captures a continuation (the rest of the gen). Freeze = leaving that extent. Install = applying the continuation = entering that extent.

## Files to Modify

- `koskari/lib/dynchain.c`: `kos_dyn_gen_install`, `kos_dyn_gen_freeze_and_release`
- `koskari/lib/dynchain.h`: Any new helpers or flags
- Possibly `koskari/lib/eval.c`: If install/freeze need to cooperate with async wind/unwind (KOS_DYN_UNWIND_ASYNC_STARTED)
- `design/interpreter/extent-wind-unwind.md`: Update if gen is added as a consumer

## Preconditions

- Phase 7 (Koskari flatten) complete
- Phase 8 (agen dealloc when frozen) may depend on this or vice versa; order TBD
- `kos_extent_run_wind`, `kos_extent_run_unwind`, and the transfer plan machinery exist and work for continuations

## Implementation

### Install: Run wind (before-thunks) for the installed segment

**Segment structure**: When we install, the chain we graft is `active -> ... -> frame -> ... -> GEN`. GEN's parent becomes the consumer's dyn_top. The "installed segment" is from the node whose parent is GEN (the innermost child of GEN in the chain) up to active (the top).

**Wind order**: Outer-to-inner relative to the consumer. From the transfer plan: wind for nodes from LCA (GEN, the splice point) toward target (active, the resume point). So we run `kos_extent_run_wind` for each node in the segment that has a wind phase, in order from the node adjacent to GEN outward to active.

**Algorithm**:

1. **After linking** (KosContext_DYN_TOP(ctx) = active, etc.): Walk from active down toward GEN. Collect nodes that need wind (WIND with before-thunk, PARAM, DYN_WITH, etc.). Order for wind: from GEN's child toward active (inner-to-outer in segment terms; the transfer plan says LCA-to-target which is outer-to-inner from the consumer's view—the first node we "enter" is GEN's child).

2. **Iterate and run wind**: For each node from GEN's child up to active (in chain order: next toward active), call `kos_extent_run_wind(ctx, node)`.

3. **Async handling**: If any call returns `KOS_DYN_UNWIND_ASYNC_STARTED`, we cannot complete install synchronously. Options:
   - (a) Defer: stash "install in progress" state, return to eval loop, run the thunk; when thunk returns, `kos_dynnode_on_control_returned` resumes the install (run next wind, or finish). This mirrors the transfer plan's handling.
   - (b) Restrict: For gen install, require sync wind only; if a before-thunk would be async, error or defer the whole install to a separate mechanism. Simpler but less general.
   - Recommend (a) for full compatibility with continuations.

4. **Integration point**: `kos_dyn_gen_install` may need to return a new status (e.g. `KOS_GEN_INSTALL_ASYNC`) when async wind is started. The caller (revive_yield, op_attach for NEXT/ITER_NEXT) would then need to handle it: not run eval immediately, let the thunk run, then on control_returned resume.

### Freeze: Run unwind (after-thunks) before manual unlink

**Chain structure at freeze**: `active (ctx->dyn_top) -> ... -> frame -> ... -> GEN -> consumer_top`. We are about to store `active -> ... -> GEN` in GEN.active and set ctx->dyn_top = consumer_top.

**Unwind order**: Inner-to-outer. We run after-thunks for each extent node we are "leaving" as we go from active down toward GEN. Same order as normal unwind: start at active, walk toward GEN, for each WIND/PARAM/DYN_WITH run `kos_extent_run_unwind`.

**Algorithm**:

1. **Before manual unlink**: Walk from active toward GEN. For each node that has an unwind phase (WIND with after-thunk and entered, PARAM, DYN_WITH, etc.), call `kos_extent_run_unwind(ctx, node, out_awaitable)`.

2. **Sync unwind**: If all return 0, proceed with the existing manual unlink (store in GEN.active, set ctx->dyn_top = saved_parent, etc.).

3. **Async unwind**: If any returns `KOS_DYN_UNWIND_ASYNC_STARTED`, we cannot complete freeze synchronously. We must:
   - Stash "freeze in progress" state (target GEN, saved_parent, owner, etc.)
   - Set ctx state so the eval loop will run the after-thunk
   - When the thunk returns, `kos_dynnode_on_control_returned` (or equivalent) resumes: run next unwind, or complete the manual unlink.

4. **Interaction with kos_dyn_walk_handle_yield**: Currently `kos_dyn_walk_handle_yield` calls `kos_dyn_gen_freeze_and_release` directly. If freeze becomes async, the walk must either (a) return a special code and let the evaluator drive the async freeze, or (b) the freeze helper itself triggers eval of the thunk and is only "complete" when sync. The evaluator's `eval_phase_post_opcode` and `kos_dynnode_on_control_returned` already know how to resume transfer plans; we need analogous state for "freeze plan" or we reuse the transfer plan machinery.

**Reuse option**: Model gen freeze as a miniature transfer plan: unwind from current to GEN (the boundary), then "install" nothing (we're removing the segment). The plan would have only EXTENT_ACTION (unwind) steps. When the last step completes, we do the manual unlink. This reuses `KosTransferPlan`, `kos_run_transfer_step`, and the control_returned logic.

### Per-node kinds in the segment

From extent-wind-unwind.md:

| Kind     | Wind (install)      | Unwind (freeze)       |
|----------|---------------------|------------------------|
| PARAM    | kos_dyn_param_wind  | kos_dyn_param_unwind  |
| WIND     | kos_dyn_wind_wind   | kos_dyn_wind_unwind   |
| DYN_WITH | (enter)             | (exit)                |
| NS       | (none)              | kos_dyn_ns_unwind     |
| YIELD_FROM| (none)             | kos_dyn_yieldfrom_unwind |

We run wind/unwind only for nodes that have those phases. FRAME, GEN, YIELD_FROM (wind), etc. are traversed but do not all have wind/unwind.

### Edge cases

- **GEN not in chain**: Should not happen at freeze; assert.
- **No WIND/PARAM in segment**: Wind and unwind loops are no-ops; behavior matches current (just link/unlink).
- **Thunk raises**: Wind or unwind returns -1; propagate to caller. For freeze, we have not yet unlinked; the chain is intact. For install, we have already linked—may need to unwind what we wound, or abort and unlink.
- **Thunk awaits**: Returns KOS_DYN_UNWIND_ASYNC_STARTED. Evaluator runs thunk; on return, resume.
- **Nested gens**: The segment is from active to GEN. If there is a nested gen (GEN2) in the segment, GEN2's chain is part of our segment. We run wind/unwind for extent nodes; GEN and GEN2 are boundaries, not extent nodes. So we run wind for WIND nodes between GEN and active; same for unwind.

### Interaction with Phase 8 (agen dealloc when frozen)

When we run GeneratorExit-driven teardown, we install GEN and run eval. The install in that path would also need to run wind for the segment. And when the gen returns/exits during close, we unwind via `kos_dyn_gen_unwind_exhausted`—that path already pops the chain. The exhausted path may need to run unwind for nodes above the frame before freeing, or the normal RETURN path may have already run unwind. Clarify during implementation.

## Acceptance Criteria

- `(dynamic-wind before body after)` where `body` contains a yield: `after` runs when the gen yields, `before` runs when the gen is resumed.
- Test: defgen with dyn-wind, yield inside body; step once, verify after ran; step again, verify before ran before body continues.
- No regression in generator tests (Phase 4, 5, 6, 7).
- If a before- or after-thunk awaits, the gen suspend/resume still works (async wind/unwind).

## Dependencies

- Phase 7 (Koskari flatten)
- extent-wind-unwind machinery, transfer plan, kos_extent_run_wind / kos_extent_run_unwind
- kos_dynnode_on_control_returned, unwind-in-progress state in eval

## Out of Scope (for this phase)

- Full integration testing with continuations (shift/reset) crossing generator boundaries; that may be blocked or have separate semantics.
- Optimizing the no-WIND case to avoid any overhead (can be a follow-up).

## References

- design/interpreter/extent-wind-unwind.md
- design/interpreter/callcc.md (§ Transfer Plan, § Wind/Unwind Semantics)
- design/interpreter/domain.md (transfer plan EXTENT_ACTION)
- koskari/lib/dynchain.c (kos_dyn_gen_install, kos_dyn_gen_freeze_and_release, transfer plan step handler)
