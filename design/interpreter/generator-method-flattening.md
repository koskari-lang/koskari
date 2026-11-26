# Flattening generator methods and the Python iterator

`(next it)` steps a Koskari generator on the caller's own evaluator.
`(it.send v)`, `(it.throw e)`, `(it.close)` and `(it.__next__)` on the
iterator `iter()` returns over the same generator, and `send`, `throw`,
`close` and `__next__` on a step awaitable, reach a C method written for
a Python caller. That method takes a context of its own and runs
`koskari_eval` on it, under the Koskari caller's C frame. The flattening
invariant (`koskari/lib/CLAUDE.md`, [../FLATTENING.md](../FLATTENING.md))
names that a bug. This document records how each path steps a generator
today, what a program sees go wrong on the paths that do not flatten,
and a proposal that makes those methods callables with an install, so a
Koskari call of one makes the request `(next it)` makes.

The synchronous run behind those methods is intended. Koskari iterables
present immediate demand iteration through the standard iterator
methods so plain CPython code can use them; CPython code may use async
iteration instead. Both are boundary calls and both honour the demand
and async rules. The demand run is a convenience for a Python caller
and stays as it is. A Koskari caller always flattens, and no demand is
injected for it: the generator runs under the caller's own demand
state. The defect is a Koskari caller reaching the Python entry.

Nothing here is implemented. Every statement about behaviour comes from
reading the code; nothing was run. The statements that rest on reading
alone are listed under "Not run".

**See also:** [../FLATTENING.md](../FLATTENING.md) (the rollup this
changes), [dynchain-unification.md](dynchain-unification.md) (sections
2, 4.4 and 4.9, and rule D16), [yield-from-close.md](yield-from-close.md)
(the close of a delegate, which needs the same close request),
[generator-return-value.md](generator-return-value.md),
[resumable-reads.md](resumable-reads.md) (section 7, the UNPACK and
star-call rows over the same iterator),
[../EVALUATOR.md](../EVALUATOR.md) (sections 7.2 and 8.2).

## 1. Current implementation

### 1.1 The objects

| Object | Type, file | Python surface | Evaluator surface |
| --- | --- | --- | --- |
| A generator | `koskari.lib.AsyncGenerator`, `KosPyAsyncGen`, `iter.c` | `__anext__`, `asend`, `athrow`, `aclose`, each returning a step awaitable; `__iter__` returning the Python iterator | iter suite: `agen_iter_step`, `agen_iter_throw`, `agen_iter_close`, `agen_iter_finish`, `agen_iter_pyctx`, `agen_iter_busy` |
| A step awaitable | `koskari.lib.async_generator_asend`, `KosIterStepper`, `iterate.c` | `send`, `throw`, `close`, `__next__` (`tp_iternext`), the send slot | await suite: `stepper_await_kind` |
| The Python iterator | `koskari.lib.Iterator`, `KosPyIter`, `iterate.c` | `send`, `throw`, `close`, `__next__` (`tp_iternext`) | iter suite forwarding to its provider: `pyiter_ki_step`, `pyiter_ki_throw`, `pyiter_ki_close`, `pyiter_ki_finish`, `pyiter_ki_busy` |

A generator has no `send`, `throw`, `close` or `__next__` of its own.
The TODO entry's "generator's stepper" is the step awaitable.

Every transform (`map`, `filter`, `zip`, the `koskari.itertools` set)
uses the same two adapters: its `tp_iter` is `KosIterAdapt_PyIter` and
its `__anext__` is `KosIterAdapt_Anext`. What follows for a generator
holds for a transform, with the transform's callback in place of the
generator's body.

### 1.2 Paths that step on the caller's evaluator

- **`(next it)`, `(next it default)`.** `OBJECT_NEXT`, `VAR_NEXT` and
  `FAST_NEXT` (`opcode/OBJECT_NEXT.c`) pop the iterator and call
  `KosContext_NextResume` (`eval.c`) with `kos_next_resume_kind` or
  `kos_next_default_resume_kind` (`eval/eval_iter.c`) and the iterator
  as the resume's owned state. `kos_iter_request_bind_next`
  (`evaluated.h`) classifies the iterator; one with an iter suite is a
  provider. `kos_iter_fetch` (`eval/eval_iter.c`) steps it through
  `kos_iter_step_provider` (`eval.c`), which calls `ki_step` with the
  running context. For a `KosPyIter` that is `pyiter_ki_step`, which
  forwards to the generator's `agen_iter_step`. `agen_iter_drive`
  (`iter.c`) marks the generator busy and attaches its span to the
  running chain (`kos_dyn_gen_install`, or
  `kos_dyn_gen_install_bootstrap` for the first step,
  `dynchain/dyn_gen.c`). The install takes `KOS_FRAME_DEMAND` from the
  frame below the attach point (`kos_dyn_gen_inherit_demand`), so the
  generator runs under the consumer's demand state.
  `kos_iter_route_status` (`eval.c`) sets the resume on the calling
  frame. The generator's yield freezes the span and
  `iter_deliver_to_resume` hands the element to that resume, which
  pushes it and threads on. The end reaches the resume as NULL with
  StopIteration pending, carrying the generator's return value.
