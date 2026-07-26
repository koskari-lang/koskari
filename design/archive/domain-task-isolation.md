# Domain Task Isolation (Per-Context Parameter Refs)

This document specifies the fix for parameter-binding corruption between
concurrently-driven Koskari contexts, and the storage redesign that
carries it: parameter bindings become **refs** (ordinary Refs) held in a
**per-context** dict that is pointer-copied (forked) at context creation.
`parameterize` installs fresh refs and restores the prior refs on
unwind. The Domain narrows to an interop boundary: it homes the base ref
dict that root contexts copy from, and the TLS machinery in domain.c
remains the discovery path for Python-side access.

**Status**: superseded; archived. Companion to [domain.md](domain.md), which
describes the Domain/ParamRef/PARAM machinery this document amends. This
revision supersedes the earlier fork-of-values proposal: the observed
failure and the fork-at-creation discipline are retained, the storage
representation and ownership changed during review.

**Superseded in part by
[../parameter-resolution.md](../parameter-resolution.md)**, which replaces D5,
D6, D7 and D10. The reasons: D2's evaluator-side resolution path was never
built, so every read went through thread-local discovery; D6's sticky default
fill wrote on read, which made whether a `set!` was task-local depend on whether
anyone had read the name before the fork; and D10's ref-fetch surface
(`parameter-ref`) exposed the inner cells, giving two ref types with opposite
capture semantics. The problem statement, the reproducer, and D1/D3/D4 stand.

---

## Problem

### Current behavior

`KosContext_New` (eval.c) chooses the new context's domain as follows:

1. If a context is active on the TLS ctx stack, **share** its domain
   (`Py_INCREF` of the same `KosDomain` pointer).
2. Else if the per-thread `python_domain_stack` is non-empty (`with
   Domain():`), share that domain.
3. Else create a fresh `KosDomain`.

`parameterize` is implemented as **shallow binding**: `kos_dynparam_enter`
(dynchain/dyn_param.c) writes the new values directly into the domain's
`param_bindings` dict, saving each prior value into the PARAM node's
`SAVED` dict; `kos_dynparam_restore` writes the saved values back when the
extent unwinds. `AWAIT`/`YIELD` suspension deliberately does **not** unwind
the chain, so a suspended context's parameterize bindings remain applied to
the domain while it is parked.

Shallow binding is sound only while enter/restore pairs form a global LIFO
order on each domain. One context always satisfies this (chain discipline).
Two sibling contexts sharing one domain and interleaving across awaits do
not.

### Reproducer

```
define-parameter p None
define results #{}
define worker [name delay]:
    (await)                          ; suspend into the task before binding
    (parameterize [[p name]]
        (sleep delay)                ; yield so the other task's binding is live
        (set! (item results name) (parameterq p None)))
define main []:
    define t1 (create-task (defer worker "A" 0.1))
    define t2 (create-task (defer worker "B" 0.2))
    (sleep 0.5)
    results
(print "results =>" (main))
```

Each `defer` builds a `KosPyCoroutine`/context while `main`'s context is
active on the TLS stack, so both workers share `main`'s domain. Expected
result: `#{A: "A", B: "B"}`. Actual trace with the shared domain:

| t    | event                          | domain `p` | A.SAVED    | B.SAVED    |
|------|--------------------------------|------------|------------|------------|
| 0    | A enters `parameterize p="A"`  | `"A"`      | `{p: ∅}`   | —          |
| 0    | B enters `parameterize p="B"`  | `"B"`      | `{p: ∅}`   | `{p: "A"}` |
| 0.1  | A wakes, reads `p`             | `"B"` ← **leak: A observes B's binding** | | |
| 0.1  | A exits, restores `SAVED`      | ∅ ← **B's live binding destroyed**       | | |
| 0.2  | B wakes, reads `p`             | ∅ → default ← **B observes nothing**     | | |
| 0.2  | B exits, restores `SAVED`      | `"A"` ← **garbage persists after both extents closed** | | |

(∅ = unset / sentinel.) Which corruption each task observes depends on
scheduling, but with any interleaving at least one of leak, lost-binding,
or stale-restore occurs. The same mechanism also means:

- **Abandoned tasks poison the domain**: `KosContext_Free` is memory
  teardown only (documented: no PARAM restore), so a deferred task dropped
  while suspended inside `parameterize` leaves its bindings permanently
  applied to the parent's shared domain.
