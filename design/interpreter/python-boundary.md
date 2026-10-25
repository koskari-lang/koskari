# Python-to-Koskari call boundary

Status: implemented, 2026-10-20 to 2026-10-22; each item carries its
Implemented note. Origin: a read of the sync (`Demand`) and
async (`Coroutine`) entry paths against what a CPython-to-CPython call
costs. Related: [demand-flattening.md](demand-flattening.md) (the
`CallInto` boundary, step 6 on C stack depth),
[../archive/eval-state-live-authority.md](../archive/eval-state-live-authority.md)
(why `KosEvalState` existed; removed in C).

## Problem

A Koskari-to-Koskari call installs a cached frame and threads on. A
Python-to-Koskari call pays, per call, work that CPython never pays for
a call into itself. The sync path, `fn(a, b)` on a `Demand` over a
simple-arity non-yielding `KosFunction` with no drive active:

| step | cost |
|---|---|
| snapshot onto the wrapper's cached context (`KosContext_TakeSnapshot`) | two thread-record lookups; outside `with Domain():` a `KosDomain` GC alloc, freed again at release |
| `koskari_eval` entry | recursion guard, `PyErr_GetExcInfo`, a third thread-record lookup, two stack reserves, a 160-byte memset, `Py_GetRecursionLimit`, thaw |
| code-less root frame (`KosResumeFrame_Install`, cache slot NULL) | dyn-node freelist pop, full init, freed at recycle |
| callee install (`KosFunction_Install`) | frame-cache hit; nothing to gain |
| return | callee pop with cache offer, staged delivery, `REPROLOGUE`, second trampoline pass, `demand_resume`, event return, terminal |
| `koskari_eval` exit | freeze, token drop, thread-record release, second `PyErr_GetExcInfo`, `DECREF ctx` |
| `KosContext_Recycle` | chain free of the root frame, a dozen field clears, domain and cells cleared |

Two trampoline passes, two dyn-node round trips, three or four
thread-record resolutions, two exception-state fetches, one GC object,
per call.

The async path, `await fn(a, b)` on a bare `KosFunction`:
`KosFunction_VectorCall` copies the template namespace (the frame cache
is never consulted), allocates a `KosCoroutine` and snapshots the
domain; the first `send` allocates a frame with no `FN` tag (never
offered back to the cache) and a `KosContext`, whose constructor builds
a throwaway eval state to derive two fields. Three or four GC objects
per call against CPython's one.

Two records hold the running state: `KosContext` on the heap and
`KosEvalState` on the C stack of `koskari_eval`. Every drive thaws one
into the other and freezes it back; a thread-local stack of eval states
exists so the GC and the context's Python getters can find the chain
the eval state took; three sites build a throwaway eval state to read
or edit the context. The "registers" rationale does not hold: handlers
reach both records through a pointer, so `es->pc` and `ctx->pc` are the
same load. The real optimization, pc and acting_pc flushed to the frame
only at frame boundaries, is about frame versus live record and
survives with the live fields on the context.

## Plan

Order: A, B, C, D, E, F. B is local and measurable at once; C is the
widest change and D is written against the record C leaves; E is
independent of C but shares B's frame-cache work; F widens the frame
cache to every call shape and is last because it is a Koskari call-path
change the boundary items only expose.

### A. Baseline

Before any edit, on the current build:

- `tox -qe churn-trace -- starmap` for the counters `call_frame_cached`,
  `call_simple_vector`, `live_rebuild`, and the freelist alloc/return
  counts; `perf stat -e instructions:u` on the same kernel.
- Two new kernels under `examples/churn/ki/`, Koskari-only, "deterministic
  baseline" like `starmap`: `demand_call` (Python `map` over a `Demand`
  of a two-argument function, no class, no bound method) and
  `coro_await` (a Python coroutine calling a two-argument Koskari
  function and awaiting each returned Coroutine in turn). `starmap`
  mixes the bound-method shape with the wrapper and
  cannot separate B from E.
- `method` and `loop` as the Koskari-to-Koskari control for C: C must
  not move their instruction counts.

Record the numbers in `tmp/perf-plan.md`.

### B. Sync boundary

#### B1. The callee is the root frame

