# Python-to-Koskari call boundary

How a call from Python enters the evaluator and what it costs, for the
sync (`Demand`) and async (`Coroutine`) paths, against a
CPython-to-CPython call. Related: [demand-flattening.md](demand-flattening.md)
(the `CallInto` boundary, step 6 on C stack depth).

## The two paths

A Koskari-to-Koskari call installs a ready frame and threads on (F). A
Python-to-Koskari call adds the entry into `koskari_eval` and the
record that carries the drive.

The sync path, `fn(a, b)` on a `Demand` over a simple-arity
non-yielding `KosFunction`:

| step | what runs |
|---|---|
| context | one from the `KosContext` freelist (`KosContext_New`), the thread record resolved once (D1) |
| `koskari_eval` entry | the running-flag refusal, the recursion guard, the thread's domain taken onto the context, the exception-state swap only when something is held (D2) |
| callee | the function's ready frame pushed as the root frame (B1) |
| return | `eval_event_return_at_frame` case 4, terminal; the root frame's release offers it to the function's cache |
| `koskari_eval` exit | flags, thread-record release, domain dropped, exception state restored only when held |
| `KosContext_Recycle` | chain free, field clears |

The async path, `await fn(a, b)` on a bare `KosFunction`, is E: a
ready frame from the function's cache, a `KosCoroutine` revived from
the function's parked one, a context from the freelist at the first
send. One GC allocation per call at steady state, as CPython's own
coroutine.

Kernels under `examples/churn/`: `demand_call` (Python `map` over a
`Demand` of a two-argument function), `demand_call_kw` (the keyword
shape through `partial`), `demand_call_method` (`map` over the bound
method of a `@demand` method), `coro_call` (coroutines gathered),
`coro_await` (coroutines awaited one at a time), `async_tree`; `method`
and `loop` are the Koskari-to-Koskari controls.

## B. Sync boundary

### B1. The callee is the root frame

`eval_phase_entry_call` installs an exact `KosFunction` whose code does
not yield directly
through `KosFunction_Install`, with no frame below it, whatever the
argument shape; `call->frame_flags` is OR-ed onto the installed frame,
since a push inherits the demand bit only from a current frame. The
trampoline threads into the body; its return reaches
`eval_event_return_at_frame` case 4 (no parent) and terminates the
drive; a raise walks to no handler and terminates as `RAISE`.
`KosContext_Recycle` frees the chain and `kos_dynframe_free` offers the
callee frame to the function's cache, so the cache serves boundary
calls too.

Every other callable (a bound method, an `Expression`, a `partial`) and
a generator function takes `KosContext_CallInto` with a code-less root
frame and a resume. `Procedure` needs nothing more: its
vectorcall discards the result.

Counter: `KOS_TE_ENTRY_ROOT_INSTALL`.

Tests: `tests/lib/test_demand_sync.py`,
`test_demand_cached_context.py`: the return value, a raise, a function
that names itself, recursion through Python re-entry while the wrapper
is in use, a `call/cc` escape from the root callee, a function whose
first call runs as the root frame like every later one.

### B2. The thread's default domain

Each thread owns one default domain, `KosTlsState.default_domain`,
minted on first need by `tls_default_domain` under `kos_tls_domain(s)`
and returned when no context is active and no `with Domain():` is open.
A context takes its domain at each eval entry (`koskari_eval` reads
`kos_tls_domain(s)` into `KosContext_DOMAIN`) and drops it at the exit,
so a coroutine, a cached context or a continuation holds no domain
between evals, and a context resumed on another thread runs under that
thread's root. `KosContext_Snapshot` carries only the parameterize
cells. Root stores outlive the call on the Python surface as they do
under the launcher. The default is released with the thread record
(`tls_state_release`, GIL held); for the main thread that is
interpreter shutdown. `Domain.current_domain()` never answers None.

Test: `test_root_store_persists_across_calls`,
`test_each_call_sees_the_parameters_in_force`.

### B3. The wrapper's root-frame slot

