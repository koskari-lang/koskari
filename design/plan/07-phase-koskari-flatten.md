# Phase 7: Koskari Flatten

## Objective

NEXT and ITER_NEXT on KosPyAsyncGen install GEN (with active) onto the consumer's chain instead of attaching only the frame. Reuse `kos_dyn_gen_install`. On yield, same freeze path; value is pushed to consumer and loop continues.

## Files to Modify

- `koskari/lib/eval.c`: `op_attach_koskari_agen_frame` -> call `kos_dyn_gen_install` with consumer ctx and agen->gen
- `koskari/lib/opcode/NEXT.c`: Pass consumer ctx and agen
- `koskari/lib/opcode/ITER_NEXT.c`: Same
- `koskari/lib/coro.c`: Ensure agen->gen is set and GEN.active is the frame before first NEXT

## Implementation

1. **Initial GEN.active**: For first NEXT, get gen_frame from `KosContext_FRAME_TOP(agen->ctx)`. Set `GEN.active = frame`, detach frame from agen->ctx (manual unlink so agen->ctx chain is empty, frame stored on gen). Then `kos_dyn_gen_install(consumer_ctx, gen)`.

2. **op_attach_koskari_agen_frame** becomes:
   - Get `gen = agen->gen`.
   - If GEN.active is NULL (first step): set active from agen->ctx top, detach from agen->ctx.
   - Call `kos_dyn_gen_install(consumer_ctx, gen)`.
   - Set pending_agen etc. as before.

3. **On yield (flatten path)**: Frame's parent is GEN, GEN's parent is consumer frame. When parent is GEN and pending_agen is set (koskari-driven): do freeze, push value onto consumer stack, clear pending_agen, return 0.

4. **YIELD handler distinction**: (a) GEN on agen->ctx (Python-driven) -> emit event and return 1. (b) GEN on consumer ctx (koskari-driven, pending_agen set) -> freeze, push to consumer, return 0.

5. **ITER_NEXT and NEXT**: Both use op_attach. Update both to use the new install flow.

## Acceptance Criteria

- `test_next_accumulates_values_from_koskari_async_gen` passes (Koskari consumer, Koskari gen).
- `test_foreach_iter_next_over_koskari_async_gen` passes.
- `test_next_with_await_in_generator` passes.
- `test_next_1_on_koskari_agen_exhausted_raises_stopiteration` passes.
- No regression in Python-driven or Python async iterator tests.