- The failure needs no OS threads; a single event loop suffices.

---

## Semantic model

The model is **Racket parameters** (dynamic extent binding), and the
design should be read against that precedent rather than Python's
`contextvars`:

- The purpose of `parameterize` is to pass values **into** deeply nested
  calls without threading them through every signature. A binding exists
  for the calls beneath it until its extent exits.
- Bindings must not leak between extent stacks when one computation
  awaits and yields control to another. Isolation is between concurrent
  extent stacks, with values flowing downward at call time.
- `ref-set!` on a parameter is permitted (storage is a legitimate
  secondary use), with the expectation that a value set under a
  `parameterize` extent is discarded when the extent that introduced the
  binding unwinds.
- A `contextvars`-style type with its own visibility rules remains
  possible as a **separate future type**; parameters do not try to be
  it, and this design does not preclude it.

The observation that snapshot-at-task-creation fixes interleaving is the
one thing borrowed from PEP 550/567. The control model (`Context.run`
re-entrancy ban, one-shot reset tokens) is not adopted: it cannot express
Koskari's multi-shot continuations, which the PARAM dyn nodes already
handle.

---

## Design

### D1 -- Bindings are refs, not values

The parameter store maps name → **ref**, where a ref is an ordinary
base Ref (`KosRef` in ref.c: name + value, empty when the value slot is
NULL). Readers and writers go through the ref. Holding a resolved ref
is holding a live shared object: every holder of the same ref sees the
same value, including after the extent that installed it has popped.

This makes the `set!` reset rule structural instead of behavioral: a
`set!` under a `parameterize` extent writes the extent's ref, and the
pop discards the ref pointer. There is no saved-value write-back to get
wrong.

### D2 -- The ref dict lives on KosContext

`KosContext` gains a `param_refs` dict (name → ref, lazy-init NULL).
Resolution inside the evaluator is `KosEvalState → KosContext →
param_refs`, with **no TLS access on this path**. TLS discovery
(`kos_ctx_current()`, the `python_domain_stack`) is used only by
Python-side entry points (ParamRef methods, `parameter()`), where no eval
state is in hand.

The `KosDomain` object stops holding live parameter state for running
contexts. Its narrowed role is D5.

### D3 -- Fork at context creation

`KosContext_New` initializes `param_refs` as:

1. Active context on the TLS ctx stack → `PyDict_Copy` of its
   `param_refs` (NULL stays NULL; lazy init preserved); the domain
   anchor is inherited.
2. Else `python_domain_stack` non-empty → `PyDict_Copy` of that domain's
   base ref dict (D5); the context anchors to that domain.
3. Else → empty ref table, and a **fresh KosDomain is minted**. Every
   context has a domain, created as needed; the domain is the mandatory
   identity/lifecycle anchor of the computation, while live parameter
   state stays in `param_refs`.

The copy is of **pointers**: parent and child share the ref objects that
existed at fork time. Only the binding *table* is per-context. A child
created inside a `parameterize` extent copies the extent's applied refs,
so synchronous nesting semantics are preserved: the inner context sees
the outer's current bindings.

Capture timing rule: the copy happens when the context is built, which is
the moment the callable is invoked (`defer`, coroutine and async-gen
construction in coro.c, demand.c). **The defer is the call site.**
`create-task` is transport; it does not re-capture. This differs from
asyncio (which snapshots at Task wrap) and is intended.

### D4 -- PARAM nodes swap refs

`kos_dynparam_enter` becomes, for each bound name: mint a fresh ref
holding the new value, record the prior ref (or an absent-sentinel) in
`SAVED`, and store the fresh ref into the owning context's
`param_refs`. `kos_dynparam_restore` swaps the prior ref back, or
deletes the entry when the sentinel was recorded. The node's `BINDINGS`
dict holds name → ref (the fresh refs it minted).

Consequences:

- Enter/restore are pointer swaps on a context-private dict; LIFO
  discipline holds per context by construction, which is the regime this
  machinery was designed for.
- `kos_dynparam_clone` keeps its `PyDict_Copy`, which now copies ref
  pointers: **continuation applications share refs**. A `set!` made in
  one application of a multi-shot continuation is visible in the next
  application. This matches Racket's shared thread-cell behavior and is a
  deliberate semantic choice; it must be pinned by a test, because it
  falls out of the implementation and would otherwise be an accident.
