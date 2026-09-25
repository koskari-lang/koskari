# Flattening

A call into Koskari made from Koskari flattens: it installs into the
running evaluator and delivers its result through a resume, with no new
context, no nested `koskari_eval`, and no C stack per level. A path that
fails to flatten is a bug. The invariant is recorded in
`koskari/lib/CLAUDE.md` (Core invariant: flattening).

This rollup classifies Koskari-to-Koskari call and drive paths. Current
corrective work: [interpreter/demand-flattening.md](interpreter/demand-flattening.md).

Entries marked (working tree) are uncommitted as of 2026-09-25.

## Flattens

- **Function and expression calls.** `KosFunction` / `KosExpression`
  installs from the `CALL_*` family through `kos_call_invoke_body`
  (`eval/eval_call.c`), including tail calls (frame reuse in
  `do_frame_prep`).
- **Bound methods.** `kos_vinstall_boundmethod` (`func.c:729`) prepends
  self and installs the function.
- **Instance calls.** `__call__` resolving to a KosCallable is inlined
  through the `kos_instance_call` sentinel (`object.c:522`).
- **`partial` over a frame-wanting inner without keywords.** The vector
  path in `kospartial_kos_vinstall` (`partial.c:190-220`).
- **`Demand` over a frame-wanting inner.** `demand_kos_vinstall`
  installs the inner and marks it `KOS_FRAME_DEMAND`.
- **`Demand` over any other inner.** (working tree) `KosEvalState_CallInto`
  with a pass-through resume.
- **`Procedure` (class `__init__`) called from Koskari.** (working tree)
  `KosEvalState_CallInto` with a resume that completes with None.
- **Generator construction.** A generator function call binds arguments
  and returns the generator; nothing is evaluated.
- **Iteration and delegation over KosIter providers.** `for-each`,
  `ITER_NEXT`, and `yield-from` over an `AsyncGenerator` or a transform
  splice the provider's work into the running chain
  (`kos_iter_acquire`, `KosIterRequest_Next`, `yield_from_classify`,
  `dynchain/dyn_gen.c`). The splice copies `KOS_FRAME_DEMAND` from the
  invoking frame.
- **Iteration through a `KosPyIter`.** (working tree) The CPython
  iterator a provider's `tp_iter` returns (formerly `DemandGenerator`)
  is a KosIter provider whose suite forwards to its provider, so
  `for-each` / `yield-from` over `(iter (gen))` splice as the bare
  provider does, carrying only the consumer's demand state.
- **Transform callbacks.** `map`, `filter`, `starmap`, `accumulate`,
  `takewhile`, `dropwhile` call their function through
  `KosEvalState_CallResult` from a code-less resume frame.
- **Continuations.** `call/cc`, `prompt` / `shift` transfers run on the
  running chain.

## Should flatten, does not

- **`partial` over a non-frame KosCallable inner, or with keywords.**
  `kospartial_kos_vinstall` calls `KosPartial_Call` (`partial.c:254`),
  reaching `demand_sync_ex` or another Python entry. Scheduled:
  demand-flattening step 3.
- **Opcodes that call Koskari callables.** `OBJECT_REPR` with a Koskari
  repr handler or `__kos_repr__` (`KosUtils_Repr` ->
  `Koskari_DemandSyncEx`, `utils.c:520`, `:561`); runtime match
  predicates and APP extractors (`MATCH_CASE` -> `matcher_call_extern`
  -> `Koskari_DemandSync`, `matcher/datum.c:485`). Tracked as a group in
  Future.

## Not expected to flatten

The Python boundary. Each entry here runs the boundary's own entry (a
fresh context and `koskari_eval`), resets the per-context frame depth,
and is counted by `Py_EnterRecursiveCall` at `koskari_eval` entry
(working tree).

- **Type instantiation.** `(C args)` calls the type: `type_call` ->
  `tp_new` -> `slot_tp_init` -> `procedure_tp_call` -> the demand core.
- **Dunders reached through CPython slots.** `tp_repr`, `tp_str`,
  `tp_richcompare`, `tp_hash`, `tp_as_number`, `tp_as_sequence`,
  `tp_as_mapping`, `tp_iter`, `tp_iternext`, `tp_getattro`, `tp_descr_*`,
  and the rest of `demand_dunders` (`object.c:~90-130`), including when a
  Koskari operator (`==`, `str`, `len`, ...) reaches them.
- **Python callables invoking Koskari callables.** `sorted(key=...)`,
  `functools`, `operator`, any Python library given a Koskari callable
  (`KosCallAdapt_CallDemand` / `VectorDemand`, `callable.c:295/319`;
  `Koskari_AsyncCall`).
- **Python drives of Koskari generators.** `next` / `send` / `throw` /
  `close` / `list` / `for` from Python through the sync adapter;
  `__anext__` / `asend` / `athrow` / `aclose` through the async steppers
  (`iterate.c`); coroutine `send` / `throw` / `close` (`coro.c`).
- **The demand API from Python.** `demand_call`, `demand_apply`,
  `demand(...)(...)` (`demand.c`).

## Future

### Opcodes that dispatch to Koskari callables

Opcodes whose work may run a Koskari callable need to behave like a
`CALL` op in that case: install the callable through `CallInto` with a
resume that finishes the opcode's work on the delivered value. One
design for the group.

- **`OBJECT_REPR`.** A Koskari repr handler or `__kos_repr__`, with a
  resume that validates the str result. `KosUtils_Repr` keeps the demand
  core for its `tp_repr` (boundary) callers (`pair.c:582/605`,
  `values.c:717/737`, `partial.c:479-508`).
- **Runtime match predicates and APP extractors** (stretch). The
  `MATCH_CASE` step suspends on the call and resumes the match with the
  delivered value, which needs the match engine's state held across the
  call. A match entered from Python keeps the demand core.
- **Koskari operators on Koskari dunders** (candidate). `==`, `str`,
  `len`, and the rest reach Koskari dunders through CPython slots and
  nest per call. Dispatching a Koskari dunder directly from the
  evaluator, as `kos_instance_call` does for `__call__`, would flatten
  them.

### Other

- **Tail calls through `Procedure` and non-flattened `Demand`.** Both
  installs call `CallInto` with `tail_call` 0; a tail call through
  either costs two frames per level (a code-less resume frame and the
  callee).
