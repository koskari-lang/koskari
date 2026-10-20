# Python-to-Koskari call boundary

Status: proposed, 2026-10-20. Origin: a read of the sync (`Demand`) and
async (`Coroutine`) entry paths against what a CPython-to-CPython call
costs. Related: [demand-flattening.md](demand-flattening.md) (the
`CallInto` boundary, step 6 on C stack depth),
[../archive/eval-state-live-authority.md](../archive/eval-state-live-authority.md)
(why `KosEvalState` exists).

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

Order: A, B, C, D, E. B is local and measurable at once; C is the
widest change and D is written against the record C leaves; E is
independent of C but shares B's frame-cache work.

### A. Baseline

Before any edit, on the current build:

- `tox -qe churn-trace -- starmap` for the counters `call_frame_cached`,
  `call_simple_vector`, `live_rebuild`, and the freelist alloc/return
  counts; `perf stat -e instructions:u` on the same kernel.
- Two new kernels under `examples/churn/ki/`, Koskari-only, "deterministic
  baseline" like `starmap`: `demand_call` (Python `map` over a `Demand`
  of a two-argument function, no class, no bound method) and
  `await_call` (an asyncio host awaiting a bare Koskari function in a
  loop). `starmap` mixes the bound-method shape with the wrapper and
  cannot separate B from E.
- `method` and `loop` as the Koskari-to-Koskari control for C: C must
  not move their instruction counts.

Record the numbers in `tmp/perf-plan.md`.

### B. Sync boundary

#### B1. The callee is the root frame

`eval_phase_entry_call` classifies the recorded call the way the
opcode arm `kos_call_cached_frame_install` does: exact `KosFunction`,
`nargs + 1 == simple_arity1`, no `kwnames`, `bind_self_state != 0`,
non-yielding code. That shape takes `KosFunction_Install` directly with
no frame below it; `call->frame_flags` is OR-ed onto the installed
frame, since `kos_dyn_push_frame_unchecked` inherits the demand bit
only from a current frame. The trampoline threads into the body; its
return reaches `eval_event_return_at_frame` case 4 (no parent) and
terminates the drive; a raise walks to no handler and terminates as
`RAISE`. `KosContext_Recycle` frees the chain and `kos_dynframe_free`
offers the callee frame to the function's cache, so the cache serves
boundary calls too.

Every other shape, and the first call of a function whose
`bind_self_state` is unresolved, keeps `CallInto` with the code-less
frame and the resume. `Procedure` needs nothing: its vectorcall already
discards the result, and its resume only mattered on the code-less
frame.

Removes from the common call: the native frame push and free, the
staged delivery, the second trampoline pass, `demand_resume`.

Counter: `KOS_TE_ENTRY_ROOT_INSTALL` beside the call-class counters.

Tests (`tests/lib/test_demand_sync.py`, `test_demand_host.py`): the
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
supplies one. Observable behaviour is unchanged: root stores do not
outlive the call, parameters in force at each call are still seen
(`test_each_call_sees_the_parameters_in_force`).

The same minting happens at `iterate.c:86` and `transform.c:366` for
their cached contexts and at `coro.c:109` per coroutine; the first two
take the same keep-and-clear; the coroutine's snapshot is per call by
design (E).

Test: a `Demand` called twice outside any domain; a root
`parameterize` store inside the first call is not visible in the
second.

#### B3. Root-frame cache for the general shape

`KosResumeFrame_Install` already takes a cache slot and
`kos_dynframe_cache_inert` already strips a code-less frame for reuse;
`demand_resume` passes NULL. A `cached_frame` slot on `KosDemandObject`
beside the cached context, handed through `KosEntryCall` to the
install, and the resume passes it to `KosNativeFrame_ReturnToCache`.
Serves the shapes B1 does not take: keywords, defaults, bound methods
through a `Demand`, `Procedure` over an `__init__` with defaults.

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

#### Naming

The `host` identifiers in `demand.c`, `demand.h` and `iterate.c`
(`host`, `host_owned`, `demand_acquire_host`, `demand_release_host`,
`iterate_acquire_host`, `test_demand_host*.py`) name a cached context.
B1 to B3 touch every one of those sites; the rename to `cached_ctx` /
`demand_acquire_context` / `demand_release_context` /
`iterate_acquire_context` is part of those items.

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
  take `KosContext *`. The `KosEvalState_*` accessor macros are
  retargeted to the context's fields and kept under their names in this
  pass, so the bodies under `opcode/`, `dynchain/`, `eval/` and
  `matcher/` change only at their signatures; folding those names into
  the `KosContext_*` family is a mechanical sweep once C is proven.
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
  comments. `KosEvalState_FlushLiveRegsToFrame` stays: it is the frame
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

#### D2. Exception-state swap only when something is held

Both `KosExc_Fetch` calls in `koskari_eval` are unconditional. A
per-version inline in `compat_39.h` through `compat_314.h`,
`kos_tstate_exc_held(tstate)`, reads the thread state's handled
exception (`exc_info->exc_type`/`exc_value` on 3.9 and 3.10,
`exc_info->exc_value` on 3.11 and later); the fetch and the install
run only when the host or the context holds one.

### E. Async boundary

- `KosFunction_VectorCall` on the simple non-yielding shape pops a
  cached frame, binds the vector into it (`KosFormals_SimpleStealApply`
  after incref, as `KosFunction_Install`'s cache arm does), stores the
  function self-bind, tags the frame `FN`, and the coroutine holds the
  frame instead of `code` + `ns`. `coroutine_materialize` pushes that
  frame as the root instead of allocating one. The frame goes back to
  the function's cache when the context is cleared.
- `KosContext` gains a freelist in `eval_context.c`, the shape
  `ns_alloc` has, serving coroutines, `demand_call`, and every
  `KosContext_New(NULL)` site; the constructor fills the three live
  fields from the root frame directly (C).
- The domain snapshot at call time stays: a coroutine runs under the
  parameters in force when it was made.
- `KosCoroutine_TakeFresh` (the Koskari-side splice of an unstarted
  coroutine) takes the frame's code and namespace off the held frame
  and returns the frame to the cache.

Test (`tests/lib/test_coroutine_record.py`, `test_coroutine_first_send.py`):
an awaited call served from the cache, a coroutine closed before its
first send returns the frame, a coroutine spliced by a Koskari `await`,
`test_context_token.py` unchanged.

## Out of scope

- C stack depth per re-entry level (demand-flattening.md step 6, part 2)
  waits on the resumable-reads row split for the CALL family.
- The keyword path through `KosArgSpread` and `demand_call`'s tuple
  slice; rare at the boundary.
- `Procedure` semantics and the class builder's dunder wrapping.