- **`for-each`, `yield-from`.** The `ITER_NEXT` family
  (`KOS_ITER_STEP`) and `opcode/YIELD_FROM.c` step a provider the same
  way, with their own resumes.
- **`(g.__anext__)`, `(g.asend v)`, `(g.athrow e)`, `(g.aclose)`.** The
  call returns a step awaitable, and a call result is awaited where it
  arrives. The awaitable's type carries the await suite, so the await
  is a code-less frame whose resume is `stepper_await_resume`
  (`iterate.c`). For an awaitable not yet started it moves the provider
  out of the awaitable, binds a request to it, and calls
  `KosContext_NextResumeBound` or `KosContext_ThrowResumeBound` with
  `kos_astep_resume_kind` or `kos_aclose_resume_kind`
  (`eval/eval_iter.c`). The generator's span attaches above that
  code-less frame, and the kind returns the element from it
  (`RESULT_FRAME_RETURN`), raises StopAsyncIteration at the end, or
  returns None for a close.

### 1.3 Paths that run a second evaluator

**The Python iterator's methods.** `pyiter_iternext`, `pyiter_send`,
`pyiter_throw` and `pyiter_close` (`iterate.c`) go through
`pyiter_step`, which refuses a re-entrant step (`running`, ValueError
"generator already executing"), to `pyiter_step_run`:

1. The context is the provider's own when its suite has `ki_pyctx`. For
   a generator that is `agen_iter_pyctx`: a context created on the
   first step from Python and kept for the generator's life. Otherwise
   the adapter creates one (`iterate_acquire_context`).
2. `iterate_suite_step` asks `ki_busy`, then calls `ki_step` against
   that context between `KosContext_EvalEnter` and `KosContext_EvalExit`.
   The generator's span is now on that context.
3. `KOS_FRAME_DEMAND` is forced onto the installed frame.
4. `koskari_eval(gen->ctx, ...)` runs it. A send or throw into a
   generator goes in through the eval's send and throw arguments.
5. A yield is the method's return value. A return raises StopIteration
   with the return value and releases the adapter
   (`pyiter_finish_exhausted`). An await under the demand bit is turned
   into `KoskariSyncViolation` inside the eval (`eval_event_await`,
   `eval.c`).

`pyiter_close` throws GeneratorExit through the same step. A yield in
answer releases the adapter and raises RuntimeError "generator ignored
GeneratorExit". The release (`pyiter_release_state`) calls the
provider's `ki_close`, which for a generator is `KosPyAsyncGen_Teardown`
(`iter.c`): a further GeneratorExit close through `kos_close_drive`
(`eval/eval_iter.c`), which is one more `koskari_eval`.

**The step awaitable's methods.** `stepper_send`, `stepper_throw`,
`stepper_iternext` and the send slot (`iterate.c`) go through
`stepper_iternext` to `stepper_drive`, which takes a context by the same
rule and calls `koskari_eval(st->ctx, ...)`. No demand bit is forced.
An await inside the step leaves the eval as `KOS_RES_AWAIT`; the method
returns what the awaitable yielded and marks itself `awaiting`, and the
next send resumes that awaitable (`stepper_resume_await`) before it
re-enters the eval. A yield of the generator completes the step by
raising StopIteration carrying the element (`stepper_finish_yield`).
`stepper_close` closes a step left mid-await through its context
(`stepper_abandon_drive`, `kos_close_drive`), and otherwise calls the
provider's `ki_close`, the teardown above.

### 1.4 How a Koskari call reaches those methods

`(it.send v)` is a `CALL_ATTR`. `resolve_named_attr_slow`
(`eval/eval_cache.c`) finds `send` on the type and returns it unbound,
with the receiver as the first argument, because the descriptor's type
has `Py_TPFLAGS_METHOD_DESCRIPTOR` (`is_plain_instance_method`).
CPython's method descriptor and slot wrapper types both have it.
`kos_call_dispatch` (`eval/eval_call.c`) runs `kc_vinstall` for a
callee whose type has a call suite (`KosCallable_CheckSlots`,
`callable.h`) and calls anything else with `PyObject_Vectorcall`. A
CPython method descriptor has no call suite, so the C method runs as it
does for a Python caller. `(var f it.send)` binds a CPython builtin
method, and `(f v)` reaches the same C function.

