# Phase 6: Revive Integration

## Objective

On resume from KOS_SUSP_YIELD, call install before pushing send value. Ensure ctx has a way to find GEN (or agen) for install.

## Files to Modify

- `koskari/lib/eval.c`: `revive_yield`; optional `eval_phase_entry`; context field for resume GEN
- `koskari/lib/iter.c`: Ensure agen passes ctx that can be revived (agen->ctx)
- `koskari/lib/coro.c`: Wire agen->gen and ctx so resume can find GEN

## Implementation

1. **Context resume link**: Add `KosDynGen *resume_gen` to KosContext. When we freeze, set `agen->ctx->resume_gen = gen`. When we revive, check `ctx->resume_gen`; if set, call `kos_dyn_gen_install(ctx, ctx->resume_gen)`, then clear `ctx->resume_gen`.

2. **When to set resume_gen**: In freeze, after releasing GEN to agen, set `agen->ctx->resume_gen = gen`.

3. **revive_yield**:
   - If `ctx->resume_gen != NULL`: call `kos_dyn_gen_install(ctx, ctx->resume_gen)`, clear `ctx->resume_gen`.
   - Install pushes Py_None; **pop it** then push send_val (or Py_None) — avoid double-push.
   - If no resume_gen, just push send_val/Py_None.
   - Return KOS_MODE_NORMAL.

4. **send_val NULL (anext):** `__anext__` creates ASend with `send_value=NULL`. In `eval_phase_entry`, when `send_val==NULL && SUSP_YIELD`, **must call `revive_yield(ctx, Py_None, ...)`** — treat NULL as Py_None per async gen protocol. Do not skip revive.

5. **Traverse**: Ensure `resume_gen` is traversed for GC. Also release `resume_gen` in `kos_context_free` when tearing down.

## Acceptance Criteria

- `test_next_accumulates_values_from_koskari_async_gen` passes (Python consumer, Koskari gen).
- `test_foreach_iter_next_over_koskari_async_gen` passes (if Python consumer with PUSH_FOREACH over Koskari gen).
- Multi-yield resume: gen yields 1, 2, 3; consumer gets all three.
