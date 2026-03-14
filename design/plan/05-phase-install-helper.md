# Phase 5: Install Helper

## Objective

Implement `kos_dyn_gen_install(ctx, gen)` that attaches GEN's active frame to ctx and sets up the chain (GEN -> frame) for execution. Reusable for both agen revive and koskari flatten.

## Files to Modify

- `koskari/lib/dynchain.h`: Declare `kos_dyn_gen_install`
- `koskari/lib/dynchain.c`: Implement

## Implementation

**Exact order (per design discussion):**

1. **GEN's parent = current ctx's active node:** `KosDynNode_PARENT(gen) = KosContext_DYN_TOP(ctx)`

2. **Incref GEN:** `kos_dyngen_acquire(gen)`

3. **Put GEN's active onto ctx's active; NULL GEN's active:**
   - `active = KosDynGen_ACTIVE(gen)` (require non-NULL)
   - `KosDynNode_PARENT(active) = (KosDynNode *) gen`
   - `KosContext_DYN_TOP(ctx) = active`
   - `KosDynGen_ACTIVE(gen) = NULL`
   - `kos_update_frame_top(ctx)`

4. **Push Py_None** onto the frame stack for YIELD resume (use `KosContext_FRAME_TOP(ctx)` after update).

5. Empty ctx (agen revive): `gen->parent = NULL` is correct; same shape. Koskari flatten: `gen->parent = ctx->dyn_top` (consumer's top); same shape.

## Acceptance Criteria

- Unit test or existing test: create GEN with active frame, call install on fresh ctx, run one opcode, verify frame executes.
- Install then immediate yield: freeze works, release works, re-install works.