`eval_phase_entry_call` takes the B1 arm for an exact `KosFunction`
whose `bind_self_state` is resolved and whose code does not yield.
Since f28c5d28 the argument shape does not matter: keywords, defaults
and the rest bind in `KosFunction_Install`, which the arm calls
directly with no frame below it; `call->frame_flags` is OR-ed onto the installed
frame, since `kos_dyn_push_frame_unchecked` inherits the demand bit
only from a current frame. The trampoline threads into the body; its
return reaches `eval_event_return_at_frame` case 4 (no parent) and
terminates the drive; a raise walks to no handler and terminates as
`RAISE`. `KosContext_Recycle` frees the chain and `kos_dynframe_free`
offers the callee frame to the function's cache, so the cache serves
boundary calls too.

Every other callable (a bound method, an `Expression`, a `partial`), a
generator function, and the first call of a function whose
`bind_self_state` is unresolved, keeps `CallInto` with the code-less
frame and the resume. `Procedure` needs nothing: its vectorcall already
discards the result, and its resume only mattered on the code-less
frame.

Removes from the common call: the native frame push and free, the
staged delivery, the second trampoline pass, `demand_resume`.

Counter: `KOS_TE_ENTRY_ROOT_INSTALL` beside the call-class counters.

Tests (`tests/lib/test_demand_sync.py`, `test_demand_cached_context.py`): the
return value, a raise, a function that names itself (bind-self store),
recursion through Python re-entry while the wrapper is in use, a
`call/cc` escape from the root callee, a function whose first call
resolves `bind_self_state`.

#### B2. The minted domain stays on the cached context

`demand_release_host` clears the domain, so outside an ambient domain
every call mints one. Instead the release clears only the domain's
`root_refs` (the root stores of the finished call, which die today
with the domain) and keeps the object; `KosContext_TakeSnapshot`
replaces it only when an active context or an ambient Python domain
supplies one. Parameters in force at each call are still seen
(`test_each_call_sees_the_parameters_in_force`).

The same minting happens at `iterate.c:86` and `transform.c:366` for
their cached contexts and at `coro.c:109` per coroutine; the first two
take the same keep-and-clear; the coroutine's snapshot is per call by
design (E).

Test: a `Demand` called twice outside any domain; a root
`parameterize` store inside the first call is visible in the second
(`test_root_store_persists_across_calls`).

Implemented as a per-thread default domain rather than keep-and-clear:
each thread owns one, `KosTlsState.default_domain`, minted on first
need by `tls_default_domain` under `kos_tls_domain(s)` and returned
when no context is active and no `with Domain():` is open. A context
takes its domain at each eval entry (`koskari_eval` reads
`kos_tls_domain(s)` into `KosContext_DOMAIN`) and drops it at the
exit; `KosContext_Snapshot` carries only the parameterize cells, so a
coroutine, a cached context or a continuation holds no domain between
evals and a context resumed on another thread runs under that
thread's root. The demand release clears only `param_refs`. Root
stores outlive the call on the Python surface as they do under the
launcher.
The default is released with the thread record (`tls_state_release`,
GIL held); for the main thread that is interpreter shutdown, when the
thread state's dict is cleared. The no-domain arms (`ParamRef`
methods, `parameter`, the ambient root resolve) are gone;
`Domain.current_domain()` never answers None. Measurement
(`demand_call`, 4 batches): instructions 6,704,898,910 -> 6,687,190,326,
cycles 2,714,314,544 -> 2,572,296,068, per call 3,253 -> 3,208
instructions, churn-trace counters identical. The kernel's Demand calls
run under an active context and never took the minting arm, so these
deltas are noise; no kernel makes context-free boundary calls.

#### B3. Root-frame cache for the general shape

`KosResumeFrame_Install` already takes a cache slot and
`kos_dynframe_cache_inert` already strips a code-less frame for reuse;
`demand_resume` passes NULL. A `cached_frame` slot on `KosDemandObject`
beside the cached context, handed through `KosEntryCall` to the
install, and the resume passes it to `KosNativeFrame_ReturnToCache`.
Serves the calls B1 does not take: a `Demand` over a bound method, an
`Expression`, a `partial` or a generator function, and every
function's first call.