For the calls B1 does not take, `KosDemandObject.cached_frame` holds
one inert code-less root frame, handed through `KosEntryCall.frame_cache`
to the install (`KosContext_CallInto` with the slot as the frame cache
and as the resume's state); `demand_resume` returns the frame to the
slot through `KosNativeFrame_ReturnToCache`. A continuation clone shares
the slot; a reentrant call finds it emptied and allocates.

Counter: `KOS_TE_ENTRY_ROOT_CACHED`.

### B4. `KosBoundMethod` vectorcall

`KosBoundMethod.vectorcall` is `boundmethod_vectorcall`, set by
`KosBoundMethod_New`, also the type's `kc_pyvectorcall`; `tp_call` is
`PyVectorcall_Call` and `kc_pycall` is NULL, so `Koskari_AsyncCall`
reaches it through `PyObject_Call`. With
`PY_VECTORCALL_ARGUMENTS_OFFSET` the entry writes self into the lent
`args[-1]` and puts the slot back after the call; otherwise it copies
self and the arguments into a stack buffer, or a heap buffer above 8
entries. Every entry is borrowed, as in CPython's bound method.

Test: a held bound method and a `Demand` over one, called with
positionals and with keywords, from Python.

## C. One record

`KosContext` is the one record of a drive; there is no separate eval
state. The live fields sit at the head of the context, grouped on one
cache line: `dyn_top`, `frame`, `block`, `namespace`, `pc`,
`acting_pc`, `goal`, `pending_value`, `breaker`, `recursion_limit`,
`terminal`. Handler signatures, the `KosThreadFn` type, the resume-kind
and elector function types, and every helper take `KosContext *`.
`koskari_eval` carries no copy of the record on its own C frame.
`KosContext_FlushLiveRegsToFrame` is the frame-boundary flush of pc and
acting_pc. `KOS_CTX_RUNNING` has one job, refusing re-entry.
`context_traverse` visits `pending_value` and the chain
unconditionally.

## D. Drive entry

### D1. One thread-record resolution per boundary call

`kos_tls_current()` resolves the thread record (allocating it, fatal on
failure), with the accessors `kos_tls_active_context(s)` and
`kos_tls_domain(s)`. `KosContext_Snapshot` and `KosContext_New` take
the record, and `kos_tls_enter_drive` takes it instead of resolving.
`koskari_eval(ctx, s, ...)` takes the record, NULL to resolve it at
entry; `demand_run_koskari` passes the record it resolved,
`coroutine_step` passes NULL: a coroutine's snapshot is taken at
creation, not in the step that evaluates it.

Counter: `KOS_TE_TLS_RESOLVE` (`tls_resolve`), one per
`tls_state_current` call: one per boundary call under an active
context.

### D2. Exception-state swap only when something is held

Each `compat_3XX.h` carries `kos_tstate_exc_held(tstate)`, a
reference-free walk mirroring that version's
`_PyErr_GetTopmostException` (`exc_info->exc_type` and `exc_value` on
3.9 and 3.10, `exc_value` on 3.11 and later), then `KosExc_Fetch`'s
emptiness test on the item it stops at. `kos_tls_thread_state(s)`
returns the record's owner, the current thread state for any resolved
record. `koskari_eval` peeks at entry and fetches the host's state only
when held, peeks again at cleanup and fetches only when the thread
holds one; the four exit cases keep their behaviour.

Counter: `KOS_TE_EXC_FETCH` (`exc_fetch`), one per fetch; zero over a
`demand_call` run.

### E. Async boundary

A Python call of a `KosFunction` makes a `KosCoroutine` over the
function's ready frame (F): `KosFunction_VectorCall` and
`KosFunction_Call` take the frame from the ready-frame core and hand it
to `KosCoroutine_NewFromFrame`; yielding code goes to
`KosAsyncGen_NewFromFrame` instead. The `Expression` form
(`KosCoroutine_NewFromCode`) builds a plain frame over its namespace.

- The record holds the frame, the parameterize snapshot taken at the
  call (skipped while no cell table exists anywhere,
  `kos_param_cells_possible`), and the function, owned for the record's
  whole life. Every binding error stays at the call.
- The first send (`coroutine_materialize`) wraps the frame in a context
  from the `KosContext` freelist (`eval_context.c`, cap 256, drained by
  `koskari_eval_clear`), starting from the snapshot, and the record lets
  go of both.
- The Koskari splice (`KosCoroutine_TakeFresh`) takes the frame itself:
  the awaiter checks frame room and pushes it ready; the snapshot is
  dropped, the grafted frame runs under the awaiter's dynamic state.
- Completion (`coroutine_settle`, `coroutine_close`, through
  `coroutine_finish`) marks the record done and releases its context
  then, so a gather holding finished coroutines holds no contexts.
  Clearing the context frees the chain and the root frame's tag offers
  it to the function's cache.
- The record's own dealloc parks the cleared, untracked object on the
  function's `dead_coro` slot when that slot is free, and the function's
  next Python call revives it as a freelist entry; the function's
  release frees a parked one.
- The return a Python driver takes through `tp_iternext` raises a
  StopIteration built directly on the type's allocation
  (`kos_stop_iteration_new`), not through the type call.

Counters: `KOS_TE_CORO_FRAME_TO_CACHE` (a coroutine root accepted by its
function's cache; `KOS_FRAME_CORO_ROOT` on the frame marks one under
tracing), the freelist trace entry `coroutine`.

Tests: `tests/lib/test_coroutine_record.py`,
`test_coroutine_first_send.py`, `test_context_token.py`.

### F. The ready frame: one bound frame for every call

Every call of a `KosFunction`, from the opcodes, from Python, or for a
generator, gets its frame from the ready-frame core in `func.h`. The
core takes the function's cache head (`kos_function_frame_take`), else
a fresh frame over a template copy (`kos_function_frame_fresh`, out of
line); binds the arguments into its namespace by their shape; stores
the function into its own slot when the body names it; tags the frame
with the function (`kos_function_frame_tag`, taking a reference or
moving the caller's owned one); and on a bind that fails midway hands
the frame back through the cache offer (`kos_function_frame_return`),
which resets the namespace, so no caller sees a half-bound frame. The
frame is ready for `kos_dyn_push_ready_frame`: parent NULL, depth 1, pc
at the code's entry, stack empty. The core knows nothing of contexts,
chains or coroutines.

One entry per argument source, so the bind folds at the site:

- `KosFunction_ReadyFrameBlock`: the top `nargs` references of an
  operand segment, stolen, with an optional receiver for slot 0. The
  simple-call class only (plain positional formals at exact arity,
  non-yielding code, classified by the caller);
  inline, the evaluator's hottest call path. On failure nothing is
  stolen.
- `KosFunction_ReadyFrameVector`: a borrowed vector with `kwnames`. The
  simple class binds by slot inline after taking a reference to each
  argument; any other shape binds through the formals machine in
  `kos_function_ready_frame_general`, out of line. Both store the
  function into its own slot when the body names it, and both take
  `self_owned` as the block entry does, so a caller's owned reference
  to the function moves into the tag.
- `KosFunction_ReadyFrameTuple`: a tuple and a dict, through the
  formals machine.

Consumers do their last step only:

- The call core pushes: `kos_call_simple_install` in the CALL_FAST
  handlers (hit or miss), the window arm of `kos_call_invoke_impl`,
  `kos_call_invoke_self_steal` for an out-of-band receiver. A
  generator function at its simple arity with its arguments in the
  window takes the block entry too, and its ready frame goes under a
  new GEN with the call completing in hand.
- `KosFunction_InstallCall` (the `kc_vinstall` slot `KosFunction_Install`
  with the callable borrowed) binds from the vector and pushes, or in
  tail position swaps the ready frame for the live frame
  (`kos_dyn_swap_frame_for_tail`), whose release offers it to its own
  function's cache; the call core's tail arm steal-binds from the
  window at the install arity before the same swap. The self-tail at
  simple arity rebinds the live namespace in place: it reuses the
  running frame rather than acquiring one, and is the one arm outside
  the core.
- The Python entries build the coroutine (E).
- Yielding code from either boundary goes under a GEN through
  `KosAsyncGen_NewFromFrame`, its tag kept, so a generator's frame
  returns to the cache at exhaustion.

A cached frame's namespace is reset to the template at cache time
(`KosNamespace_ResetToTemplate`: raw slots NULL, template refs kept),
the state `KosNamespace_Copy(BASE_NS)` produces, so every shape binds
into it. `KosFunction_CacheFrame` keeps up to `KOS_FRAME_CACHE_MAX`
frames per function; the frame leaves the cache before the bind, since
the formals machine can run user code (a by-ref keyword formal's floor
test) and a call of the same function from there must not take the
frame being bound.

Counters: `KOS_TE_CALL_FRAME_CACHED` (a hit bound by slot),
`KOS_TE_CALL_FRAME_CACHED_GENERAL` (a hit bound by the formals
machine), `KOS_TE_CALL_SIMPLE_STEAL` and `KOS_TE_CALL_SIMPLE_VECTOR`
(misses by slot).

Tests: the frame cache tests in `tests/lib/`, the native
`test_cache_frame_takes_every_shape` and
`test_general_call_binds_into_cached_frame`.

## Out of scope

- C stack depth per re-entry level (demand-flattening.md step 6, part 2)
  waits on the resumable-reads row split for the CALL family.
- The keyword path through `KosArgSpread` and `demand_call`'s tuple
  slice; rare at the boundary.
- `Procedure` semantics and the class builder's dunder wrapping.