A step awaitable reaches Koskari code un-awaited only as a value that is
not a call result: an argument passed from Python, an item of a
container, an attribute. `tests/reproducers/scripts/generator_self_close.ki`
does this with `box[0]`.

The behaviour is the same on master.

## 2. The problem

### 2.1 The Python iterator's `send`, `throw`, `close`, `__next__`

The defect class is one program giving two results depending on how it
names the same step: `(next it)` and `(it.__next__)` are the same
request on the same object.

1. **An await in the generator raises `KoskariSyncViolation` under an
   async caller.** `pyiter_step_run` forces `KOS_FRAME_DEMAND`, which
   is the intended run for a Python caller, and a Koskari caller
   reaches the same function. A body that awaits, a `dyn-wind` before or after segment
   that awaits, a `dyn-with` whose enter or exit hands back an
   awaitable, and a transform callback that awaits, all raise
   "async suspension is not allowed in demand_sync" at the await.
   Through `(next it)` the same generator suspends the caller and
   resumes (`tests/reproducers/test_pyiter_flatten.py`,
   `test_pyiter_suspends_off_demand_path`;
   `tests/reproducers/test_wind_across_yield.py`,
   `test_re_entry_under_an_async_consumer_suspends`). The Python-called
   half is tested (`test_pyiter_from_python_unchanged`,
   `test_re_entry_under_a_python_step`,
   `test_wind_out_under_a_python_step`); the Koskari-called half follows
   from the code and has no test.
2. **`close` cuts a cleanup that awaits.** `(it.close)` throws
   GeneratorExit under the forced demand bit, so a `finally` that awaits
   raises `KoskariSyncViolation` out of `close`, and the rest of that
   cleanup does not run. `(g.aclose)` on the same generator completes
   it.
3. **The generator reads parameters from its own context.** The
   context `agen_iter_pyctx` creates starts from the parameterize cells
   in force at the first step from a Python method
   (`KosContext_Snapshot`, `eval/eval_context.c`) and is kept. A later
   `(it.send v)` under a different `parameterize` runs the body against
   the first step's cells. `(next it)` runs it on the caller's chain,
   under the caller's cells (`test_pyiter_sees_parameterize`).
4. **Each call is one more evaluator on the C stack.** `koskari_eval`
   counts it against the eval nesting limit (`kos_eval_check_nesting`,
   `eval.h`; 200 under Emscripten) and against CPython's C recursion
   count (`kos_recursion_enter`). Generators that step each other
   through these methods raise RecursionError at a depth where the same
   chain through `(next it)` is bounded by the frame depth limit alone.
5. **The re-entry error depends on the outer step.** A body that calls
   `(it.send v)` on its own iterator gets ValueError "generator already
   executing" when the outer step is a Python method (`running` is set
   on the adapter), and RuntimeError "async generator already being
   iterated" when the outer step is a flattened `(next it)` (the
   adapter's flag is clear, and `iterate_suite_step` meets the
   provider's `ki_busy`).

### 2.2 The step awaitable's `send`, `throw`, `close`, `__next__`

No wrong result is known against the Python coroutine protocol for
`send`, `throw` and `__next__`: the method returns what an inner
awaitable yields and raises StopIteration with the element when the
step completes, which is what a Python caller stepping the awaitable
sees. Items 3 and 4 above apply unchanged, since the step runs on the
generator's own context under a nested `koskari_eval`.

One wrong result follows from the code for `close`: on an awaitable not
yet started, `stepper_close` calls `ki_close`, the synchronous
teardown. `kos_close_drive` cannot honour an await in the cleanup; it
frees the chain and reports RuntimeError "async generator ignored
GeneratorExit" as unraisable. `(st.close)` returns None with the
cleanup cut short, where `(g.aclose)` completes it.

The path contradicts the model in
[dynchain-unification.md](dynchain-unification.md) section 2, whatever
its results. A step awaitable handled by Koskari is a call, and an await
inside it is a release of the calling thread. Stepped by hand under a
second evaluator, the generator's await comes back to the Koskari
caller as the return value of `send`: a transfer at an await.

### 2.3 Other entries that run a second evaluator under a Koskari caller

These are the same defect class, and the rule that a Koskari caller
always flattens covers them. Section 3.9 says what each takes.

