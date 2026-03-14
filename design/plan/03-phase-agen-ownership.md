# Phase 3: Agen Ownership of GEN

## Objective

KosPyAsyncGen holds an explicit reference to its GEN node so GEN can survive release from the chain.

## Files to Modify

- `koskari/lib/iter.h`: Add `KosDynGen *gen` to `KosPyAsyncGen` (or forward-declare and use `void *` if avoiding dynchain.h in iter.h)
- `koskari/lib/iter.c`: Init, clear, traverse, dealloc
- `koskari/lib/coro.c`: Pass GEN to agen in `KosAsyncGen_NewFromCodeAndNs`

## Implementation

1. Add `KosDynGen *gen` (or `void *gen` with cast at use sites) to `KosPyAsyncGen`. Initial value NULL.

2. In `KosAsyncGen_NewFromCodeAndNs`: after creating GEN and frame:
   - Set `KosDynGen_ACTIVE(gen) = frame` (gen's "basic frame" at creation).
   - Set `agen->gen = gen` and call `kos_dyngen_acquire(gen)` (owner holds ref).

3. In `asyncgen_clear` / dealloc when frozen (chain empty, agen->gen set): prefer throw-based unwind (GeneratorExit) in a demand context for proper cleanup; if suspend is attempted, fall back to pure cleanup. See README "Agen Dealloc When Frozen". Ultimately call `kos_dyngen_release(agen->gen)` if non-NULL; set to NULL. GEN's free handles active via `kos_dynchain_free(active, gen)`.

4. In `asyncgen_traverse`: traverse GEN and its active via a helper if iter.h cannot include dynchain. Ensure GEN (and active frame) are reachable for GC.

5. Document: agen->gen is set at creation; when GEN is released from chain (freeze), agen holds the only ref.

## Acceptance Criteria

- `tox -qe py` passes.
- `test_next` and `test_run_iter_adapters` pass.
- No new leaks in async gen create/drive/close cycles.
- Agen without yield (immediate return) still works; GEN stays on chain until context free.
