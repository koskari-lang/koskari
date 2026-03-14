# Phase 9: Gen Wind/Unwind (dynamic-wind at install/freeze)

## Objective

When installing a generator onto a chain (resume) or freezing it (yield), run the same wind/unwind semantics as the transfer plan uses for continuations. Treat yield as a delimited continuation: leaving the gen extent must run after-thunks; entering (install/resume) must run before-thunks. This ensures `dynamic-wind` regions in generator bodies behave correctly across suspend/resume.

## Approach: Reuse Transfer Plan Machinery

Use the existing `KosTransferPlan` infrastructure rather than custom wind/unwind loops. The eval loop already runs `run_transfer_plan_to_completion` at the top of each iteration (eval.c:2537, 2538); if we set `KosContext_PLAN(ctx)`, the plan runs automatically. Async thunks (wind before, unwind after) are handled by the existing suspend/resume flow.

**Key insight**: Do not use `KOS_XFER_INSTALL` for gen. The transfer plan's INSTALL step clones the segment and replaces the chain. For gen we need the live chain (preserves stack, PC, suspended state). We link manually, then drive wind/unwind via EXTENT_ACTION steps.

## Background

**Current behavior**: `kos_dyn_gen_install` (dynchain.c:625) only links the chain; it does not run wind for WIND, PARAM, or DYN_WITH nodes. `kos_dyn_gen_freeze_and_release` (dynchain.c:2719) does a manual unlink; it does not run unwind for extent nodes between top and GEN. Result: `(dynamic-wind before body after)` that spans a yield will not run `after` on yield or `before` on resume.

**Transfer plan semantics** (callcc.md, extent-wind-unwind.md): On continuation invoke: (1) unwind from current to LCA; (2) INSTALL splice (clones segment); (3) wind from LCA to target; (4) INJECT. Wind = enter; unwind = exit.

**Generator analogy**: Yield captures a continuation. Freeze = leaving that extent. Install = applying the continuation = entering that extent.

## Files to Modify

- `koskari/lib/dynchain.c`: `kos_dyn_gen_install`, `kos_dyn_gen_freeze_and_release`, plan-building helpers, `kos_dynnode_on_control_returned`, possibly `kos_dyn_wind_wind`
- `koskari/lib/dynchain.h`: `KOS_XFER_GEN_FREEZE` step type (or equivalent), plan struct extensions if needed
- `koskari/lib/eval.c`: Revive/attach paths; demand-mode abort when freeze hits async
- `design/interpreter/extent-wind-unwind.md`: Update if gen is added as a consumer

## Code References

| Location | Purpose |
|----------|---------|
| dynchain.c:625 | kos_dyn_gen_install (link only) |
| dynchain.c:2719 | kos_dyn_gen_freeze_and_release (manual unlink) |
| dynchain.c:2598 | kos_dyn_wind (dispatches to kos_dyn_param_wind, kos_dyn_wind_wind, kos_dyn_dyn_with_wind) |
| dynchain.c:2623 | kos_dyn_unwind (dispatches per kind) |
| dynchain.c:801 | kos_dyn_wind_wind (calls KosContext_CallNoArgs, returns KOS_DYN_UNWIND_ASYNC_STARTED) |
| dynchain.c:814 | kos_dyn_wind_unwind |
| dynchain.c:891 | kos_dynnode_on_control_returned (only handles WITH parent; needs WIND) |
| dynchain.c:849 | kos_dyn_wind_on_control_returned (sets ENTERED, POST_BEGIN) |
| dynchain.c:4449 | xfer_step_extent_action (runs kos_dyn_wind or kos_dynnode_run_transfer_unwind) |
| dynchain.c:4523 | xfer_step_install (clones; uses NEEDS_INSTALL_WIND for PARAM/DYN_WITH only) |
| dynchain.h:351 | KosTransferStep enum (EXTENT_ACTION, INSTALL, INJECT, POP_ONE, PUSH, TRUNCATE_AND_INJECT) |
| eval.c:114 | run_transfer_plan_to_completion (returns 0 on KOS_XFER_SUSPENDED_AWAIT; plan stays set) |
| eval.c:2537 | Eval loop: if (KosContext_PLAN(ctx)) run_transfer_plan_to_completion |
| eval.c:2378 | eval_phase_post_opcode calls kos_dynnode_on_control_returned on KOS_OP_RES_CONTROL_RETURNED |

## Capability Notes