| Entry | Reached from Koskari by | Wrong result |
| --- | --- | --- |
| `coroutine_send`, `coroutine_throw`, `coroutine_iternext`, the send slot, `coroutine_demand` (`coro.c`, through `coroutine_step`) | a method call on a `Coroutine` held as a value. A Koskari call of a Koskari function makes no `Coroutine`; one exists when Python made the call | none known against the coroutine protocol; item 4 applies |
| `demand_call`, `demand_apply` (`demand.c`, through `demand_run_koskari`), bound as `demand-call` and `demand-apply` in `koskari/bootstrap/__init__.py` | an ordinary call | none known for the value: demand refuses suspension on either route. Item 4 applies. A `shift` in the callee to a `reset` in the caller crosses a context boundary here and does not through `(demand f)`; not traced |
| `kos_close_drive` from `ki_close` (`KosPyAsyncGen_Teardown`) | the `yield-from` failure path closing a Koskari delegate (`opcode/YIELD_FROM.c`); a transform's close of its sources | a delegate's cleanup that awaits is cut and reported as unraisable; recorded in [yield-from-close.md](yield-from-close.md) |
| the same teardown from a finalizer or a dealloc | no Koskari frame is calling | none: no caller exists to flatten onto |
| UNPACK and star-call over a generator | `PySequence_Fast` / `PySequence_Tuple` through `tp_iter` and `tp_iternext` | item 1; recorded in [../FLATTENING.md](../FLATTENING.md) and [resumable-reads.md](resumable-reads.md) section 7 |

## 3. Proposal

### 3.1 Shape

The methods become callables with a call suite. A Koskari call of one
runs its install, which puts a code-less frame on the calling chain and
makes a next or throw request against the receiver from that frame's
resume, as `stepper_await_resume` does for an awaited step. A Python
call of one runs the existing C function, unchanged.

No opcode, no compiler change, no branch in `kos_call_dispatch`, no
field on `KosContext`, `KosDynFrame`, `KosIterSlots` or
`KosEvaluatedTypeObject`. The mechanism is the call suite's own: the
dispatch already runs an install for any callee that has one.

A code-less frame is the right carrier because the result of a call
belongs to the call site: an operand push, a tail position, a native
caller's resume (`(map it.send xs)`). A resume set on the calling
frame, the `OBJECT_NEXT` shape, pushes onto that frame's stack and
serves none of the others. The code-less frame returns its value
through `kos_return_deliver` to whichever of them made the call.

### 3.2 A method type with an install (`iterate.c`)

One new static type, working name `KosNativeMethod`, modelled on the
Demand family as a descriptor (`demand.c`, `demand_descr_get`) and on
`KosApplyType` (`utils.c`) as a native callable with both an install
and a Python worker.

- Instance: `vectorcall`, `name` (an interned str), `install` (a
  `KosVectorInstallFunc`), `pycall` (a `vectorcallfunc`).
- `tp_flags`: `Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_VECTORCALL |
  Py_TPFLAGS_METHOD_DESCRIPTOR`. `tp_call = PyVectorcall_Call`.
  `tp_base` is the evaluated marker type.
- `kos_call.kc_vinstall`: reads `install` from the instance and
  tail-calls it with the same arguments. `kos_call.kc_pyvectorcall` and
  the instance's `vectorcall`: `pycall`.
- `tp_descr_get`: the instance itself for a class read,
  `KosBoundMethod_New(self, obj)` otherwise.
- `__name__` member, `__qualname__` getter, a repr.

The method-descriptor flag is what makes `CALL_ATTR` hand the instance
to the dispatch unbound with the receiver first, with no bound object
allocated; the type is static, so `kos_type_is_immutable` holds and the
call site caches it, as it cached the CPython descriptor.

The instances are created in `koskari_iterate_init` and placed in the
type dicts (section 3.6).

### 3.3 The Python iterator's installs (`iterate.c`)

Four instances on `KosPyIterType`: `send`, `throw`, `close`,
`__next__`. Each `pycall` unpacks the vector and calls `pyiter_send`,
`pyiter_throw`, `pyiter_close` or `pyiter_iternext`. `tp_iternext` stays
`pyiter_iternext`, so Python's `next` and `for` do not change.

Each `install`:

1. Checks the receiver's type and the argument count; TypeError
   otherwise, worded as CPython words a descriptor misuse.
2. Answers in hand when the adapter has no provider, as the Python
   method does: StopIteration for `send` and `__next__`, the thrown
   exception for `throw`, None for `close`. Refuses with ValueError
   "generator already executing" when `running` is set: a Python
   method's step of this adapter is on the C stack (question 1).
3. Normalizes a throw's arguments with `KosExc_NormalizeThrowArgs`, as
   `pyiter_throw` does.