- The transfer-plan shapes are unchanged: PARAM_RESTORE on unwind, param
  enter folded into INSTALL, clones re-save against the applying
  context's dict at rewind.
- The partial-failure hole in the current enter loop (a mid-loop error
  leaves earlier keys applied with no `SAVED` record) is fixed in
  passing: on error, roll back the swaps already made.

### D5 -- Domain homes the base ref dict

The `KosDomain` object keeps one piece of parameter state: `base_refs`
(name → ref, lazy). It serves three roles:

- **Pre-defines**: the host populates refs before starting Koskari
  (`with Domain(): ParamRef("*interactive*").store(...)`). Root contexts
  copy `base_refs` at creation (D3 rule 2), sharing those refs.
- **Python-side access point**: ParamRef/`parameter()` calls with no
  active context resolve against the innermost Python domain scope's
  `base_refs`.
- **Post-return observation**: Python reads results through the same
  shared refs after Koskari returns. The contract narrows in one
  documented way: a parameter **first created inside** a Koskari context
  lands in that context's dict and is not visible to Python afterward.
  Refs that existed in `base_refs` when the root context copied are the
  channel; pre-define what you intend to observe.

`with Domain():` keeps its surface (context manager, per-thread scope
stack). The TLS stacks in domain.c remain the mechanism that tracks which
context is ambient on the thread; the AWAIT/YIELD scope pop (invariant I2
in domain.md) is what keeps that tracking correct while an event loop
interleaves tasks, and stays load-bearing for the Python-side discovery
path.

### D6 -- Ref operations mirror the globals namespace; value queries observe

Parameter access splits by category, and the namespace-like semantics
belong to the **ref** side only, because a ref is a place:

**Ref (place) operations** -- `define-parameter` / `ParamRef` deref and
store, `parameter-ref`. Resolving `name` against a store (a context's
`param_refs`, or a domain's `base_refs`):

1. Ref present → use it.
2. Ref absent → create an empty ref, install it in the store, use it.
3. Ref empty and a declared default was supplied → store the default
   into the ref (the fill is visible to every sharer of that ref).
4. Ref empty and no default → NameError, same as an unbound global.

**Value queries** -- `parameter` / `parameterq`. Observational only: a
present, non-empty ref yields its value; otherwise the caller-supplied
default answers (never entering the store), or NameError. A query
creates nothing and writes nothing.

Ref rule 2 means resolving a place on a miss mutates the resolving
store. Under fork, a miss in a child creates the ref in the **child's**
dict; siblings that miss independently get independent refs. Sharing
topology follows ref creation: a ref is shared by exactly the contexts
descended (by fork) from the store where it was created. Pre-defining in
`base_refs` (D5), or a `parameter-ref` / `define-parameter` touch
before spawning children, is what establishes a shared ref -- a value
query cannot.

### D7 -- set! visibility rules

- **(a) Under an introducing extent**: `set!` writes the extent's fresh
  ref; the pop discards it. Reset on unwind is structural (D1).
- **(b) No introducing extent**: `set!` writes the resolved ref, which
  persists for the life of every context sharing it. This is the
  sanctioned storage channel: a ref shared via `base_refs` or via fork
  is a deliberate cross-context mailbox.
- **(c) Inherited refs**: a ref that arrived via fork behaves as a
  context-root binding of the child. No node in the child's chain
  introduced it, so nothing in the child resets it; it lives as long as
  the child does.

### D8 -- GC

Forking multiplies ref dicts and refs routinely outlive the extent and
the context that minted them (held by sibling forks, closures, captured
continuations). Required:

- `param_refs` is visited wherever the context's PyObject members are
  traversed (the wrapper types' `tp_traverse` paths that already visit
  `ctx->domain` and namespace members).
- `KosDomainType` gains `Py_TPFLAGS_HAVE_GC`, `tp_traverse`/`tp_clear`
  over `base_refs`, and GC allocation, closing the gap domain.md Step 2
  already claims closed.
- Refs are base Refs and participate in the existing Ref tracking. The
  planned Ref freelist / lazy-GC work must treat parameter refs as
  binding refs for its reveal invariant; this design increases their
  population.

### D9 -- TLS hardening rides along

`push_active_ctx` / `push_active_token` silently dropped pushes at
`KOS_TLS_STACK_MAX` while pops decrement unconditionally, desynchronizing
`kos_ctx_current()`. The evaluator-side resolution path (D2) no longer
depends on TLS, but fork-source selection (D3) and Python-side discovery
do. Silent drops become silent wrong-fork decisions.

Implemented as **growth** rather than a hard error: the per-thread stacks
start at `KOS_TLS_STACK_INITIAL` and double on demand, so overflow stops
existing and legitimately deep Koskari/Python interop recursion is not
punished. Growth failure is OOM-only and fatal (`Py_FatalError`) --
continuing with a desynced stack would corrupt fork decisions silently.

### D10 -- ParamRef narrows to an accessor

The dict holds plain base Refs; `ParamRef` (ref.c) is no longer the
stored entity. It survives as a **re-resolving accessor**: `deref()`
resolves the current ref (D6, using its stored default for the sticky
fill) and derefs it; `store()` resolves and writes through. Because it
re-resolves on every use, a ParamRef is safe to hold anywhere, including
module scope and closures -- it always addresses the innermost binding of
the context it runs in. Existing surface (`launch.ki`, `_variables.ki`
`define-parameter` expansion, tests) keeps working unmodified. Retiring
or renaming the type is cosmetic and deferred.

A separate **ref fetch** operation (returning the live ref itself) is
the opt-in for deliberate holders: storage channels, hot-loop consumers
that resolve once and read directly. Its contract is capture-at-
resolution: a held ref does not track later `parameterize` of the same
name.

---

## Walkthrough

`worker` parameterizes `p`, suspends mid-extent, reads after resume.
`main` parameterizes `p="main"` and spawns two workers.

**Scenario 1 -- spawn, then await all.** Main enters its extent (fresh
ref `c_main` in `C0.param_refs`), builds CA and CB (each fork
`{p → c_main}`), and awaits; the AWAIT pops C0 off the TLS stack, its
extent still applied to its own private dict. The loop drives A: A's
extent swaps `c_A` into `CA.param_refs`, suspends; B likewise with
`c_B`. A resumes and reads through its own dict: `"A"`. A's pop restores
`c_main` in CA's dict and **cannot touch** B's live `c_B` -- the step
that corrupted under the shared dict now operates on disjoint dicts. B
reads `"B"`; main resumes and reads `"main"`.

**Scenario 2 -- spawn, main ends, loop drains.** Same through the forks.
Main's extent exits (C0's dict restored), main returns, C0 is freed.
`c_main` survives, held by CA's and CB's dicts. The workers run later
with an initially empty TLS stack, push themselves, parameterize, read
their own values, and inherit `"main"` for plain reads -- capture at
fork, per D3, with no dependence on any ancestor being alive. When the
last worker completes, the last reference to `c_main` drops and it is
freed by ordinary refcounting. Cancellation, and even drop-without-unwind
(`KosContext_Free`), destroy only the dead context's private dict.

The previously catastrophic scenario (2) is now the boring one.

---

## Visible behavior: before / after

| Scenario | Before (shared value dict) | After (per-context refs) |
|---|---|---|
| Sync nesting: koskari parameterizes, calls Python, which calls koskari | inner sees binding | inner sees binding (fork copies applied refs) |
| Deferred sibling reads param bound by other sibling mid-extent | sees sibling's value | sees own fork (default) |
| Sibling exits parameterize while other sibling's extent is live | clobbers shared dict | swaps its private dict; no effect on others |
| Task killed while suspended in parameterize | binding leaks into parent forever | dies with the task's own dict |
| `set!` (no extent) on a ref shared via fork or base_refs | visible (accident of sharing) | visible (sanctioned channel, D7b) |
| `set!` inside extent, then extent exits | reset via saved-value write-back | reset structurally: ref discarded |
| Parameter first created inside koskari, read by Python after return | visible | **not visible**; pre-define in `base_refs` to observe (D5) |
| `with Domain():` pre-store from Python, read inside koskari | visible | visible (root context copies `base_refs`) |
| Multi-shot continuation: `set!` in application N, read in application N+1 | reset each application (values re-applied) | **visible** (applications share refs, D4) |
| `Domain.get_contexts()` on a parent domain | lists child ctxs (same domain) | lists only ctxs holding that exact domain object |

---

## define-parameter and closures

`define-parameter` (bootstrap `_variables.ki`) binds a ParamRef, which
re-resolves per use (D10), so its behavior is position-independent and
safe. The documentation point is about **refs**, for when the ref-fetch
surface exists:

- In a function body, resolving a ref binds the frame to the innermost
  binding at entry. Cheap direct access; correct for the
  read-config-once consumer. It does not see a later `parameterize` of
  the same name, including one opened later in the same body.
- At module scope, a resolved ref is created in the module-load
  context's dict -- a fork side branch that nothing else descends from.
  A closure over it captures a ref that goes stale when the load context
  dies. This is an anti-pattern, and pre-declaring "for efficiency" is
  exactly the trap: users must reach for the re-resolving accessor or the
  in-body resolve instead.

---

## Known hazard: `with Domain():` under async Python

The per-thread `python_domain_stack` is pushed by `__enter__` and popped
by `__exit__`. A coroutine that suspends **inside** the `with` block
leaves the domain on the thread's stack while the event loop drives
unrelated tasks: they observe an ambient Python domain scope that
logically belongs to the suspended coroutine. `hosted_run`
(koskari/launcher.py) has exactly this shape.

This is the same interleaving disease on the Python side of the fence.
Resolution decision pending, two candidates:

1. Back the Python-side scope with a `ContextVar` instead of the TLS
   array -- Python-side ambient state following Python tasks is the one
   place contextvars is precisely the right tool.
2. Document `with Domain():` as synchronous-only (no suspension inside
   the block) and keep the TLS array.

Not a blocker for the commits below; the fork discipline bounds the blast
radius to fork-source selection for contexts created from pure-Python
stacks during such a window.

---

## Implementation path

Three commits, each independently green.

### Commit A -- refs replace values (representation)

Storage stays on the shared `KosDomain`; only the representation and the
enter/restore mechanics change. Observable behavior is unchanged for all
existing tests, with one intended exception: multi-shot `set!`
persistence (D4), which gets its pinning test here.

1. domain.c: `param_bindings` values become refs. Replace
   `kos_domain_get_param` / `set_param` / `clear_param` with a resolver
   (`kos_domain_resolve_ref(domain, key, create)`) implementing D6;
   set/get become ref write-through/deref. `m_parameter` stays a value
   query per D6: peek-only, no ref creation, the default a caller-local
   fallback. (An interim revision wrongly gave it the ref-side fill;
   corrected during review -- the fill belongs to ref operations only.)
2. dyn_param.c: `kos_dynparam_new` / `_new_from_bindings` mint fresh
   refs around incoming values (PUSH_PARAM stack discipline untouched);
   `SAVED` records prior refs or the absent-sentinel; enter/restore
   become pointer swaps; enter rolls back on mid-loop failure. Clone is
   unchanged in code, changed in meaning (shared refs) -- pin it.
3. ref.c: ParamRef deref/store reimplemented over the resolver (D10).
4. Tests: full suite green unmodified (contract-level assertions only);
   new: multi-shot shared-ref pin, holder-retains-popped-ref,
   sticky-default fill, empty-ref NameError.

### Commit B -- ownership moves to the context; fork (isolation)

1. eval.h/eval.c: `KosContext.param_refs` field; `KosContext_New`
   implements D3 (fork active ctx / copy `base_refs` / fresh mint);
   `KosContext_Free` drops it. `ctx->domain` remains mandatory --
   inherited or minted as needed -- as the identity/lifecycle anchor;
   live parameter state moves to `param_refs`.
2. domain.c: `KosDomain.base_refs` (D5); Python-side resolution order
   becomes active-ctx refs, else innermost python-scope `base_refs`,
   else no home (default or error, as `m_parameter` today). Evaluator
   callers resolve through `KosEvalState` (D2); dyn_param takes the dict
   from the context.
3. TLS stacks grow instead of silently dropping (D9).
4. GC (D8): context-traversal visits `param_refs`; `KosDomainType`
   becomes GC-aware over `base_refs`.
5. Tests: the reproducer above into `tests/reproducers/` (expected
   `#{A: "A", B: "B"}` and default after return); deterministic
   interleave (drive two coroutine objects with explicit sends, no
   loop); fork isolation both directions; snapshot-captures-applied-
   extent; base_refs pre-define flow (`*interactive*` shape);
   drop-mid-extent harmlessness; `tests/lib/test_domain.py` contract
   under the D5 narrowing (pre-defined refs observed after return).

### Commit C -- surface and docs