Implemented: `KosDemandObject.cached_frame`, handed through
`KosEntryCall.frame_cache`; the general arm of `eval_phase_entry_call`
is one `KosContext_CallInto` call with the slot as the frame cache and
as the resume's state, and `demand_resume` returns the frame to the
slot. A continuation clone shares the slot; a reentrant call finds it
emptied and allocates. Counter `KOS_TE_ENTRY_ROOT_CACHED`. The
measurement below was taken before f28c5d28 moved keyword calls to the
B1 arm: `demand_call_kw` no longer takes this arm, so the figures
describe the earlier routing. Measurement
(new kernel `demand_call_kw`, `(partial (demand mixkw) b: 7)` over
`builtins.map`, 4 batches of 200,000 calls): instructions
8,656,596,634 -> 8,604,921,989, per call 5,630 -> 5,596; cycles per
call 1,620 -> 1,644, within noise. churn-trace (`demand_call
demand_call_kw`): `entry_root_cached` 1,199,940 (every call but the
first of each of the 60 wrappers), dyn_node freelist allocations
2,407,214 -> 1,207,274, the difference being `entry_root_cached`.
`demand_call` (B1 arm) unchanged: 3,232 -> 3,229 instructions per
call.

#### B4. `KosBoundMethod` vectorcall

A bound Koskari method held by Python (`m = obj.method`, a `Demand`
over one handed to a C callback) goes tuple, second tuple with self,
`PyObject_Call`, `PyVectorcall_Call` unpack, per call
(`boundmethod_pycall`). The type gains a `vectorcall` field and
`Py_TPFLAGS_HAVE_VECTORCALL`; the entry prepends self into a stack
buffer (heap above 8) and calls `PyObject_Vectorcall` on the function,
which reaches `KosCallAdapt_VectorAsync` or `demand_vectorcall`
directly. `tp_call` becomes `PyVectorcall_Call`; `boundmethod_pycall`
and its two tuple builds go.

Test: a held bound method and a `Demand` over one, called with
positionals and with keywords, from Python.

Implemented: `KosBoundMethod.vectorcall` set by `KosBoundMethod_New`
to `boundmethod_vectorcall`, also the type's `kc_pyvectorcall`;
`kc_pycall` is NULL, so `Koskari_AsyncCall` reaches it through
`PyObject_Call` and `PyVectorcall_Call`. With
`PY_VECTORCALL_ARGUMENTS_OFFSET` the entry writes self into the lent
`args[-1]` and puts the slot back after the call; otherwise it copies
into the stack buffer, or a heap buffer above 8. The entries are
borrowed. Measurement (new kernel `demand_call_method`, `builtins.map`
over the bound method of a class whose `mix [self a b]` is
`@demand`, 4 batches of 200,000 calls): instructions 7,228,751,595 ->
6,823,092,299, per call 3,850 -> 3,331; cycles per call 775 -> 597
(754 on a repeat, within noise). churn-trace counters identical
(`entry_root_install` 1,206,876, `call_frame_cached` 1,219,611,
`entry_root_cached` 0): the Demand below is entered with the same
vector as before.

#### Naming

The `host` identifiers in `demand.c`, `demand.h` and `iterate.c`
(`host`, `host_owned`, `demand_acquire_host`, `demand_release_host`,
`iterate_acquire_host`, `test_demand_host*.py`) named a cached context.
Renamed under B2 to `cached_ctx`, `demand_acquire_context`,
`demand_release_context`, `iterate_acquire_context` and
`test_demand_cached_context*.py`.

### C. One record: remove `KosEvalState`

`KosContext` is the record. `KosEvalState` is removed, not aliased and
not renamed into; nothing is renamed to it.

- The live fields move to the head of `KosContext`, grouped on one
  cache line: `dyn_top`, `frame`, `code`, `block`, `namespace`, `pc`,
  `acting_pc`, `goal`, `pending_value`, `breaker`, `recursion_limit`,
  `terminal`. `frame_top` is `frame`; the frozen `block` and
  `namespace` are the live ones.
- Handler signatures, the `KosThreadFn` type, the resume-kind and
  elector function types, and every helper that took `KosEvalState *`
  take `KosContext *`. The `KosEvalState_*` accessor macros and
  helpers are first retargeted to the context's fields under their
  names, so the bodies under `opcode/`, `dynchain/`, `eval/` and
  `matcher/` change only at their signatures, then folded into the
  `KosContext_*` family in a mechanical prefix sweep once the first
  pass has gone through the full gate.
- `koskari_eval` no longer carries the struct in its own frame: the
  C-stack copy is gone, and with it the thaw and freeze that kept the
  two records agreeing.