4. Installs a code-less frame (`KosContext_InstallNativeFrame`,
   `eval/eval_call.c`) carrying a request kind and the adapter as its
   owned state, stages what the call carries
   (`KosContext_StageDelivery`: the sent value, None for `__next__` and
   `close`, the exception instance for `throw`), and answers
   `EVAL_REPROLOGUE`. This is the `KosContext_CallInto` outcome the
   install contract lists (`evaluated.h`). A tail call reuses the
   caller's frame inside `KosContext_InstallNativeFrame`.

The pushed code-less frame carries the caller's demand bit: the frame
push copies it (`kos_dyn_push_frame`, `dynchain/dyn_frame.c`), and the
generator install then takes it from that frame. Under an async caller
an await in the generator suspends the caller; under a demand caller it
raises `KoskariSyncViolation`. Both match `(next it)`.

Three request resumes run on that frame, each modelled on
`stepper_await_resume`:

- **step** (`send`, `__next__`): `kos_iter_request_bind_next(&req,
  adapter)`, `req.send = value`, `KosContext_NextResumeBound` with the
  sync step kind (section 3.4) and the adapter as state.
- **throw**: `KosContext_ThrowResumeBound` with the same kind. When the
  answer is `KOS_RES_RESUME` with StopIteration pending, the provider
  was already finished and took nothing; the pending end is replaced by
  the exception thrown, as `pyiter_step_run` raises it and as
  `stepper_await_resume` special-cases the same answer for `athrow`.
- **close**: a new GeneratorExit instance, `KosContext_ThrowResumeBound`
  with the sync close kind.

Each ends as `stepper_await_resume` ends: release the adapter when the
request raised before it deferred, `RESULT_REPROLOGUE` for a
REPROLOGUE answer, `RESULT_ENTRY` otherwise. The request kinds' clear
and traverse are a `Py_DECREF` and a `Py_VISIT` of the adapter; they are
not the next kinds' clear, since no step is in flight while the request
has not been made.

The request goes to the adapter, not to its provider, so the adapter's
suite forwards it and `pyiter_ki_finish` applies the provider's own
recycle rule, as for `(next it)`.

### 3.4 Two step kinds (`eval/eval_iter.c`, declared in `eval.h`)

Beside `kos_astep_resume_kind` and `kos_aclose_resume_kind`, sharing
`kos_next_state_clear`, `kos_resume_clone_object` and
`kos_next_state_traverse`:

- **sync step kind.** The body of `astep_resume` with the sync
  protocol's end: finish the provider, release the state, return an
  element from the frame (`RESULT_FRAME_RETURN`); at the end raise the
  pending StopIteration as it stands, so a return value it carries
  reaches the caller; a pending StopAsyncIteration becomes
  StopIteration; any other failure propagates.
- **sync close kind.** The body of `aclose_resume` with the message
  "generator ignored GeneratorExit". The shared body becomes one
  `KOS_OP_INLINE` function taking the message, called by both.

### 3.5 The step awaitable's installs (`iterate.c`)

Four instances on `KosIterStepperType`: `send`, `throw`, `close`,
`__next__`, with `pycall` over `stepper_send`, `stepper_throw`,
`stepper_close`, `stepper_iternext`. `tp_iternext` and the send slot
stay as they are for Python and for asyncio.

Each `install` reads the awaitable's state the way
`stepper_await_resume` reads it:

| State | Install |
| --- | --- |
| `running` | ValueError "generator already executing", as the Python methods raise |
| spent (no provider) | the reuse error (`stepper_set_reuse_error`); `close` returns None |
| not started | takes the step: the priming rules of `stepper_send` and `stepper_throw` are applied to the fields (a non-None send replaces an `asend` value and is refused by an `athrow` or `aclose`; a throw replaces what was primed), the provider moves to the resume's state, the awaitable is spent, and the request is made from a code-less frame as in section 3.3 |
| started by another owner (`started` or `awaiting`) | question 4 |

The kinds render the completed step in the coroutine protocol, which is
what `stepper_finish_yield` and `stepper_finish_exhausted` render:

- **protocol step kind** (`send`, `throw`, `__next__` of an `anext`,
  `asend` or `athrow` awaitable): an element raises StopIteration
  carrying it (`kos_raise_stop_instance`); the end raises
  StopAsyncIteration with the payload rule of `astep_resume`; an
  `athrow` that found the provider finished raises StopIteration.
- **protocol close-step kind** (the same methods on an `aclose`
  awaitable): the end or the GeneratorExit coming back raises
  StopIteration; an element raises RuntimeError "async generator
  ignored GeneratorExit".
- **`close`** on an awaitable not started: the sync close request of
  section 3.3 against the provider, returning None. The cleanup runs on
  the caller's chain, so an await in it suspends the caller.

