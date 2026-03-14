# Phase 4: Freeze Behavior on Yield

## Objective

On YIELD when parent is GEN: unwind to GEN, store ctx's active node in GEN.active, release GEN from chain (do NOT pop — manually unlink). Value is already in out_ev. Implement for agen first; same logic for demand_gen (KosDemandSyncGenerator).

## Files to Modify

- `koskari/lib/dynchain.c`: Add `kos_dyn_gen_freeze_and_release`; extend `kos_dyn_walk_handle_yield`

## Implementation

**Critical: release (do not pop).** We manually unlink the chain; we do not use `kos_dyn_pop_top`.

**Freeze sequence** (when YIELD hits parent==GEN), exact order:

1. **Record GEN's parent, then remove GEN->parent link:**
   - `saved_parent = KosDynNode_PARENT(gen)`
   - `KosDynNode_PARENT(gen) = NULL`

2. **Remove ctx's active node and store it in GEN:**
   - `ctx_active = KosContext_DYN_TOP(ctx)`
   - `KosDynGen_ACTIVE(gen) = ctx_active`

3. **Put GEN's old parent onto ctx's active:**
   - `KosContext_DYN_TOP(ctx) = saved_parent`
   - `kos_update_frame_top(ctx)`

4. **Owner handling:** If `KosDynGen_OWNER(gen)` is set (agen or demand_gen): set `owner->gen = gen` (for agen), set `KosContext_RESUME_GEN(ctx) = gen` for both. Call `kos_dyngen_release(gen)` to balance the install's acquire (chain no longer holds it).

5. **Integration:** When `kos_dyn_walk_handle_yield` finds GEN and GEN consumes, call `kos_dyn_gen_freeze_and_release`. Return 0; value is already in out_ev.

6. **RETURN path:** When generator returns (parent is GEN), need analogous freeze. May be Phase 6.

## Acceptance Criteria

- Python `async for x in koskari_agen` yields values correctly.
- `test_next` tests for Python async gen pass.
- `test_next_accumulates_values_from_koskari_async_gen` passes (Python consumer, Koskari gen).
- After yield, `agen->gen` is set, GEN is not on ctx chain, `KosDynGen_ACTIVE(agen->gen)` is the frame.
