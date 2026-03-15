# Phase 8: Agen Dealloc When Frozen

**Status:** Implemented.

## Objective

When an async generator is freed (GC/dealloc) while frozen (agen->gen set, GEN.active holds the suspended chain, agen->ctx chain empty), emulate Python's close behavior. We do not create a new context; we use agen->ctx, install GEN onto it like normal, then drive it via demand with GeneratorExit injected as a throw.

## Background

Python's async generator close(): inject GeneratorExit, run to completion. If the generator catches it, does cleanup, and re-raises (or raises StopIteration, or returns), we're done. If it tries to AWAIT or YIELD again, we cannot honor that—abort and fall back to pure cleanup.

## Files to Modify

- `koskari/lib/iter.c`: `asyncgen_dealloc` (and possibly `asyncgen_clear` for GC)
- Possibly a new helper in `koskari/lib/iter.c` or `eval.c` for the demand-driven close loop

## Preconditions

- Agen is frozen: `agen->gen != NULL`, `KosDynGen_ACTIVE(agen->gen)` holds the suspended chain, `KosContext_DYN_TOP(agen->ctx) == NULL`
- We are in dealloc (or clear) — the agen is being torn down

## Implementation

### Primary Path: Throw-Based Unwind

1. **Prepare GeneratorExit**
   - Use `throw_typ = PyExc_GeneratorExit`, `throw_exc = PyObject_CallNoArgs(PyExc_GeneratorExit)` or equivalent instance
   - Match Python close semantics (inject as throw, not PyErr directly)

2. **Drive via demand**
   - Frozen state: agen->ctx has dyn_top=NULL, SUSPENSION_EVENT=SUSP_YIELD, RESUME_GEN=gen
   - Call `koskari_eval(agen->ctx, &ev, NULL, throw_typ, throw_exc)`
   - eval_phase_entry sees SUSP_YIELD with throw set; calls revive_yield. Revive installs gen onto ctx (restores chain), pops install's Py_None, sets PyErr for throw, returns KOS_MODE_EXCEPTION. Eval runs; gen receives GeneratorExit. No pre-install; revive handles it. Single eval call—no loop.

3. **Interpret the result**
   - Inspect `res` and `ev` from the eval call above.
   - If `res < 0`: exception. Check PyErr. If GeneratorExit (or StopIteration after generator catches and returns), clear it—success, proceed to final cleanup.
   - If `res == 0` and `KosEvent_KIND(&ev) == KOS_EV_AWAIT` or `KOS_EV_YIELD`: suspend attempt. We cannot honor it. **Abort to fallback.**
   - If `res == 0` and `KOS_EV_RETURN` or `KOS_EV_RAISE`: terminal. If RAISE with GeneratorExit, clear and success. If RETURN (gen returned None or value after catching), success. Proceed to final cleanup.
   - If we get AWAIT or YIELD at any point: abort immediately to fallback.

4. **Final cleanup (primary path success)**
   - PyErr_Clear() if we stashed GeneratorExit
   - After RETURN or RAISE, the chain still has frame->gen on ctx (RETURN emits without detaching in Phase 2c). Call `kos_dyn_gen_unwind_exhausted(agen->ctx)` to free the chain, restore dyn_top (NULL), release gen. Then `kos_context_free(agen->ctx)`.

### Fallback: Pure Cleanup

When we detect AWAIT or YIELD (suspend attempt):

1. **Abort the throw-based run**
   - PyErr_Clear() to drop any exception state
   - Do NOT continue eval

2. **Pure cleanup pass**
   - `active = KosDynGen_ACTIVE(agen->gen)` (the stored chain)
   - `kos_dynchain_free(active, (KosDynNode *) agen->gen)` — free from active down to but not including gen
   - `kos_dyngen_release((KosDynGen *) agen->gen)` — release gen (may free it)
   - `agen->gen = NULL`
   - `kos_context_free(agen->ctx)` — free the context
   - `agen->ctx = NULL` (in clear; in dealloc we're about to free the agen struct)
   - No further user code runs

### When Not Frozen

If the agen is not frozen (agen->gen is NULL, or ctx has dyn_top), use the current logic: release gen if present, free ctx.

### GC Integration

`asyncgen_clear` is called during GC. It must do the same teardown when frozen: attempt throw-based unwind, fallback to pure cleanup. Then clear agen->ctx and agen->gen so the agen can be resurrected or finalized without dangling refs.

## Edge Cases

- **Gen has no active**: If GEN.active is NULL (shouldn't happen when frozen), skip install and fall straight to release/free.
- **Install fails**: If kos_dyn_gen_install fails (e.g. gen has no active), fall to fallback.
- **Multiple wind/finally**: The unwind will naturally run them. If a wind after-thunk or finally does await, we get AWAIT—abort.
- **GeneratorExit swallowed and something else raised**: We get KOS_EV_RAISE with a different exception. We could either treat as "did our best" and clear, or propagate. Python closes and if the gen raises something other than GeneratorExit/StopIteration, that propagates. For dealloc we probably clear and continue—we're tearing down.

## Acceptance Criteria

- Agen freed while suspended runs finally blocks and wind after-thunks before teardown (when no suspend in cleanup).
- Agen that yields or awaits during close triggers fallback; no crash, no leak.
- GC clear of frozen agen does not leak; agen can be collected.
- Test: create agen, advance to first yield, drop all refs (no close), GC collects—verify no crash, proper cleanup.
- Test: agen with try/finally in body; drop at yield; verify finally runs.

## Dependencies

- Phase 7 (Koskari flatten) complete; kos_dyn_gen_install, freeze, revive all working
- kos_dyn_gen_unwind_exhausted exists for consumer exhaustion; we may need analogous logic for agen->ctx when gen returns during close (or reuse it)