Under this design a Koskari `send` on a step awaitable never returns an
inner awaitable's yield. The await releases the calling thread, and
`send` completes when the step does. Under a demand caller the await
raises `KoskariSyncViolation`. A Koskari program that steps awaitables
by hand to schedule them itself stops working; question 3 asks whether
that is the intent.

### 3.6 Placing the instances in the type dicts

`send`, `throw` and `close` leave `pyiter_methods` and
`stepper_methods`. Two ways to place the instances:

- **Before `PyType_Ready`.** Set `tp_dict` to a dict holding the four
  instances. CPython documents `tp_dict` as accepting initial
  attributes before the type is readied, and its slot-wrapper pass
  skips a name already present, so `__next__` is the instance while
  `tp_iternext` stays the C function.
- **After `PyType_Ready`.** `PyDict_SetItem` on `tp_dict`, then
  `PyType_Modified`. `set_type_constant` (`arena.c`) is the precedent
  for a plain name; `__next__` names a slot, and CPython's
  documentation excludes slot names from additions after the type is
  readied.

The method type is readied first either way. Question 6.

### 3.7 `KosBoundMethod` (`func.c`)

`KosBoundMethod_New` accepts the new type as a third test after
`KosFunction_CheckExact` and `KosDemand_Check`. `(var f it.send)` then
binds a `KosBoundMethod`, whose install (`kos_vinstall_boundmethod`)
injects the receiver through `KosContext_CallMethod` and reaches the
same install, and whose Python vectorcall reaches `pycall`.
`boundmethod_repr` falls back to the function's `__name__` when
`boundmethod_resolve_function` finds no `KosFunction`.

### 3.8 What stays untouched

- `kos_call_dispatch`, every CALL handler, `resolve_named_attr_slow`,
  the method cache.
- `OBJECT_NEXT` and its twins, `ITER_NEXT`, `YIELD_FROM`,
  `kos_iter_fetch`, `KosContext_NextResumeBound`,
  `KosContext_ThrowResumeBound`, `kos_iter_route_status`.
- `iter.c`: the generator's suite, its busy mark, its teardown.
- `dynchain/`: the generator install, freeze and winding.
- `pyiter_step`, `pyiter_step_run`, `stepper_drive`, `stepper_iternext`
  and every Python-called method body. A call from Python is the
  Python boundary and keeps its own entry.
- `stepper_await_resume` and the two awaited-step kinds.
- The bytecode and the `.kio` ABI.

### 3.9 The other entries

A Koskari caller always flattens, so these join the work. The same
method type serves each. They follow the Python iterator and the step
awaitable, each as its own change.

- **`Coroutine.send`, `throw`, `__next__`, `demand`** (`coro.c`). A
  fresh coroutine is taken with `KosCoroutine_TakeFresh`, as
  `coroutine_await_resume` takes it. `send`, `throw` and `__next__`
  install a code-less frame whose resume raises StopIteration carrying
  the frame's return value, and the coroutine's frame runs above it.
  `demand` installs one marked `KOS_FRAME_DEMAND` whose resume returns
  the value, the shape of `demand_kos_vinstall`. A started coroutine is
  the same case as a started step awaitable (question 4). `close` on a
  fresh coroutine evaluates nothing today (`coroutine_drop_record`).
- **`demand-call`, `demand-apply`** (`demand.c`). Each becomes an
  instance with an install, as `apply` is (`kos_vinstall_apply`,
  `utils.c`). The install is `demand_kos_vinstall` with the first
  argument as the wrapped callable; `demand-apply` spreads its tuple
  and dict through `KosContext_CallArgs` as `kos_apply_install_core`
  does. The Python worker keeps `Koskari_DemandSync`. The flattened
  call still runs under demand: here the caller asked for it, and no
  second evaluator is needed to honour it.
- **A delegate's close from `yield-from`.** The close request of
  section 3.3 is the piece [yield-from-close.md](yield-from-close.md)
  asks for in its KosIter kind. That document owns the routing of
  GeneratorExit through the throw kind.
- **A finalizer's or a dealloc's teardown.** Stays. No Koskari frame is
  calling.

### 3.10 Hot-path cost

By code reading:

- A call whose callee is a `KosFunction`, a Demand, a continuation or a
  plain Python callable executes the instructions it executes today.
  The new type is one more callee behind `KosCallable_CheckSlots`,
  which every call already evaluates.
- `CALL_ATTR` resolution and its cache are not edited.
- `OBJECT_NEXT`, `for-each` and `yield-from` are not edited.
- `KosBoundMethod_New` gains one compare, reached only after the two
  existing tests fail; a `KosFunction` binding returns at the first.