1. Ref-fetch entry point for deliberate holders (module function or
   Domain method returning the live ref).
2. Docs: amend [domain.md](domain.md) (D2/D3 storage description, the
   Koskari→Python→Koskari inheritance paragraph, testing-matrix item 4);
   user-facing parameterize docs state task-isolation semantics, the
   defer-is-the-call-site rule, and the define-parameter guidance,
   without linking design/.
3. Decision + implementation or documentation for the async
   `with Domain():` hazard. **Resolved**: the Domain is thread-scoped by
   design -- it represents the contexts currently active on a thread,
   and a suspending context removes itself (I2). What looked like a leak
   was task-scoped (contextvars) misframing; no mechanism change. The
   pop-mismatch RuntimeError is the intended tripwire for two tasks
   interleaving Domain blocks on one thread (no in-tree case exists;
   `hosted_run` is a structural singleton). ContextVar was evaluated and
   rejected: its guarantee is a scheduler contract, and only koskari_eval
   knows koskari's activation boundaries -- the loop cannot see them, and
   demand-sync and explicit-send driving have no loop at all.
4. Migration audit per notes below.
5. Threaded verifiers (`tests/reproducers/test_param_thread_isolation.py`):
   a defer created inside a parameterize extent on one thread reads its
   captured binding when driven on another; two threads inside
   same-name extents simultaneously (barrier-forced overlap) each read
   their own binding, and a store through the fork-shared root ref is
   visible across threads with barrier-ordered happens-before; a second
   thread sees none of the first thread's ambient state until the same
   Domain object is entered there explicitly.

---

## Migration notes

The intentional behavior changes:

- No write-through from child contexts except via refs shared at fork
  or via `base_refs` (D7). Bootstrap and examples audited for params
  used as cross-defer return channels; none expected. `launch.ki`'s
  `*interactive*` store works unchanged: the store creates the ref in
  the launch context, and command/REPL contexts fork from it. Hosts that
  want Python-side visibility pre-define in the Domain (D5).
- Python-after-return observation requires pre-defined refs (D5).
- Multi-shot continuations share refs across applications (D4).
- Module-scope ref capture is documented as an anti-pattern
  (define-parameter section); the ParamRef accessor form remains safe
  everywhere.

## Out of scope (tracked separately)

- **Generator extents: intended semantics, not a leak.** Generators are
  freezes following the Python model: yield does not unwind, so a
  parameterize extent persists across the generator's suspensions and
  undoes when its block exits or the generator is exhausted. With
  per-context refs this lifecycle is contained: an async generator owns
  its context (refs forked at creation), so its extents live in its own
  table -- the driver's view is never touched while the generator is
  parked mid-extent, and an abandoned generator takes its table with it.
  Verified in tests/reproducers/test_param_generator_extents.py. The
  extent-safe suspension methodology is delimited continuations
  (shift/reset), whose transfer plans unwind and rewind PARAM extents
  per application: **yield does not unwind; shift does**. The earlier
  fix sketch (freeze/install honoring PARAM wind/unwind caps) is
  narrowed to the intra-context spliced-segment machinery
  (kos_dyn_gen_*), should a consumer-reachable path through it with
  PARAM nodes ever exist; the surface generator story needs no fix.
- **Async `with Domain():`** decision (see Known hazard) if deferred out
  of Commit C.
- **PARAM `SAVED` hygiene** for future in-place re-wind (enter assigns
  `SAVED` without releasing a prior dict); safe today, noted for any
  future re-entry path.

## Future

Chain-based lookup (Racket parameterization proper) remains the richer
end state: resolution walks the dynchain for the nearest PARAM node
binding the key, and the per-context dict shrinks to a root store. This
design moves toward it: the nodes already own the refs, so the migration
is a lookup-path change with the swap machinery deleted, and generators
and multi-shot continuations become correct by construction. Per-context
dicts with ref pointers are the natural cache representation for that
lookup. Nothing here prejudges it.

---

## References

- [domain.md](domain.md): Domain / ParamRef / PARAM design (amended by this doc)
- [extent-wind-unwind.md](../interpreter/extent-wind-unwind.md): unified extent wind/unwind contract
- [freeze-thaw-context-segments.md](freeze-thaw-context-segments.md): context freeze/thaw
- Racket parameterization and thread cells: the semantic model
- PEP 550 / PEP 567: the snapshot-at-creation storage discipline (control model not adopted)
