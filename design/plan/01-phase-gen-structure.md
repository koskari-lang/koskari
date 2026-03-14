# Phase 1: GEN Structure (active, refcount)

## Objective

Add `active` and `refcount` fields to `KosDynGen` without changing behavior yet.

## Files to Modify

- `koskari/lib/dynchain.h`: Extend `u.gen` struct
- `koskari/lib/dynchain.c`: `kos_dyngen_new` init; `kos_dyngen_free` cleanup; GC traverse

## Implementation

1. In `dynchain.h`, add to `u.gen`:
   - `KosDynFrame *active` — suspended frame (NULL when running)
   - `uintptr_t refcount` — reference count (init 1)

2. Add macros: `KosDynGen_ACTIVE(n)`, `KosDynGen_REFCOUNT(n)`.

3. In `kos_dyngen_new`: set `active = NULL`, `refcount = 1`.

4. In `kos_dyngen_free`: free the active chain without recursing into GEN. Chain structure is `active (top) -> ... -> Frame -> GEN`. Call `kos_dynchain_free(active, (KosDynNode *) gen)` so we free down to but not including GEN. Clear `KosDynGen_ACTIVE(gen) = NULL` *before* the free to prevent re-entry. See README "GEN Free and Chain Recursion".

5. In `kos_dynnode_traverse` for `KOS_DYN_GEN`: traverse the `active` frame and its stack/ns when non-NULL. Frame holds PyObject refs (stack, ns) that need visiting.

## Acceptance Criteria

- `tox -qe py` passes.
- `make test-valgrind` (if applicable) passes for native tests.
- No new linter errors.
- GEN creation in `koskari/lib/coro.c` and `koskari/lib/sync.c` unchanged; sync.c GEN uses remain valid.