By measurement, with the baseline taken before any edit:

- `tox -qe perf -- -- --log <file>` over the churn kernels, comparing
  instruction counts per kernel before and after. The expectation is a
  flat count on every kernel, since no kernel calls these methods.
- `callgrind` on a kernel that binds methods in a loop, to show the
  `KosBoundMethod_New` change at zero instructions on the
  `KosFunction` path.

The call itself: a Koskari `(it.send v)` costs one code-less frame and
one pass of the prologue for the staged request, where it cost a
context acquire and a `koskari_eval` entry and exit. No kernel measures
it today; one is listed under Tests.

### 3.11 Tests

All in `tests/reproducers/`, beside
`test_pyiter_flatten.py` and `scripts/pyiter_flatten.ki`, in-process,
with the Koskari caller written in the script and the assertions in
Python.

The Python iterator, Koskari caller:

1. `send` delivers the value as the yield's value; the send that ends
   the generator raises StopIteration.
2. `send` at a yield inside a `dyn-wind` runs the before segment, then
   delivers.
3. A before segment that awaits at the re-entry a `send` makes suspends
   an async caller and the value is delivered afterwards. This is the
   test that fails today with `KoskariSyncViolation`.
4. The same under `demand_call` raises `KoskariSyncViolation`.
5. `throw` is raised at the yield, after the before segment; a throw
   into a finished generator raises the exception thrown.
6. `close` runs a `finally`, returns None, and returns None again on a
   closed iterator; a `finally` that awaits completes under an async
   caller.
7. `(var f it.send)` then `(f v)` gives the result of test 3.
8. `(it.__next__)` under `parameterize` reads the caller's value on
   every step, the first included, matching
   `test_pyiter_sees_parameterize`.
9. A body that sends to its own iterator is refused, with the error
   question 1 selects, under a Koskari outer step and under a Python
   one.
10. `(it.__next__)` over `(iter (map f xs))` with an `f` that awaits
    suspends an async caller.
11. A generator ending with `(return v)`: the error question 5 selects.

The step awaitable, Koskari caller, the awaitable passed in from Python:

12. `send` on an `__anext__` awaitable not started raises StopIteration
    carrying the element; at the end, StopAsyncIteration.
13. The same over a body that awaits suspends an async caller, and
    `send` then raises StopIteration with the element.
14. `close` on one over a generator whose `finally` awaits completes
    the cleanup and returns None.
15. A spent awaitable raises the reuse error; a running one, ValueError.

From Python, unchanged, already held by
`tests/reproducers/test_asyncgen_protocol.py` (`TestDemandGenParity`),
`test_pyiter_from_python_unchanged`, `test_generator_self_close.py` and
`TestWindAcrossYieldSyncSteps` / `TestWindAcrossYieldDemand` in
`test_wind_across_yield.py`. Add one test that `type(it).send`,
`it.send.__self__`, `it.send.__name__` and `repr(it.send)` answer.

Gates: `make test`; `make test-native`, `make test-asan`,
`make test-valgrind` (C change); `make test-pyversions` (type dict
contents across 3.9 to 3.14).

A perf kernel that steps a generator through `(it.send v)` in a loop
gives the call a counter.

### 3.12 Alternatives

- **A test for these C functions in `kos_call_dispatch`.** One compare
  on every plain Python call. Rejected: hot path.
- **A compile-time rewrite of `(X.send v)` to an instruction.** The
  receiver's type is not known at compile time, a bound `f` escapes it,
  and it is a compiler trick over a runtime dispatch that already
  exists. Rejected.
- **A resume on the calling frame, as `OBJECT_NEXT` sets.** Serves only
  a call whose result is pushed on that frame (section 3.1). Rejected.
- **One type per method, or one type with a method enumeration and a
  switch in its install.** Same behaviour. The instance carrying its
  two function pointers needs neither the extra types nor the switch,
  and serves the entries of section 3.9 without change.
- **Have the install make the request itself, with no staged pass.**
  The request can answer `KOS_RES_RESUME` (an end in hand), which the
  install contract does not list. Not proposed; question 7.

### 3.13 Relation to the adapter-only draft

An earlier draft covered the Python iterator alone. Kept: the
method-descriptor callable with an install, the code-less frame with a
staged request modelled on `stepper_await_resume`, the two sync kinds
beside the awaited-step kinds, the `KosBoundMethod_New` acceptance, the
Python worker calling the existing functions. Changed: the type carries
its install and worker per instance in place of a method enumeration,
so the step awaitable and the entries of section 3.9 use it; the step
awaitable's methods are in the proposal; placing the instances before
`PyType_Ready` is offered beside the write after it; the up-front busy
test is dropped from the install in favour of the provider's own
refusal at the request (question 1).

