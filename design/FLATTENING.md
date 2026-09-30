# Flattening

A call into Koskari made from Koskari flattens: it installs into the
running evaluator and delivers its result through a resume, with no new
context, no nested `koskari_eval`, and no C stack per level. A path that
fails to flatten is a bug. The invariant is recorded in
`koskari/lib/CLAUDE.md` (Core invariant: flattening).

This rollup classifies Koskari-to-Koskari call and drive paths. Current
corrective work: [interpreter/demand-flattening.md](interpreter/demand-flattening.md).

## Flattens

- **Function and expression calls.** `KosFunction` / `KosExpression`
  installs from the `CALL_*` family through `kos_call_invoke_impl`
  (`eval/eval_call.c`), including tail calls (frame reuse in
  `KosEvalState_InstallCallFrame`).
- **Bound methods.** `kos_vinstall_boundmethod` (`func.c`) calls the
  function through `KosEvalState_CallMethod` with self as the receiver.
- **Instance calls.** `kos_instance_call_target` (`object.c`) reports
  the KosCallable `__call__` found on the type and whether it binds.
  The class dispatch re-enters on the target with the instance as the
  receiver when it binds, and without it otherwise.
- **`partial` and `values`.** Each install merges its bound tuple and
  dict with the call vector into one borrowed vector
  (`KosArgSpread_InitMerged`) and calls the inner through
  `KosEvalState_Call`. A KosCallable inner installs or runs in this
  evaluator; a Python inner is called and an awaitable result awaited.
- **`Demand`.** `demand_kos_vinstall` calls the inner through
  `KosEvalState_Call`, marks an installed callee `KOS_FRAME_DEMAND`, and
  settles an awaitable in hand under a demand-marked native frame
  (`KosEvalState_SettleInto`).
- **`Procedure` (class `__init__`) called from Koskari.**
  `KosEvalState_CallInto` with a resume that completes with None. A
  tail call reuses the live frame.
- **A fresh Koskari coroutine reaching an await.** `kos_settle_value`
  installs it as a call frame on the running chain, whether it came
  from a Python wrapper such as `functools.partial` or from a `defer`
  awaited directly.
- **Generator construction.** A generator function call binds arguments
  and returns the generator; nothing is evaluated.
- **Iteration and delegation over KosIter providers.** `for-each`,
  `ITER_NEXT`, and `yield-from` over an `AsyncGenerator` or a transform
  splice the provider's work into the running chain
  (`kos_iter_acquire`, `KosIterRequest_Next`, `yield_from_classify`,
  `dynchain/dyn_gen.c`). The splice copies `KOS_FRAME_DEMAND` from the
  invoking frame.
- **Iteration through a `KosPyIter`.** The CPython
  iterator a provider's `tp_iter` returns (formerly `DemandGenerator`)
  is a KosIter provider whose suite forwards to its provider, so
  `for-each` / `yield-from` over `(iter (gen))` splice as the bare
  provider does, carrying only the consumer's demand state.
- **A generator-function `__iter__` called from Koskari.** The class
  builder wraps it in `KosIterFunction`, whose install
  is the function's own: `(.__iter__ obj)` gets the `AsyncGenerator`.
  `for-each` over the instance acquires through `tp_iter`, a slot call
  that evaluates nothing (construction only) and yields a `KosPyIter`,
  which then splices as above.
- **Transform callbacks.** `map`, `filter`, `starmap`, `accumulate`,
  `takewhile`, `dropwhile` call their function through
  `KosEvalState_Call` from a code-less resume frame.
- **Continuations.** `call/cc`, `prompt` / `shift` transfers run on the
  running chain. A continuation invoked from a code-less resume frame
  (a transform callback, a `Demand` over a continuation) returns into
  that frame's resume.

## Should flatten, does not

- **Opcodes that call Koskari callables.** `OBJECT_REPR` with a Koskari
  repr handler or `__kos_repr__` (`KosUtils_Repr` ->
  `Koskari_DemandSync`, `utils.c`); runtime match predicates and APP
  extractors (`MATCH_CASE` -> `matcher_call_extern` ->
  `Koskari_DemandSync`, `matcher/datum.c`). Tracked as a group in
  Future.
- **Opcodes that materialise a Koskari generator.** The UNPACK family
  (`KosEvalState_PopUnpackSequence` -> `PySequence_Fast`,
  `eval/eval_state.c`) and the star-call family (`kos_op_call_star` ->
  `PySequence_Tuple`, `eval.c`) go through the generator's `tp_iter`
  into a `KosPyIter`, a nested evaluator per element on the demand
  path, so a generator whose body awaits fails as a sync violation
  where `for-each` over it flattens. The demand path is forced today.
  The collecting row, one NEXT node per element under the row resume,
  is section 7 of
  [interpreter/resumable-reads.md](interpreter/resumable-reads.md).

## Not expected to flatten

The Python boundary. Each entry here runs the boundary's own entry (a
fresh context and `koskari_eval`), resets the per-context frame depth,
and is counted by `Py_EnterRecursiveCall` at `koskari_eval` entry.

- **Type instantiation.** `(C args)` calls the type: `type_call` ->
  `tp_new` -> `slot_tp_init` -> the Procedure's vectorcall -> the demand
  core.
- **Dunders reached through CPython slots.** `tp_repr`, `tp_str`,
  `tp_richcompare`, `tp_hash`, `tp_as_number`, `tp_as_sequence`,
  `tp_as_mapping`, `tp_iter`, `tp_iternext`, `tp_getattro`, `tp_descr_*`,
  and the rest of `demand_dunders` (`object.c:~90-130`), including when a
  Koskari operator (`==`, `str`, `len`, ...) reaches them.
- **Python callables invoking Koskari callables.** `sorted(key=...)`,
  `functools`, `operator`, any Python library given a Koskari callable
  (`demand_vectorcall` -> `Koskari_DemandSyncVector`, `demand.c`;
  `Koskari_AsyncCall`).
- **Undelimited continuations on a foreign context.** Applied
  abortively in the capturing context through `KoskariContinuationApply`.
  A delimited continuation is not in this list: from outside its
  delimiter it grafts onto the call site on any context.
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
