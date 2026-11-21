# Flattening

A call into Koskari made from Koskari flattens: it installs into the
running evaluator and delivers its result through a resume, with no new
context, no nested `koskari_eval`, and no C stack per level. A path that
fails to flatten is a bug. The invariant is recorded in
`koskari/lib/CLAUDE.md` (Core invariant: flattening).

This rollup classifies Koskari-to-Koskari call, drive and await paths.
Current corrective work:
[archive/demand-flattening.md](archive/demand-flattening.md).

## Flattens

- **Function and expression calls.** `KosFunction` / `KosExpression`
  installs from the `CALL_*` family through `kos_call_invoke_impl`
  (`eval/eval_call.c`), including tail calls (frame reuse in
  `KosContext_InstallCallFrame`).
- **Bound methods.** `kos_vinstall_boundmethod` (`func.c`) calls the
  function through `KosContext_CallMethod` with self as the receiver.
- **Instance calls.** `kos_instance_call_target` (`object.c`) reports
  the KosCallable `__call__` found on the type and whether it binds.
  The class dispatch re-enters on the target with the instance as the
  receiver when it binds, and without it otherwise.
- **`partial` and `values`.** Each install merges its bound tuple and
  dict with the call vector into one borrowed vector
  (`KosArgSpread_InitMerged`) and calls the inner through
  `KosContext_Call`. A KosCallable inner installs or runs in this
  evaluator; a Python inner is called and an awaitable result awaited.
- **`Demand`.** `demand_kos_vinstall` calls the inner through
  `KosContext_Call`, marks an installed callee `KOS_FRAME_DEMAND`, and
  settles an awaitable in hand under a demand-marked native frame
  (`KosContext_SettleInto`).
- **`Procedure` (class `__init__`) called from Koskari.**
  `KosContext_CallInto` with a resume that completes with None. A
  tail call reuses the live frame.
- **An await of a Koskari awaitable.** The settle (`kos_settle_value`)
  and the `AWAIT` opcode take an awaitable whose type carries the await
  suite (`kos_await` on `KosEvaluatedTypeObject`, `evaluated.h`) on an
  await frame: a native frame pushed above the awaiter, uncounted
  against the frame limit, whose resume is the suite's kind, entered
  with the owned awaitable. The kind continues the awaitable's chain
  under that frame, in whatever state the chain is. A fresh
  coroutine's prepared frame swaps into the await frame as a tail
  call's does, whether it came from a Python wrapper such as
  `functools.partial` or from a `defer` (`coro.c`). A coroutine parked
  at a Python await, started by a Python `send` and left there, splices
  its whole chain under the await frame (`kos_dyn_span_detach`,
  `kos_dyn_span_attach`, `dynchain/dyn_gen.c`); the pending Python
  await moves to the awaiter's context, and the awaiter suspends on the
  value the coroutine had handed its driver, which the context records
  at every suspension (`py_await_yielded`). The coroutine is active
  until its chain passes the await frame, whose resume finishes it,
  and refuses every entry meanwhile. A Defer delegating to an awaitable
  with a suite hands it on and is spent (`defer.c`). An awaited
  `__anext__`, `asend`, `athrow` or `aclose` stepper over a Koskari
  async generator is a next or throw request on the await frame with
  the step kind as its resume (`iterate.c`, `eval/eval_iter.c`), and a
  step a Python drive left parked mid-await splices as a coroutine's
  chain does, its generator recording that the step runs on a
  consumer's chain. A Python awaitable, or a Koskari one that is
  active or done, goes to the Python-protocol drive on the same frame
  (`kos_await_python`, `eval/eval_await.c`). A splice refused for frame
  room leaves the chain parked; a fresh coroutine refused is finished,
  as CPython's is when its first step cannot enter.
- **Generator construction.** A generator function call binds arguments
  and returns the generator; nothing is evaluated.
- **Iteration and delegation over KosIter providers.** `for-each`,
  `ITER_NEXT`, and `yield-from` over an `AsyncGenerator` or a transform
  splice the provider's work into the running chain
  (`kos_iter_acquire`, `kos_iter_fetch`, `yield_from_classify`,
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
  `KosContext_Call` from a code-less resume frame.
- **Match predicates and extractors.** A pattern is `MATCH_*`
  instructions in the frame that matches, and a `(? pred)` or
  `(app f ...)` is a `CALL` from that frame
  ([archive/matcher-bytecode.md](archive/matcher-bytecode.md)).
  A matcher entered from Python (`Pattern.match`, a `Syntax`'s
  `first_match`) is one `Function` call through the demand entry, and the
  calls inside it flatten as in any function.
- **Continuations.** `call/cc`, `prompt` / `shift` transfers run on the
  running chain. A continuation invoked from a code-less resume frame
  (a transform callback, a `Demand` over a continuation) returns into
  that frame's resume.

## Should flatten, does not

- **Opcodes that call Koskari callables.** `OBJECT_REPR` with a Koskari
  repr handler or `__kos_repr__` (`KosUtils_Repr` ->
  `Koskari_DemandSync`, `utils.c`). Tracked in Future. The demand path
  is forced today.
- **Opcodes that materialise a Koskari generator.** The UNPACK family
  (`KosContext_PopUnpackSequence` -> `PySequence_Fast`,
  `eval/eval_drive.c`) and the star-call family (`kos_op_call_star` ->
  `PySequence_Tuple`, `eval.c`) go through the generator's `tp_iter`
  into a `KosPyIter`, a nested evaluator per element on the demand
  path, so a generator whose body awaits fails as a sync violation
  where `for-each` over it flattens. The demand path is forced today.
  The collecting row, one next request per element under the row resume,
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
  (`iterate.c`); coroutine `send` / `throw` / `close` (`coro.c`). A
  Python `await` of a Koskari coroutine parked at an inner await is
  refused at `__await__`, as CPython refuses its own; the Koskari
  await of the same coroutine splices it (Flattens, above).
- **A Koskari await of an active or finished Koskari awaitable.** The
  await kind hands a coroutine or stepper that is running, on this or
  any chain, or already done, to the Python-protocol drive, whose
  `send` refuses it as in progress or as reused.
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
- **Koskari operators on Koskari dunders** (candidate). `==`, `str`,
  `len`, and the rest reach Koskari dunders through CPython slots and
  nest per call. Dispatching a Koskari dunder directly from the
  evaluator, as `kos_instance_call` does for `__call__`, would flatten
  them.