## Open questions

1. **Which error does a `send`, `throw`, `close` or `__next__` from the
   generator's own body raise on the flattened route?** The provider's
   refusal at the request is RuntimeError "async generator already
   being iterated", what `(next it)` raises there today. The Python
   methods raise ValueError "generator already executing" when their
   own step is on the C stack. Recommendation: the provider's
   RuntimeError on every flattened route, so the four methods and
   `(next it)` answer alike and the install needs no busy test of its
   own; a Python outer step keeps ValueError through `running`, checked
   first in the install.
2. **What does a Koskari `close` do when the generator yields under
   GeneratorExit?** Recommendation: what `aclose_resume` does: finish
   the provider as exhausted and raise RuntimeError "generator ignored
   GeneratorExit". The Python method's release runs `ki_close`, a
   second GeneratorExit close under `kos_close_drive`, which is the
   nested evaluator this work removes.
3. **Is a Koskari `send` on a step awaitable meant to complete the
   step, never returning an inner awaitable's yield?** That is the
   consequence of taking the step as a call with an await as a thread
   release (section 3.5). Recommendation: yes. The alternative keeps a
   value transfer at an await for Koskari callers.
4. **What does a Koskari `send`, `throw` or `close` do to a step
   awaitable, or a coroutine, that a Python caller started and left
   suspended on its own context?** Rule D16 in
   [dynchain-unification.md](dynchain-unification.md) keeps that chain
   on its context with its owner and refuses a Koskari `await` of it
   with the reuse error. Recommendation: the same refusal for the
   methods, on the same ground: the step belongs to whoever started it.
   The alternative is the existing Python method body, a second
   evaluator on the owner's context.
5. **Does `(return v)` reach the Koskari caller of `send` as
   `StopIteration(v)`?** An end that arrives through the chain carries
   the value (`iter_deliver_to_resume`), and the sync step kind as
   proposed raises it as it stands. An end in hand carries none:
   `kos_iter_fetch` drops the payload of `KOS_ITER_EXHAUSTED`, where
   `pyiter_finish_exhausted` raises it. A generator has no payload in
   hand, so the two routes agree for generators; a transform whose
   callback raised `StopIteration(v)` differs. Recommendation: raise
   what arrives, and leave the in-hand payload to
   [generator-return-value.md](generator-return-value.md).
6. **Are the instances placed before `PyType_Ready` or after?**
   Recommendation: before, since `__next__` names a slot.
   `make test-pyversions` shows it either way.
7. **May an install make its request without the staged pass through
   the prologue?** It would save one pass per call. It needs the
   install contract to admit a RESUME answer, or the request's answers
   mapped onto the listed ones. Recommendation: no, until a kernel
   shows the pass matters.
8. **The name of the method type.** `KosNativeMethod` is a working
   name.

## Not run

Nothing was run. These statements rest on reading alone:

- Section 2.1 item 1, Koskari-called half: an await reached through
  `(it.send v)`, `(it.throw e)`, `(it.close)` or `(it.__next__)` raises
  `KoskariSyncViolation` under an async Koskari caller.
- Section 2.1 item 2: `(it.close)` over a cleanup that awaits.
- Section 2.1 item 3: the stale parameter cells. Read from
  `agen_iter_pyctx` and `KosContext_Snapshot`; the PARAM extent's own
  behaviour on that context was not traced.
- Section 2.1 item 4: RecursionError at depth through these methods.
- Section 2.1 item 5: the two re-entry errors.
- Section 2.2: `(st.close)` on an awaitable not started cutting a
  cleanup that awaits.
- Section 2.3: every "none known" entry is the absence of a finding,
  not a test. The `shift` across `demand-call` was not traced.
- Section 1.4: that a bound `(var f it.send)` reaches the C function
  through CPython's builtin method; the non-call attribute read was
  not traced through the evaluator.
- Section 3.3: that the code-less frame push used by
  `KosContext_InstallNativeFrame` (`kos_dyn_push_frame_native`) copies
  the demand bit as `kos_dyn_push_frame` does, and that a tail call's
  reset keeps it (`kos_dynframe_reset_for_tail`).
- Section 3.6: CPython's handling of a `tp_dict` set before
  `PyType_Ready`, and of a slot name written after it, on 3.9 through
  3.14. Stated from CPython's documentation, not from its source at
  each version.
- Section 3.5: that every state of `KosIterStepper` reachable from
  Koskari is one of the four rows.

## Status

Investigate. `design/TODO.md` carries the stub.

# The end.