- WIND has `KOS_DYN_CAP_NEEDS_WIND_BEFORE_ACTION` (dynchain.c:747), not NEEDS_INSTALL_WIND. WIND nodes get wind via explicit EXTENT_ACTION (wind) steps, not via INSTALL's built-in wind.
- PARAM and DYN_WITH have `KOS_DYN_CAP_NEEDS_INSTALL_WIND` (dynchain.c:1691, 1419). For gen we use EXTENT_ACTION (wind) for all three.

## Implementation

### Install: Link Then Drive via Plan

1. **Do the link** (current kos_dyn_gen_install logic): GEN's parent = ctx->dyn_top; kos_dyngen_acquire; ctx->dyn_top = active; gen->active = NULL; kos_update_frame_top; push Py_None.

2. **Build a transfer plan**:
   - Walk from active down toward GEN. Collect nodes that need wind: WIND (before_thunk), PARAM, DYN_WITH. Use `kos_dyn_wind` / capability checks.
   - Order: from GEN's child outward to active (first node we "enter" is GEN's child).
   - Actions: one `KOS_XFER_EXTENT_ACTION` (phase WIND) per node, then `KOS_XFER_INJECT` (send_val or throw setup).

3. **Set plan and return**: `KosContext_PLAN(ctx) = plan`. Return 0. Do not run the plan inside install.

4. **Eval loop**: At next iteration, `run_transfer_plan_to_completion` runs the plan. When complete, plan cleared, execution continues with gen body.

5. **Callers**: revive_yield (eval.c, called from eval_phase_entry), op_attach_koskari_agen_frame (eval.c, for NEXT/ITER_NEXT). Both call kos_dyn_gen_install then continue; no changes needed if plan runs automatically.

### Freeze: Plan Before Unlink

1. **Build a freeze plan**:
   - Walk from active toward GEN. Collect nodes that need unwind: WIND (after_thunk and entered), PARAM, DYN_WITH, NS, YIELD_FROM. Use `kos_dyn_unwind` / capability checks.
   - Order: inner-to-outer (active toward GEN).
   - Actions: one `KOS_XFER_EXTENT_ACTION` (phase UNWIND) per node, then a final step to perform the manual unlink.

2. **New step type**: Add `KOS_XFER_GEN_FREEZE`. Step handler: record saved_parent, ctx_active; set gen->active = ctx_active; ctx->dyn_top = saved_parent; owner handling; kos_dyngen_release. (Same logic as current kos_dyn_gen_freeze_and_release lines 2757–2782.)

3. **Set plan and return**: `KosContext_PLAN(ctx) = plan`. Return 0 from kos_dyn_gen_freeze_and_release. Do not do manual unlink synchronously.

4. **Eval loop**: Plan runs. Unwind steps may return KOS_DYN_UNWIND_ASYNC_STARTED (thunk runs); on thunk return, control_returned runs, next iteration resumes plan. When GEN_FREEZE step runs, manual unlink happens. Plan clears.

5. **YIELD event**: After plan completes, the yield value is already in out_ev. The walk_handle_yield caller must not emit the event until the plan is done. Flow: post_opcode sees KOS_EV_YIELD, calls walk_handle_yield, which builds plan and returns (no event yet). Eval continues; plan runs. When plan completes, we need to emit the yield. Options: (a) GEN_FREEZE step sets a flag and returns a code so post_opcode knows to emit; (b) walk_handle_yield returns a code meaning "plan set, run it, then emit on completion". Clarify during implementation.

### control_returned Gap (Critical)

When a WIND before-thunk returns, the frame we return to has parent = WIND node. But `kos_dynnode_on_control_returned` (dynchain.c:891) only checks `parent && KosDynNode_IS_WITH(parent)`. It never handles WIND.

**Fix**: Add branch: if `node` is FRAME and `parent = KosDynNode_PARENT(node)` and `KosDynNode_IS_WIND(parent)` and `KosDynWind_PHASE(parent) == KOS_WIND_PHASE_PRE_BEGIN`, call `kos_dyn_wind_on_control_returned(ctx, parent)`.

**Plan advancement**: When we get KOS_XFER_SUSPENDED_AWAIT, `run_transfer_plan_to_completion` returns 0 without advancing `KosTransferPlan_ACTION_INDEX`. So the next loop iteration runs the same step again. `kos_dyn_wind_wind` would call the before-thunk again. We must either:
- (a) Have `kos_dyn_wind_wind` return 0 immediately when `KosDynWind_ENTERED(node)` (or phase is POST_BEGIN), or
- (b) Have `kos_dyn_wind_on_control_returned` advance `KosTransferPlan_ACTION_INDEX(ctx->plan)` when it handles a wind thunk.