- Removed: `KosEvalState_Init` (a `begin drive` sets `breaker`,
  `recursion_limit`, `terminal` on the context), `ThawFromContext`,
  `FreezeToContext`, the "lazy and evil" block in
  `KosContext_NewFromSnapshot` (direct stores of the root frame's
  frame, block, namespace, code, pc and acting_pc), the throwaway
  states at `iter.c:115/160`, `iterate.c:109/134`,
  `coro.c:480/578/654`, the thread-local eval-state stack and
  `kos_tls_active_eval_for_ctx` (`context_traverse`, the context
  getters, and `eval.c:678` read the context), the steal-on-thaw
  comments. `KosContext_FlushLiveRegsToFrame` stays: it is the frame
  boundary flush of pc and acting_pc.
- `KOS_CTX_RUNNING` keeps its one job, refusing re-entry; the "chain is
  on the eval state while running" distinction has nothing left to
  express. `context_traverse` visits `pending_value` and the chain
  unconditionally.
- Gate: `make lint test`, `make test-native`, `make test-asan`,
  `make test-valgrind`; `method` and `loop` instruction counts within
  noise of A; `live_rebuild` falls by the walks the removed
  `KosContext_NewFromSnapshot` block ran. A hot-path change: the branch
  comparison against A is the evidence, or the item is withdrawn.

### D. Drive entry on the single record

#### D1. One thread-record resolution per boundary call

`kos_ctx_current`, `kos_domain_current` and `kos_tls_enter_drive` each
redo `PyThread_tss_get` + `PyThreadState_Get` + owner check.
`demand_run_koskari` resolves the record once; `KosContext_TakeSnapshot`
and an internal `koskari_eval` variant take it as a parameter, the
public `koskari_eval` resolving and delegating. Same for `coroutine_step`.

Implemented: `kos_tls_current()` resolves the record (allocating it,
fatal on failure), with the accessors `kos_tls_active_context(s)` and
`kos_tls_domain(s)`; `KosContext_Snapshot`, `KosContext_TakeSnapshot`
and `KosContext_New` take the record, and `kos_tls_enter_drive` takes
it instead of resolving. `koskari_eval(ctx, s, ...)` takes the record,
NULL to resolve it at entry; `demand_run_koskari` passes the record it
resolved for the snapshot, `coroutine_step` passes NULL: a coroutine's
snapshot is taken at creation, not in the step that evaluates it.
`KosContext_TakeSnapshot` has since gone with the wrapper's cached
context: a `Demand` call takes a context from the freelist through
`KosContext_New`.
Counter `KOS_TE_TLS_RESOLVE` (`tls_resolve`), one per `tls_state_current`
call. churn-trace `demand_call` (1,200,000 boundary calls under an
active context): `tls_resolve` 1,207,813 against `init` 1,207,232, one
per boundary call where the code path before made two (snapshot, eval
entry). Measurement (`demand_call`, 4
batches): instructions 6,738,058,735 -> 6,698,294,914 (repeat
6,676,609,183), per call 3,240 -> 3,185; cycles per call 717 -> 808,
within build-placement noise. Plain Python loop, 200,000 calls of a
`Demand` with no context active (three resolutions before, one after):
3,846 -> 3,783 instructions per call.

#### D2. Exception-state swap only when something is held

Both `KosExc_Fetch` calls in `koskari_eval` are unconditional. A
per-version inline in `compat_39.h` through `compat_314.h`,
`kos_tstate_exc_held(tstate)`, reads the thread state's handled
exception (`exc_info->exc_type`/`exc_value` on 3.9 and 3.10,
`exc_info->exc_value` on 3.11 and later); the fetch and the install
run only when the host or the context holds one.

Implemented: each `compat_3XX.h` carries `kos_tstate_exc_held`, a
reference-free walk mirroring that version's
`_PyErr_GetTopmostException`, then `KosExc_Fetch`'s emptiness test on
the item it stops at. Fields read: `PyThreadState.exc_info`, and on
`_PyErr_StackItem` `exc_type`, `exc_value`, `previous_item` (3.9,
3.10; skip on `exc_type` NULL or None) or `exc_value`,
`previous_item` (3.11, 3.12 skip on NULL or None; 3.13, 3.14 skip on
NULL). `kos_tls_thread_state(s)` returns the record's owner, the
current thread state for any resolved record. `koskari_eval`
peeks at entry and fetches the host's state only when held, peeks
again at cleanup and fetches only when the thread holds one; the four
exit cases keep their behaviour. Counter `KOS_TE_EXC_FETCH`
(`exc_fetch`), one per fetch. churn-trace `demand_call`: `exc_fetch`
0 over 1,207,232 drives, where the code path before made two per
drive. Measurement (`demand_call`, 4 batches): instructions
6,698,800,619 -> 6,542,233,283 (repeat 6,537,928,665), per call
3,179 -> 2,982; the one-batch control drops 193 per call; cycles per
call 677 -> 575 (repeat 539). Only the 3.12 header was compiled
locally.

