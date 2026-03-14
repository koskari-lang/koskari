# Phase 2: Refcount Machinery

## Objective

Add acquire/release to GEN; route `kos_dynnode_free` for GEN through release so pop can be used without freeing when refcount > 0.

## Files to Modify

- `koskari/lib/dynchain.h`: Declare `kos_dyngen_acquire`, `kos_dyngen_release`
- `koskari/lib/dynchain.c`: Implement acquire/release; change `kos_dynnode_free` GEN case

## Implementation

1. `void kos_dyngen_acquire(KosDynGen *gen)` — increment `refcount` (no-op if NULL).

2. `void kos_dyngen_release(KosDynGen *gen)` — decrement `refcount`; if 0, call `kos_dyngen_free`. No-op if NULL.

3. In `kos_dynnode_free` switch for `KOS_DYN_GEN`: call `kos_dyngen_release((KosDynGen *) node)` instead of `kos_dyngen_free`.

4. Ensure `kos_dyngen_new` leaves `refcount = 1` so first release frees (matches current behavior when chain is freed).

5. In `kos_dyngen_release`: guard against refcount underflow (refcount already 0); do not call `kos_dyngen_free` if we detect underflow (avoids recursion / double-free when freeing active chain triggers a second release).

## Acceptance Criteria

- All existing tests pass.
- No double-free or leak under valgrind for tests that create/free async gens.
- Manual verification: GEN created, released once (no acquire) -> freed. GEN acquired once, released twice -> freed on second release.