Recommend (b): keep extent logic simple; advance plan in control_returned. Then the next step runs wind for the next node.

### Demand Mode (Koskari Flatten)

When freeze runs in the demand loop (NEXT/ITER_NEXT, koskari-driven), we cannot suspend. If any unwind step returns `KOS_DYN_UNWIND_ASYNC_STARTED`, we cannot honor it.

**Handling**: Detect demand context (e.g. KosContext_PENDING_AGEN set). If freeze plan would need to run and we get async, abort: clear/free the plan, fall back to current manual unlink (no unwind). Same model as Phase 8's "abort to fallback" when close attempts suspend. No user code in thunks runs; we just free the chain.

### Per-Node Kinds

| Kind      | Wind (install)              | Unwind (freeze)              | API                          |
|-----------|-----------------------------|------------------------------|------------------------------|
| PARAM     | kos_dyn_param_wind          | kos_dyn_param_unwind         | kos_dyn_wind / kos_dyn_unwind|
| WIND      | kos_dyn_wind_wind           | kos_dyn_wind_unwind          | kos_dyn_wind / kos_dyn_unwind|
| DYN_WITH  | kos_dyn_dyn_with_wind       | kos_dyn_with_unwind          | kos_dyn_wind / kos_dyn_unwind|
| NS        | (none)                      | kos_dyn_ns_unwind            | (unwind only)                |
| YIELD_FROM| (none)                      | kos_dyn_yieldfrom_unwind     | (unwind only)                |

FRAME, GEN are boundaries; skip. kos_dyn_wind (dynchain.c:2598) and kos_dyn_unwind (dynchain.c:2623) dispatch by kind.

### Edge Cases

- **GEN not in chain**: Assert at freeze.
- **No WIND/PARAM in segment**: Plan has only INJECT (install) or GEN_FREEZE (freeze); no-op extent steps.
- **Thunk raises**: Wind/unwind returns -1; plan step fails; run_transfer_plan_to_completion returns -1; eval propagates.
- **Thunk awaits**: Returns KOS_DYN_UNWIND_ASYNC_STARTED; plan stays; eval continues; thunk runs; on resume, control_returned advances plan.
- **Nested gens**: Segment is active->...->GEN. Nested GEN2 is a boundary; run wind/unwind for extent nodes between.

### Phase 8 Interaction

When GeneratorExit-driven teardown runs (iter.c asyncgen_teardown_frozen), we install GEN and run eval. That install path will use the same plan-based install (wind steps + inject). When the gen returns during close, we use kos_dyn_gen_unwind_exhausted. The normal RETURN path runs kos_dyn_walk_return_to_frame, which may run unwind for extent nodes. Verify: does the gen's top-level RETURN run unwind for WIND nodes between frame and GEN? If not, unwind_exhausted may need to run unwind before freeing, or the RETURN path already handles it. Clarify during implementation.

## Preconditions

- Phase 7 (Koskari flatten) complete
- Phase 8 (agen dealloc when frozen) complete
- Transfer plan machinery (KosTransferPlan, kos_run_transfer_step, run_transfer_plan_to_completion) working

## Acceptance Criteria

- `(dynamic-wind before body after)` where body contains a yield: `after` runs when the gen yields, `before` runs when the gen is resumed.
- Test: gen with dyn-wind, yield inside body; step once, verify after ran; step again, verify before ran before body continues.
- No regression in generator tests (Phase 4, 5, 6, 7, 8).
- If a before- or after-thunk awaits, gen suspend/resume works (agen path).
- Demand mode: if thunk would await during freeze, fall back to manual unlink; no crash, no leak.

## Dependencies

- Phase 7 (Koskari flatten)
- Phase 8 (agen dealloc when frozen)
- Transfer plan, kos_dyn_wind, kos_dyn_unwind, kos_dynnode_on_control_returned

## Out of Scope

- Continuations (shift/reset) crossing generator boundaries
- Optimizing the no-WIND case

## References

- design/interpreter/extent-wind-unwind.md
- design/interpreter/callcc.md (§ Transfer Plan, § Wind/Unwind Semantics)
- design/interpreter/domain.md (transfer plan EXTENT_ACTION)
- koskari/lib/dynchain.c (install, freeze, wind/unwind, transfer steps)
- koskari/lib/eval.c (run_transfer_plan_to_completion, eval loop, control_returned)