### E. Async boundary

The unstarted coroutine is a bound call: a namespace filled from the
formals at the call, and nothing else built until it is driven. The
frame and the context exist only from first send to completion.

- The call binds as before: template copy, steal bind for the simple
  shape, the formals machine otherwise. Every binding error stays at
  the call.
- The record holds `code`, the bound namespace, the snapshot, and the
  function (owned) when a function made the call; the `Expression`
  form holds none. No frame.
- The first send builds the root frame over `code` and the namespace,
  tags it `FN` with the function, and builds the context over it from
  the snapshot. The context comes from a `KosContext` freelist in
  `eval_context.c`, the shape `ns_alloc` has. The record lets go of
  code, namespace, snapshot and function once the frame and context
  have them.
- The Koskari splice (`KosCoroutine_TakeFresh`) is unchanged: the
  awaiter installs the code and namespace as a callee; the function
  reference is dropped with the rest.
- Completion marks the record done and releases its context then, so
  a gather holding finished coroutines holds no contexts. Clearing the
  context frees the chain: the `FN`-tagged root frame is offered to the
  function's cache, else to the dyn-node freelist.
- The domain snapshot at call time stays: a coroutine runs under the
  parameters in force when it was made.

Rejected: building the frame at the call (from the function's cache
when it held one) and holding it on the record. Under a gather every
pending coroutine then holds a frame at once, which overran the
dyn-node freelist and the function's cache: instructions per coroutine
rose 2.6% (`coro_call`) and 2.2% (`async_tree`).

Test (`tests/lib/test_coroutine_record.py`, `test_coroutine_first_send.py`):
repeated awaits from Python of simple, keyword and default shapes, a
coroutine closed before its first send then the function called again,
unstarted coroutines dropped and collected, the Koskari splice
repeatedly, defers and coroutines gathered, the Python surface
unchanged; `test_context_token.py` unchanged.

Implemented: `KosCoroutine` gains `fn`; `KosCoroutine_NewFromFunctionCall(fn,
ns)` (ns stolen) serves the three function callers in func.c and
`KosCoroutine_NewFromCode` the `Expression` form. `coroutine_materialize`
tags the root frame `FN` and sets `KOS_FRAME_CORO_ROOT`, a frame flag
read only by the trace count. `coroutine_settle` and `coroutine_close`
mark the coroutine done through `coroutine_finish`, which clears the
context. `KosContext` freelist cap 256, drained by `koskari_eval_clear`.
Counter `KOS_TE_CORO_FRAME_TO_CACHE`: a coroutine root accepted by its
function's cache. Measurement (kernels `coro_call`, an asyncio-hosted
Python driver gathering `mix(i, n - i)` over 2,000 calls, 20,000
coroutines per batch; `async_tree`, 54,610 per batch; `coro_await`,
the same calls awaited one at a time, 20,000 per batch): instructions
per coroutine `coro_call` 42,297 -> 41,757, `async_tree` 53,932 ->
53,904; cycles 12,563 -> 13,873 (repeat 12,571) and 20,076 -> 19,307.
`coro_await` has no before figure: 5,607 instructions and 1,471 cycles
per coroutine. churn-trace (`coro_call async_tree`, 6 batches each):
context freelist 381,154 hits and 66,864 misses, all GC allocations
before; dyn_node misses 187,079 -> 74,774; namespace allocation
unchanged. The remaining misses are `async_tree`'s: its 1,365
interior coroutines are suspended at once, each with a context and a
root frame, against caps of 256 (context) and 128 (dyn node); context
overruns 66,600 = 60 runs x (1,366 - 256), consistent with those
contexts plus one running leaf. `coro_await` alone: context 120,349 hits, 9 misses.
`coro_frame_to_cache` is 8 per function (16, and 24 with
`coro_await`): the first send builds its frame with `kos_dynframe_new`
and never takes from the function's cache, so the cache fills once and
refuses every later root, which goes to the dyn-node freelist.

