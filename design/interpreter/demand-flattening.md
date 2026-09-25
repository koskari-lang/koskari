# Demand path flattening

Status: in progress, uncommitted. Started 2026-09-25 from the design
TODO entry "Recursion guard at the Python boundary". Reviewed by two
independent passes (plan review, implementation advice); their verified
findings are folded in. Revised the same day: demand applies to driving
a generator, not to constructing one; `DemandGenerator` becomes
`KosPyIter`, a CPython iterator adapter that Koskari flattens through
without applying demand; an `__iter__` bound to a generator function
presents its generator to Python as one.

Progress: steps 1 (D5), 2 (`KosIterFunction`), and 3 (D8) are
committed; D7 is resolved by an approved contract change; step 4 (D4,
D6) is done in the working tree with its contract changes applied. D11
(composable continuations from code-less frames) is open and blocks
closing the arc. Steps 5-7 remain.

## Invariant

A call into Koskari made from Koskari flattens: it installs into the
running evaluator and delivers its result through a resume. A new
context or eval state running a nested `koskari_eval` for such a call is
a bug. Recorded in `koskari/lib/CLAUDE.md` (Core invariant: flattening).

The Python boundary is outside the invariant. Calling a type is a
Python-boundary allocator with native rules (`type_call` -> `tp_new` ->
`slot_tp_init`); a dunder reached through a CPython slot (`tp_repr`,
`tp_iter`, `tp_richcompare`, ...) is the same; so is Python calling
`next()` on a Koskari generator. When that boundary calls into Koskari,
the boundary's own entry is the established mechanism. An opcode or
native helper invoked by Koskari bytecode that calls a Koskari callable
directly (not through a CPython slot) is Koskari-originated.

## Demand belongs to the drive

The demand restriction (no suspension) applies when a generator is
entered: next, send, throw, close. Calling a generator function
evaluates nothing; it binds arguments and returns the generator. So a
generator is never marked, converted, or wrapped for demand at
construction.

- In the evaluator this is already the rule: the generator splice
  copies `KOS_FRAME_DEMAND` from the invoking frame
  (`dynchain/dyn_gen.c:286-288`, `:390-392`), so a drive under the
  demand path is demand, and a drive outside it is not.
- From Python, a synchronous drive (`next`, `send`, `throw`, `close`,
  `for`, `list`) is a boundary drive and applies demand; an asynchronous
  one (`__anext__`, `asend`, `athrow`, `aclose`, `async for`) does not.
  The KosIter providers (`AsyncGenerator` and the transforms) stay
  async-only types. Their `tp_iter` returns a `KosPyIter`, the CPython
  iterator adapter, whose Python-side drive applies demand.

`KosPyIter` is the former `DemandGenerator` renamed (`KosIterDemandGenType`
-> `KosPyIterType`, `KosIterDemandGen` -> `KosPyIter`,
`KosIterAdapt_DemandIter` -> `KosIterAdapt_PyIter`, Python name
`koskari.lib.Iterator`). The name describes an iterator adapter for
CPython, not a demand mechanism, following `KosPyAsyncGen` and
`KosPyContinuation`.

A `KosPyIter` that reaches Koskari code is its provider. Koskari drives
through it flattened and applies demand only if the Koskari drive itself
is under the demand path (the splice rule above). Wrapping in a
`KosPyIter` never obliges demand on a Koskari-side drive.

CPython's `slot_tp_iter` requires `__iter__` to return an iterator. A
Koskari `__iter__` written as a generator function returns an
`AsyncGenerator`, so the class builder wraps such an `__iter__` in
`KosIterFunction`, whose Python call path presents the generator as a
`KosPyIter`. This is limited to a generator-function `__iter__`, where
the protocol demands an iterator; any other `__iter__` must return one
itself, no other result is converted at the boundary, and `demand(...)`
returns a generator as the `AsyncGenerator` it is.

## Findings

### Origin

The TODO entry claimed a three-level `(super)` chain segfaults for lack
of `Py_EnterRecursiveCall` around the nested eval. A correct three-level
chain runs (corpus `class-layout-inheritance-super-002`). What
segfaults is unbounded recursion through `__init__` called from
Koskari.

Measured on that recursion (gdb, default build): each level is 10 C
frames and 1,072 bytes of C stack:

| bytes | frame |
|---:|---|
| 240 | `kos_call_body_window_owned_tail` |
| 224 | `koskari_eval` |
| 160 | `demand_sync_ex` |
| 160 | `KosEvalState_Call` |
| 80 | `procedure_kos_vinstall` |
| 80 | `kos_vinstall_boundmethod` |
| 64 | `op_CALL_ATTR` |
| 32 | `kos_call_invoke_body` |
| 16 | `KosCallable_Call` |
| 16 | `KosCallable_CallSlots` |

No CPython frame is in that loop: Koskari calling a Koskari method,
nesting an eval per level (D1).

`Py_EnterRecursiveCall` was added at `koskari_eval` entry and exit. On
3.12 its C recursion budget (about 10,000 levels) exceeds an 8 MB stack
at about 1 KB per level; with a 16 MB stack the probe raises
`RecursionError: maximum recursion depth exceeded in koskari_eval`. With
D1 fixed, Koskari-originated recursion never reaches it: flattened calls
are counted by the per-context frame-depth limit (`KosDynFrame_DEPTH`),
which a nested eval had reset to 1 on each fresh context.

### Defects

**D1. `Procedure` never flattened.** Fixed in the working tree.
`procedure_kos_vinstall` (`object.c`) boxed its arguments into a tuple
and dict and ran `Koskari_DemandSyncEx`: a fresh context and a nested
`koskari_eval` per call, `tail_call` ignored. `demand_wrap_namespace`
wraps every KosCallable `__init__` in a `Procedure` (`object.c:425`),
so every Koskari-to-Koskari `__init__` call nested. Consequences: C
stack per level, the frame-depth limit bypassed (segfault instead of
`RecursionError`), a tuple, dict, context, and eval entry and exit per
call. `Demand` had flattened since 2026-06-21 (`6ecfeede`); `Procedure`
was written later (`4794f20f`, `a2461523`) against the cold path only.

**D2. `Demand`'s cold fallback nested.** Fixed in the working tree.
`demand_kos_vinstall` flattens a frame-wanting inner, but any other
inner (a `partial`, a continuation, a yielding function) was packed into
a tuple and dict for `demand_sync_ex`, which nested an eval, or refused
the callable (D4).

**D3. No call-or-install entry for a non-resume context.** Fixed in the
working tree. `KosEvalState_CallResult` arms its resume on the current
frame (`eval/eval_call.c:290`, `:318-335`); inside an install slot that
is the calling bytecode frame, and the return fast paths assume only
code-less frames carry resumes (`eval.c:2539-2548`). Each wrapper
hand-rolled the value-in-hand case and grew a packed fallback.
`KosEvalState_CallInto` (`eval/eval_call.c`, declared in `eval.h`)
provides the code-less frame.

**D4. The boundary refuses most KosCallables.** Fixed in the working
tree (step 4).
`demand_sync_ex` (`demand.c:318-325`) accepts only exact `KosFunction`
and `KosExpression`. Any other KosCallable reached from Python raises
`TypeError: demand_sync does not support ...`: a `partial` bound as
`__init__` (`slot_tp_init` -> `KosCallAdapt_CallAsync` ->
`procedure_tp_call` -> `Koskari_DemandSync`), `demand(partial(...))(x)`
called from Python, any `Demand`-wrapped dunder bound to a `partial` or
continuation. The same function hand-rolls call-or-install: `BoundMethod`
unwrap with `PySequence_Concat`, recursive `Demand` unwraps, a
`CallResult` / `CallArgs` split on keywords, hand-sorted pre-eval
outcomes, an "install left no code" check, manual `KOS_FRAME_DEMAND`
marking, and a yielding branch that converts the generator to a
`KosPyIter` (`demand.c:333-350`). Unchanged on master; reproduced
on the current build from pure Python.

**D5. Koskari iteration of a `DemandGenerator` nests.** Fixed in the
working tree (step 1).
`KosIterDemandGenType` is a plain `PyTypeObject` without KosIter slots;
`kos_iter_acquire` (`eval/eval_iter.c:44`) passes through only KosIter
providers and `am_aiter` objects, so Koskari iterating a
`DemandGenerator` goes `PyObject_GetIter` -> `demandgen_iternext` ->
`demandgen_step` -> `koskari_eval(gen->host, ...)` (`iterate.c:1087`),
and `yield_from_classify` (`opcode/YIELD_FROM.c:33`, `:128`) the same.