### F. The frame cache serves every call shape

A cached frame's namespace is reset to the template at cache time
(`KosNamespace_ResetToTemplate`: raw slots NULL, template refs kept),
the state `KosNamespace_Copy(BASE_NS)` produces for every call shape.
Only the consumers are simple-only: the three arms
(`kos_call_cached_frame_install`, the tail-in-place arm, the cache arm
in `KosFunction_Install`) steal-bind positionals by index, which needs
`nargs + 1 == simple_arity1`; the refusal in `KosFunction_CacheFrame`
for `simple_arity1 == 0` only keeps a function no consumer can serve
from holding dead frames. A keyword call, a short supply that consults
a default, star, keyword-only and by-ref shapes, and `Procedure` over
an `__init__` with keyword arguments all copy the template and
allocate a frame per call (B3's measurement: one dyn node per keyword
call on the callee side, plus the namespace copy).

- The general arm in `KosFunction_Install` and the opcode call path
  (`KosFunction_PrepareCallNamespaceFromVector` sites) pop a cached
  frame when one is present and run `KosFormals_VectorApplyFast` and
  the self bind on its namespace instead of copying the template, then
  push it ready and tag it `FN`.
- The general binder can fail midway (bad keyword, missing argument)
  with partial stores in the namespace; the steal bind cannot. On a
  bind failure the namespace is reset to the template before the frame
  goes back to the cache, or the frame is freed.
- `KosFunction_CacheFrame` drops the `simple_arity1 == 0` refusal:
  every shape has a consumer. The cap and the privacy checks stay.
- The yields gate stays ahead of every arm; a generator never takes a
  frame.
- Counter: `KOS_TE_CALL_FRAME_CACHED` counts the general arm's hits
  too, or a sibling counter splits them (decide at implementation by
  what the kernels need to show).

Kernels: `demand_call_kw` (callee side of the keyword boundary call)
and a Koskari-to-Koskari keyword call kernel. Expected: the dyn node
allocation per keyword call goes, and the namespace copy with it.

Test: keyword, default-consulting, star and keyword-only calls repeated
through one function give the right results; a call with a bad keyword
raises and the next good call succeeds; a bound method with keywords;
`Procedure` over an `__init__` with keyword arguments; the frame cache
tests in `tests/lib/` unchanged.

Implemented: the general arm of `KosFunction_Install` (not simple, not
yielding, not a tail call) takes the function's cached frame when one
is there; the opcode call path reaches it through that install. The
frame is unlinked before the bind, not after as in the steal-bind
arms: the binder runs user code for a by-ref keyword formal with a
floor (`KosRef_IsEmpty` on a dynamic ref runs its emptiness fence),
and a call of the same function from there took a frame still at the
cache head, bound into it, and reset it at its release, so the outer
call read an emptied slot. A failed bind offers the frame back
through `KosFunction_CacheFrame` (reset to the template, relinked
under the cap as the cache is then) or frees it when refused. The
tail-call path keeps the template copy. `KosFunction_CacheFrame`
drops the `simple_arity1 == 0` refusal. Sibling counter
`KOS_TE_CALL_FRAME_CACHED_GENERAL`. The native test
`test_cache_frame_takes_every_shape` replaces the refusal test.
Measurement (kernels `call_kw`, a Koskari loop
calling `mixkw [a b: 0]` with `b:` by keyword, 200,000 calls per
batch; `demand_call_kw`): instructions per call `call_kw` 3,498 ->
3,094 (repeat 3,087), `demand_call_kw` 5,370 -> 4,943 (repeat 4,948);
cycles per call 757 -> 596 (repeat 629) and 1,606 -> 1,264 (repeat
1,309). churn-trace (both kernels, 1,200,000 keyword calls each):
`call_frame_cached_general` 2,400,141, every keyword call to `mixkw`
but the first plus 143 from discovery and the harness;
`call_frame_cached` 19,829 unchanged; dyn_node allocations
2,407,054 + 313 -> 6,971 + 255 and namespace allocations
2,400,083 + 846 -> 0 + 788, each down by `call_frame_cached_general`.

## Out of scope

- C stack depth per re-entry level (demand-flattening.md step 6, part 2)
  waits on the resumable-reads row split for the CALL family.
- The keyword path through `KosArgSpread` and `demand_call`'s tuple
  slice; rare at the boundary.
- `Procedure` semantics and the class builder's dunder wrapping.