**D6. Generators are converted for demand at the boundary.** Fixed in
the working tree (step 4). The boundary's yielding branch
(`demand.c:333-350`) converted a
yielding function's generator to a `KosPyIter`, reached only
through its `Demand` and `BoundMethod` unwraps. With demand applied at
the drive, the generator is returned as is and the branch is removed.

**D7. D2's change broke an existing test.** Resolved: contract change
approved and applied in the working tree.
`tests/lib/test_demand_sync.py::TestDemandCallFlattening::test_fallback_yielding_function_returns_generator`
asserted `hasattr(result, '__next__')` and `next(result) == 7` on the
result of `demand(yielding_fn)` called from Koskari. Under the drive
rule that result is the `AsyncGenerator` itself; the test now asserts
`isinstance(result, AsyncGenerator)` and `next(iter(result)) == 7`.

**D8. `KosPartial` nests for KosCallable inners.** Fixed in the working
tree (step 3). `kospartial_kos_vinstall` routed any non-frame inner to
`KosPartial_Call`
(`partial.c:254`), a Python-boundary call made from Koskari. For a
KosCallable inner (a `Demand`, a `Procedure`, a continuation, a nested
non-frame KosCallable) that reaches `demand_sync_ex` or another Python
entry: `partial(demand(partial(f)))` from Koskari hit the D4 refusal,
and after step 4 would have run a nested eval. The same arm pushed the
raw result, so a partial over a Python coroutine function called from
Koskari delivered the unawaited coroutine; both were shown red before
the fix.

**D9. `OBJECT_REPR` nests for Koskari repr handlers.**
Deferred to the opcode dispatch group in
[../FLATTENING.md](../FLATTENING.md) Future.
`OBJECT_REPR` (`opcode/OBJECT_REPR.c:28`) -> `KosUtils_Repr` ->
`Koskari_DemandSyncEx` for a registered handler (`utils.c:520`) or
`__kos_repr__` (`utils.c:561`). A Koskari handler or `__kos_repr__`
nests an eval under a Koskari opcode. The `tp_repr` paths reaching the
same helper (`pair.c:582/605`, `values.c:717/737`, `partial.c:479-508`)
are CPython slots: boundary.

**D10. Runtime match predicates nest.**
Deferred (stretch) to the opcode dispatch group in
[../FLATTENING.md](../FLATTENING.md) Future. `MATCH_CASE` ->
`KOS_MATCH_TEST_PREDICATE` (`matcher.c:1376-1384`) ->
`matcher_call_extern` -> `Koskari_DemandSync` (`matcher/datum.c:485`). A
Koskari predicate or APP extractor in a runtime `match` nests an eval.
The comment there ("Matching never enters the evaluator ... koskari
callables run on a demand eval drive") describes nesting as acceptable
and is removed with the fix.

**D11. Composable continuations invoked from a code-less resume frame.**
Open; predates this work for transforms. When a `shift` continuation's
landed segment completes, `POP_PROMPT` (`opcode/POP_PROMPT.c:67`) routes
the value to the recorded invoke-site frame through
`kos_eval_route_delim_return` (`eval.c:~1488`), which finishes the
delim (in-place: `kos_dyn_finish_inplace_pop_return`; push-graft:
`kos_dyn_finish_delimited_pop_return`) and resumes dispatch at the
invoker's pc. A code-less invoker has no pc:

- `(reset (* 2 (shift k ((demand k) 1))))` (invoked from `CallInto`'s
  frame, in-place path) segfaults in `op_POP_PROMPT`.
- `(reset (* 2 (shift k (list (map k #[1 2])))))` (invoked from a
  transform's resume frame) returns `2`, not `[2, 4]`.

Returning REPROLOGUE from the route when the invoker is code-less, so
the prologue delivers the value on its stack to the armed resume, stops
the crash but still yields `2` for `(+ ((demand k) 1) ((demand k) 2))`
and for the `map` case: the first delivery ends the reset. Not applied.
The in-place invoke snapshots the invoke site as a bytecode resume point
(`dynchain/xfer_plan.c:100-170`, including the nested-handler frame
clone), and the push-graft path sets the invoker's pc; neither yet has a
delivery-to-resume form. Needs a design pass through the in-place
restitch and the delimited return.

### Boundary sites (no action)

Python-driven entries that step or create a context from Python:
`iter.c:127/337`, `coro.c:291/366/435`, `iterate.c:238/624` (the async
steppers), `iterate.c:1087` (the `KosPyIter` Python-side drive),
`transform.c:316` (`kos_transform_pyctx`),
`callable.c:295/319` (`KosCallAdapt_CallDemand` / `VectorDemand`),
`object.c:163` (`procedure_tp_call`), bootstrap `_iter.ki:194`
(`(demand key)` passed to `sorted`).

### Behavior notes

- `tail_call` is dropped by the `Procedure` and non-flattenable `Demand`
  installs (`CallInto` passes 0). `do_frame_prep` leaves the caller for a
  wants-frame-0 callable and the RETURN after TAIL_CALL retires it
  (`eval/eval_call.c:36-60`), so results are correct, but a tail call
  through either wrapper costs two frames per level.
- `CallInto` reviewed for leaks, double frees, and stranded frames on
  the value-in-hand, settle, installed-callee, raise-before-install,
  raise-in-callee, and AWAIT-under-demand paths; none found. A raise
  before install leaves the code-less frame unarmed and the walk pops it
  (`dyn_walk.c:646-653`); `kos_dynframe_clear_resume` tolerates a NULL
  clear (`dyn_frame.c:120-125`).
- Continuations reached through `CallInto`: an escaping `call/cc`
  continuation through a `Demand` works (`(call/cc (lambda [k] ((demand
  k) 5) 99))` -> 5). A composable (`shift`) continuation invoked from
  any code-less resume frame is broken (D11).
- A retained `Context` from a boundary drive reports `namespace` as
  None after the drive ends: the root is the code-less `CallInto` frame,
  which has no namespace. `test_domain.py`'s running-context test now
  reads the namespace inside the drive; a snapshot API for finished
  contexts is a TODO entry.

## Working tree

Committed as WIP (`8d59c673`):

- `eval.c`: `Py_EnterRecursiveCall(" in koskari_eval")` after the
  fast-slot refusal, `Py_LeaveRecursiveCall()` before the context
  release.
- `eval.h`, `eval/eval_call.c`: `KosEvalState_CallInto`. Pushes a
  code-less frame with no resume armed, ORs `frame_flags` into it, calls
  through `KosEvalState_CallResult` with the resume, and stages a value
  back in hand for the resume (REPROLOGUE). It never reports RETURN. The
  frame starts unarmed: an armed frame makes `KosEvalState_Call` turn
  NORMAL into REPROLOGUE (`eval/eval_call.c:199-201`,
  `callable_promote_normal` `callable.c:73`), which `CallResult` reads
  as an installed callee, skipping settling.
- `object.c`: `procedure_resume` / `procedure_resume_kind` (drop the
  value, complete with None); `procedure_kos_vinstall` reduced to the
  empty-wrapper check and `CallInto(..., KOS_FRAME_DEMAND)`.
- `demand.c`: `demand_resume` / `demand_resume_kind` (complete with the
  value); the cold fallback in `demand_kos_vinstall` replaced by
  `CallInto(..., KOS_FRAME_DEMAND)`.
- Step 1: `DemandGenerator` renamed `KosPyIter` (`iterate.c`,
  `iterate.h`, `iter.c`, `demand.c`, the 14 `transform/*.c`); `KosPyIterType`
  is a `KosEvaluatedTypeObject` whose `kos_iter` suite forwards to the
  provider (`pyiter_ki_step`, `pyiter_ki_throw`, `pyiter_ki_close`,
  `pyiter_ki_finish`); stale comments corrected (`iterate.c`,
  `iterate.h`, `iter.c`, `callable.h`, `demand.c`, `eval/eval_iter.c`).
- `koskari/lib/CLAUDE.md`: the flattening invariant.
- `tests/reproducers/test_procedure_install.py`,
  `tests/reproducers/scripts/procedure_install.ki`.
- `tests/reproducers/test_pyiter_flatten.py`,
  `tests/reproducers/scripts/pyiter_flatten.ki`: Koskari `for-each` and
  `yield-from` over `(iter (gen))` see the consumer's `parameterize`;
  a suspension through the adapter proceeds off the demand path and
  raises `KoskariSyncViolation` under `demand_call`; Python iteration is
  unchanged. Not shown red against the pre-step-1 build.
- `tests/lib/test_demand_sync.py`: the D7 contract change.

Committed as WIP (`511636b3`):

- Step 2: `KosIterFunctionType` and `wrap_iter_function` (`object.c`);
  `tests/reproducers/test_iter_function.py`,
  `tests/reproducers/scripts/iter_function.ki`.

Committed as WIP (`33226f50`):

- Step 3: `kospartial_kos_vinstall` (`partial.c`) calls every inner
  through `KosEvalState_CallResult` / `KosEvalState_CallArgs`;
  `kospartial_inner_is_flattenable` removed;
  `tests/reproducers/test_partial_flatten.py`,
  `tests/reproducers/scripts/partial_flatten.ki`.

Uncommitted:

- Step 4: `demand.c` core rewritten (`demand_run_koskari` through
  `CallInto`, vector-native `demand_sync_vector`, `demand_resume` moved
  above it, `Koskari_DemandSyncVector`); `KosArgSpread` in
  `callable.h` / `callable.c`, used by `KosCallable_CallArgsSlots` and
  the demand core; `KosCallAdapt_VectorDemand` passes its vector
  through; the approved contract changes in `test_demand_sync.py` and
  `test_dunder_adapter.py`; new `TestDemandBoundaryCallables`.
- `test_domain.py`: the running-context namespace is read inside the
  drive; a TODO entry for a finished-context snapshot API.
- `test_procedure_install.py`: continuation cases through `Demand` and
  `map` (D11).

Test state: everything passes except the two D11 tests:
`test_demand_composable_continuation_twice` segfaults (taking the
pytest run down with it) and `test_map_composable_continuation` fails
(`2 != [2, 4]`). `make lint` is clean.

## Withdrawn designs

- A fresh context in `demand_sync_ex` presented as the fix for a
  Koskari-originated call. Violates the invariant.
- Evaluator-side class construction (running `type_call` in the
  evaluator). Calling a type is the Python boundary.
- Classifying results at the boundary (converting any async generator
  to a `DemandGenerator`). Mutates the presented type of an object the
  demand function merely returned.
- Keeping the boundary's yielding branch with its unwraps.
- Presenting a `DemandGenerator` at construction, by `KOS_FRAME_DEMAND`
  (extent-wide) or by a non-inherited `KOS_FRAME_DEMAND_CALL` /
  `KOS_FRAME_DEMAND_ROOT` bit (demand call only). Demand belongs to the
  drive; construction evaluates nothing.
- Moving the synchronous surface onto the providers (`tp_iternext`,
  `send` / `throw` / `close` on `AsyncGenerator` and the transforms) and
  retiring `DemandGenerator`. Would make every provider both a sync and
  an async iterator; `KosIterFunction` on a generator `__iter__` keeps
  the async types async.
- Forcing `KOS_FRAME_DEMAND` on a Koskari-side drive through a
  `KosPyIter`. Demand is the Python-side drive's; a Koskari drive
  carries its own.
- Unwrapping a `KosPyIter` to its provider at acquisition. Needs type
  tests in `kos_iter_acquire` and `yield_from_classify`; forwarding the
  suite needs none.
- A frame flag checked on the generic return path to discard a result.
  Replaced by a resume.

## Contract changes

Existing tests whose declared contract changes. Each is changed only
with explicit approval. All follow from step 4 (the boundary returns a
yielding callable's `AsyncGenerator` as is) or from D2's change already
in the working tree.

Applied: the D7 test (`test_fallback_yielding_function_returns_generator`).
Pending sign-off: the rest below.

- A `demand_call` / `demand(...)` result used directly as a sync
  iterator, without `iter()`:
  - `test_demand_sync.py::test_yield_produces_demand_generator`
    (`:118-131`): `next(gen)`.
  - `test_demand_sync.py::TestDemandGenerator::test_yielding_function_returns_generator`
    (`:176-202`): `hasattr(result, '__next__')`, `iter(result) is
    result`, `next(result)`.
  - `test_demand_sync.py` `:270-285` and `:310-323`: `next(gen)` expected
    to raise `KoskariSyncViolation`. The intent (a suspension under a
    synchronous Python drive raises) holds through `next(iter(gen))`.
  - `test_demand_sync.py::TestDemandCallFlattening::test_fallback_yielding_function_returns_generator`
    (`:1143-1171`, D7): applied.
- A yielding Koskari dunder other than `__iter__` called from Python now
  returns the `AsyncGenerator`:
  - `test_dunder_adapter.py:277` (`__add__`) and `:308` (`__eq__`):
    `hasattr(result, '__next__')`.
- Wording: docstrings and class names that say `DemandGenerator`
  (`test_demand_sync.py:120/176-178/207/907-917/1145`,
  `test_run_iter_adapters.py:74/130-138`,
  `test_asyncgen_protocol.py:203/258`, `test_eval.py:187`,
  `opcode/test_yield.py:268-269`,
  `reproducers/test_asyncgen_anext_await.py:104`,
  `reproducers/test_asyncgen_protocol.py:622`,
  `reproducers/test_demand_gen_await.py:19/100/110`) become `KosPyIter`.
  Their assertions hold.
- Holds unchanged: `test_run_iter_adapters.py:151` (`iter(agen) is not
  agen`); `test_dunder_adapter.py:240` (generator-function `__iter__`,
  via `KosIterFunction`); `test_repr_includes_code_name` (the
  `AsyncGenerator` repr `async_generator object yielding_fn` contains
  the asserted substring); `test_provider_drive_matrix.py`'s
  `"kos-demand-gen"` row and `TestKoskariIterDemandGenerator`, which
  keep their meaning (Koskari over a `KosPyIter`) and now run flattened.

## Corrective plan

Each step ends green on `make test` and `make lint`, except where noted.

### 1. D5: `KosPyIter`, flattened from Koskari without demand (done)

Rename, in `iterate.c` / `iterate.h` and the 15 `tp_iter` users
(`iter.c:573`, `transform/{accumulate,batched,chain,compress,cycle,dropwhile,filter,islice,map,pairwise,starmap,takewhile,zip,zip_longest}.c`,
`demand.c:343` until step 4 removes it): `KosIterDemandGenType` ->
`KosPyIterType`, `KosIterDemandGen` -> `KosPyIter`, `demandgen_*` ->
`pyiter_*`, `KosIterAdapt_DemandIter` -> `KosIterAdapt_PyIter`,
`tp_name` `koskari.lib.DemandGenerator` -> `koskari.lib.Iterator`. The
type has no Python binding (`iterate.c:1361-1362`) and no `.py`, `.ki`,
or `docs/` reference.

`KosPyIterType` becomes a `KosEvaluatedTypeObject` with
`.tp_base = KOS_EVALUATED_PYTYPE(KosEvaluatedType)` and a `kos_iter`
suite, modeled on `KosPyAsyncGenType` (`iter.c:559`); the forward
declaration (`iterate.c:900`), allocation (`:1334`), and `PyType_Ready`
(`:1362`) follow. Layout is compatible: `KosEvaluatedType`'s
`tp_basicsize` is `sizeof(PyObject)` (`evaluated.c:36-45`). Every
acquisition site (`PUSH_FOREACH`, `OBJECT_ITER`, `VAR_ITER`, `FAST_ITER`,
`kos_transform_init_one` `transform.c:92`, `yield_from_classify`) already
keys on `KosIter_Check`, so all of them flatten with no new test.

The suite forwards to the provider's suite and adds nothing:

- `ki_step` / `ki_throw`: a closed adapter (`provider` NULL) reports
  exhausted; otherwise the provider's `ki_step` / `ki_throw`. No
  `KOS_FRAME_DEMAND` forcing: the generator splice already sets or
  clears the bit from the Koskari invoker (`dyn_gen.c:286-288`,
  `:390-392`).
- `ki_close`: the provider's `ki_close`; `provider` stays set and the
  provider reports its own closed state.
- `ki_finish`: the provider's, with `exhausted && !
  provider_slots->ki_pyctx_recyclable`, since
  `kos_dyn_iter_request_finish_req` (`dyn_iter_node.c:54-58`) applies the
  recyclable rule against the adapter's suite.
- `ki_pyctx`: NULL.

The Python-side drive (`pyiter_step`, formerly `demandgen_step`) is
unchanged, including its `KOS_FRAME_DEMAND` force (`iterate.c:1075-1084`)
and `close`'s throw-through-drive mapping (`:1198-1227`). No state
hand-over between Python and Koskari drives: an agen provider freezes
into `GEN.active` on each yield whichever context drove it
(`iter.c:389-393`); a transform keeps its own `cached_frame`.

Correct `eval/eval_iter.c:37` ("Koskari AsyncGenerator passes through" ->
"KosIter provider"). Other stale prose naming `DemandGenerator`:
`iter.c:21`, `iterate.c:20-22`, `iterate.h:29-35`, `callable.h:257-260`,
`demand.c:23-38`, `:211`. Design rollups (`design/EVALUATOR.md:1907`,
`design/INVARIANTS-AND-CLARIFICATIONS.md:167`, `:216`,
`design/JARGON.md:93`, `:165`, `design/language/async.md:72-74`) are
corrected only on the maintainer's go-ahead.

### 2. `__iter__` bound to a generator function: `KosIterFunction` (done)

- `object.c`: `KosIterFunctionType` (`koskari.lib.IterFunction`), a
  `Demand` subtype laid out as `Procedure` is. `wrap_iter_function` in
  `demand_wrap_namespace`, run before the demand pass, wraps `__iter__`
  only when it is an exact `KosFunction` whose code yields
  (`KosCode_YIELDS`), decided once at class build. Any other `__iter__`
  stays a demand dunder and must return an iterator itself. The demand
  pass leaves an `IterFunction` as it is (`KosDemand_New` returns a
  `Demand` subtype unchanged).
- Python call path (`kc_pycall`, reached from `slot_tp_iter` through the
  bound method): `Koskari_AsyncCall` constructs the `AsyncGenerator`,
  then `KosIterAdapt_PyIter` wraps it. Calling a generator function
  evaluates nothing, so the demand core is not involved; demand applies
  when the `KosPyIter` is driven.
- Evaluator install: the wrapped function's own install through
  `KosCallable_Call`, which pushes the `AsyncGenerator` for the Koskari
  caller. No frame, no demand flag; `tail_call` passes through.
- `__reversed__` needs nothing: `reversed()` returns its result
  unchecked, and iterating it goes through `tp_iter`.
- Tests: `tests/reproducers/test_iter_function.py` with
  `scripts/iter_function.ki` (the wrapper type, `iter(obj)` presenting
  an `Iterator`, `(.__iter__ obj)` from Koskari returning the
  `AsyncGenerator`, Koskari `for-each` over the instance, a
  non-generator `__iter__` staying a `demand`);
  `test_dunder_adapter.py:240` holds.

### 3. D8: `KosPartial` routes every inner through the evaluator (done)

`kospartial_kos_vinstall` no longer splits on whether the inner wants a
frame. Without keywords on either side it calls
`KosEvalState_CallResult` with the merged positional vector; with
keywords it merges them and calls `KosEvalState_CallArgs`.
`KosEvalState_Call` handles a plain Python callable (`PyObject_Vectorcall`
+ `deliver_py_call_result`), so a coroutine result is awaited.
`kospartial_inner_is_flattenable` is removed; `KosPartial_Call` remains
for `tp_call` only.

Tests: `tests/reproducers/test_partial_flatten.py` with
`scripts/partial_flatten.ki`. `partial(demand(partial(f)))` from Koskari
sees the caller's `parameterize`, and a partial over a Python `async
def` returns the awaited value (both red before the fix); bound, call,
and combined keywords over Koskari and Python inners; a Python inner in
tail position.

### 4. D4: the boundary through `CallInto`

- Vector-native core `static int demand_sync_ex(PyObject *callable,
  PyObject *const *args, Py_ssize_t nargs, PyObject *kwnames,
  PyObject **out)`.
- Not a KosCallable: `PyObject_Vectorcall`, then
  `demand_step_python_result`. Unchanged semantics.
- A KosCallable: the boundary's one context and eval state;
  `CallInto(&es, callable, args, nargs, kwnames, &demand_resume_kind,
  NULL, KOS_FRAME_DEMAND)`; freeze on every path. `RAISE` / `FATAL`
  fail. `AWAIT` clears the parked await iterator
  (`KosEvalState_PythonAwaitClearIter`, as `eval_event_await` does at
  `eval.c:2482`), sets `KoskariSyncViolation`, and fails. `NORMAL` /
  `REPROLOGUE`: run `koskari_eval`; its RETURN goes to
  `demand_finish_koskari_value`; its AWAIT / YIELD keep the existing
  "missing deny guard" error. `CallInto` never reports RETURN: a value
  in hand is staged, frozen onto the code-less frame's stack, and
  delivered by the prologue (`eval/eval_state.c:119-131`,
  `eval.c:3446-3450` -> `eval_step_resume_delivery` `:3110-3125`). A
  yielding callable delivers its generator the same way;
  `demand_finish_koskari_value` (`demand.c:181-195`) passes a provider
  through (no `am_await`).
- `KosResumeFrame_Install` with no current frame gives depth 1
  (`kos_dynframe_new`, `dynchain/dyn_frame.c:34`; `eval.h:1110-1125`).
- Removed: the type refusal and its comment (`demand.c:319-328`), the
  `BoundMethod` unwrap (`:267-289`; `kos_vinstall_boundmethod` prepends
  self), the `Demand` unwraps (`:236-246`, `:291-303`), the yielding
  branch (`:330-352`), the `CallResult` / `CallArgs` split
  (`:370-382`), the hand-sorted outcomes (`:384-411`), the "no code"
  check (`:414-420`), the manual flag (`:425`).
- `demand_resume` / `demand_resume_kind` move above the demand core.
- Spread helper extracted from `KosCallable_CallArgsSlots`
  (`callable.c:118-166`), e.g. `KosArgSpread` with a small inline array,
  `KosArgSpread_Init(spread, args_tuple, kwds_dict)` and
  `KosArgSpread_Clear(spread)`; no allocation without keywords; entries
  borrowed from the caller's tuple and dict. `KosCallable_CallArgsSlots`
  uses it; `Koskari_DemandSync` / `Koskari_DemandSyncEx` keep their
  signatures and spread once through it.
- New public `Koskari_DemandSyncVector(callable, args, nargs, kwnames,
  out)` in `demand.h`; `KosCallAdapt_VectorDemand` passes its vector and
  `kwnames` through (zero-length `kwnames` as NULL).
- Callers verified: `utils.c:520/561` (the `r == 1` branch at `:565`
  stays dead), `matcher/datum.c:485`, `object.c:163`,
  `callable.c:295/319`, `demand.c:830/862`. Borrowed tuple and dict; NULL
  args and NULL kwargs both occur.
- Resolves D4 and D6. The `demand_call` contract changes land here.

### 5. Tests

- `test_frameless_result_is_none` passes unchanged. The contract changes
  above are applied as approved.
- `KosPyIter` from Koskari: `for-each` and `yield-from` over
  `(iter (gen))` run flattened (the `parameterize` observable below);
  from a non-demand async call, a `gen` that does `(sleep 0)` suspends
  normally through the adapter; under the demand path it raises
  `KoskariSyncViolation`, as the bare `(gen)` does.
- `KosPyIter` from Python: `next`, `send`, `throw`, `close`, `list`
  unchanged, including a suspension raising `KoskariSyncViolation`
  (catchable in the body, as `test_demand_gen_await.py` exercises) and
  "generator ignored GeneratorExit".
- `KosIterFunction`: covered by step 2's
  `tests/reproducers/test_iter_function.py`.
- `tests/lib/test_demand_sync.py` (phase_lib): `demand(partial(operator.add,
  5))(100) == 105`; `demand_call(partial(fn, 1), 2)`;
  `demand_apply(fn, (1,), {"b": 2})` and `demand(fn)(1, b=2)` for
  keywords at the boundary; the existing `BoundMethod(demand(fn), obj)()`
  case stays as the regression for the removed unwrap.
- Generators returned through the demand path keep their type: a
  yielding function called through `demand(...)` from Python, and a
  module-level generator returned through one, are `AsyncGenerator`.
- Flattening observable: a `parameterize` binding is visible to a
  flattened run and invisible to a nested one (`kos_param_lookup`,
  `domain.c:299-314`). Used for Koskari iterating a generator obtained
  from a demand call (`for-each` and `yield-from`), and for D8
  (`partial(demand(partial(f)))` from Koskari).
- Continuations through `CallInto`: a `Demand` and a `Procedure`
  wrapping a `call/cc` continuation, invoked from Koskari inside and
  outside a demand extent, including a second (multi-shot) entry.

### 6. Recursion guard and TODO

- Decide whether the `eval.c` guard stays. Remaining users are Python
  boundary re-entries. On 3.9-3.11 it consumes the Python recursion
  limit, on 3.12+ the C limit (which differs between 3.12 and 3.13); the
  `type_call` path is already counted by CPython, so the guard counts
  that path twice. The guard does not by itself make boundary recursion
  safe: a `type_call` -> `slot_tp_init` -> `procedure_tp_call` ->
  `demand_sync_ex` -> `koskari_eval` level is deeper than the 1 KB D1
  level, and the 3.12 / 3.13 C limits times that likely exceed an 8 MB
  stack (inferred; measure before deciding).
- Replace or remove the design TODO entry "Recursion guard at the Python
  boundary", whose `(super)` claim is inaccurate.

### 7. Gate

`make test`, `make lint`, `make test-native`, one ASan run for the C
arc.
