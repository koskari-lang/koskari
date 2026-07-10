# Domain Task Isolation (Fork-on-Context-Creation)

This document specifies the fix for parameter-binding corruption between
concurrently-driven Koskari contexts: **new contexts fork (snapshot) the
ambient Domain instead of sharing it by pointer**. It covers the observed
failure, the semantic decision being made, the data-model and API changes,
the implementation plan, and the testing matrix.

**Status**: proposed. Companion to [domain.md](domain.md), which describes
the Domain/ParamRef/PARAM machinery this document amends.

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

### Why this is the contextvars problem

This is precisely the failure mode that motivated PEP 550/567: dynamic
bindings implemented as mutation of task-shared storage break as soon as
tasks interleave. `contextvars` fixed it by giving each logical task a
**snapshot view captured at task creation**, with writes visible only
within that task. We adopt that **storage discipline**. We do *not* adopt
contextvars' **control discipline** (`Context.run` re-entrancy ban,
one-shot reset tokens): it is strictly nested and cannot express Koskari's
multi-shot continuations, which our PARAM dyn nodes (clone-on-install,
save-at-rewind) already handle correctly. The dynchain remains the scoping
mechanism; only domain inheritance changes.

---

## Goals

- Two concurrently-driven contexts never observe or destroy each other's
  `parameterize` bindings.
- A context that dies mid-extent cannot corrupt any other context's view.
- Existing single-context semantics are unchanged: nested parameterize,
  unwind-on-return/raise, continuation capture/apply (multi-shot), and the
  synchronous Koskari→Python→Koskari inheritance of currently-applied
  bindings.
- The `with Domain():` Python context manager keeps its documented
  contract, and remains the explicit opt-in for genuinely shared ambient
  state.
- No change to `dyn_param.c`, the PARAM opcodes, compiler lowering, or the
  transfer-plan wind/unwind integration.

## Non-Goals

- Chain-walking parameter lookup (Racket-style parameterization with a
  version cache). That is the eventual richer design; this fix makes the
  current machinery sound and does not preclude it. See "Future" below.
- Fixing the generator freeze/install PARAM leak (see "Out of scope").
- Cross-thread propagation policy (unchanged: explicit only).

---

## Design Decisions

### D1 — Fork on ambient (ctx-stack) inheritance

When `KosContext_New` inherits from an active context, it creates a **new
`KosDomain` whose `param_bindings` is a snapshot** of the parent's:

- Parent has no bindings dict, or an empty one → child starts with
  `param_bindings = NULL` (lazy init preserved).
- Otherwise → `PyDict_Copy` of the parent's dict.

The snapshot is taken at context-creation time, which for coroutines,
async generators, and demands is the moment the callable is invoked
(coro.c `KosCoroutine_NewFromCode` / `KosAsyncGen_NewFromCodeAndNs`,
demand.c). Because `parameterize` applies its values to the parent's dict
eagerly, a child created *inside* a parameterize extent snapshots the
parameterized values — so the synchronous nesting semantics ("inner
context sees outer's current bindings") are preserved exactly.

Values are shared by reference (a shallow dict copy); only the *binding
table* is per-context. Mutating a mutable parameter value remains visible
everywhere it is referenced, as today.

### D2 — Explicit Python domain scope still shares

When there is no active context and the new context inherits from
`python_domain_stack` (`with Domain():`), it **shares** that domain by
pointer, as today. Rationale:

- `with Domain():` is an explicit, named object handed to the computation;
  it is the intended mechanism for deliberate ambient sharing and for
  Python to observe parameters stored by Koskari after it returns
  (tests/lib/test_domain.py contract).
- With D1 in place, forking is the *default*; a single explicit sharing
  mechanism must survive, and this is it.

Documented hazard: two tasks driven concurrently under one `with Domain():`
scope opt into shared shallow binding and its interleaving behavior. That
is now a choice, not a default.

### D3 — Save/restore machinery unchanged

`kos_dynparam_enter` / `kos_dynparam_restore` and the PARAM caps
(`NEEDS_INSTALL_WIND`, `NEEDS_UNWIND_ACTION`) are untouched. With one
domain per context, enter/restore pairs are LIFO per domain again, which
is the regime the mechanism was designed for. Multi-shot continuation
re-entry keeps its current semantics: installed clones re-save against the
(now context-private) domain at rewind.

### D4 — Semantic change: no write-through to the parent

`ParamRef.store` / `ref-set!` / `parameterize` inside a child context is
no longer visible to the parent or siblings after the child completes.
This is the same visibility rule as `contextvars` and is the point of the
fix. Code that used a parameter as a covert return channel across a defer
boundary must instead return values, mutate a shared object held *in* a
parameter (D1 shares values), or run under an explicit shared `Domain`
(D2).

### D5 — `version` becomes a generation counter

`KosDomain.version` is currently written once (0) and never read. On fork,
set `child->version = parent->version + 1`. Informational only (repr /
debugging / future cache invalidation); no behavior depends on it.

### D6 — KosDomain becomes GC-aware

Forking multiplies domain instances, so the existing gap matters more:
`KosDomainType` has no `Py_TPFLAGS_HAVE_GC` and no traverse/clear, while
`param_bindings` can hold arbitrary values that cycle back to the domain
(closures, coroutines). Add `Py_TPFLAGS_HAVE_GC`, `tp_traverse`
(`Py_VISIT(param_bindings)`), `tp_clear`, and switch allocation to the GC
APIs. This also brings the implementation in line with what
[domain.md](domain.md) Step 2 already claims.

---

## Visible behavior: before / after

| Scenario | Before (shared) | After (fork) |
|---|---|---|
| Sync nesting: koskari parameterizes, calls Python, which calls koskari | inner sees binding | inner sees binding (snapshot includes applied values) |
| Deferred sibling reads param bound by other sibling mid-extent | sees sibling's value | sees own snapshot (default) |
| Sibling exits parameterize while other sibling's extent is live | clobbers shared dict | no effect on others |
| Task killed while suspended in parameterize | binding leaks into parent forever | dies with the task's own domain |
| `ParamRef.store` in child, read by parent after child completes | visible | **not visible** (D4) |
| `with Domain():` store from Python, read inside koskari | visible | visible (D2, unchanged) |
| `with Domain():` store from koskari, read from Python after return | visible | visible (D2, unchanged) |
| `Domain.get_contexts()` on a parent domain | lists child ctxs (same domain) | lists only ctxs sharing that exact domain object |

---

## API and data-model changes

### domain.h / domain.c

```c
/**
   Fork a domain: new KosDomain whose param_bindings is a snapshot of
   parent's (NULL when parent has no bindings). version = parent+1.
   parent may be NULL (equivalent to KosDomain_New). Returns new ref.
*/
KosDomain *KosDomain_Fork(PyObject *parent);
```

Implementation: `KosDomain_New()`, then if `parent` is a `KosDomain` with
a non-empty `param_bindings`, `PyDict_Copy` it into the child; copy/bump
`version`.

### eval.c — `KosContext_New`

Replace the inheritance block:

```c
{
  PyObject *domain = kos_domain_current();

  if (domain && ! kos_ctx_current()) {
    /* explicit `with Domain():` python scope: share (D2) */
    ctx->domain = domain;
    Py_INCREF(ctx->domain);
  } else {
    /* ambient ctx inheritance (fork, D1) or no ambient (fresh) */
    ctx->domain = (PyObject *) KosDomain_Fork(domain);
    if (! ctx->domain) { /* existing error path */ }
  }
}
```

Note this also removes the existing redundancy: the old code consulted
`kos_ctx_current()`'s domain and then `kos_domain_current()`, but the
latter already prefers the ctx stack.

No changes to `KosContext_Free` (it already drops the domain ref), to
coro.c / demand.c (they call `KosContext_New`), or to dynchain code.

### Python surface

No new Python API in v1. `Domain`, `ParamRef`, `parameter()` signatures
are unchanged. A later convenience (`Domain.fork()`, or a
`koskari.eval(..., domain=...)` override) can be added if a use case
appears; do not block on it.

---

## Performance

Fork cost is one dict copy per **context** creation (per defer / coroutine
/ async-gen / demand), not per call frame, and only when the parent has
bindings; the common no-params case stays `NULL`→`NULL`. Parameter dicts
are expected to be small (tens of entries at most).

If profiling ever shows fork cost, the upgrade path is lazy copy-on-write
at the two mutators (`kos_domain_set_param`, `kos_domain_clear_param`):
share the dict on fork and copy before the first write when
`Py_REFCNT(bindings) > 1`. This works only if the bindings dict is never
exposed or borrowed outside domain.c — an invariant that holds today (the
snapshot paths expose PARAM-node dicts, not the domain dict) but must then
be documented and kept. **v1 uses the eager copy**: zero invariants, and
correctness first.

---

## Implementation plan

1. **`KosDomain_Fork`** in domain.c/h, with `version` bump (D5).
2. **GC support** for `KosDomainType` (D6): `Py_TPFLAGS_HAVE_GC`,
   `tp_traverse`, `tp_clear`, GC alloc/dealloc, `PyObject_GC_Track/UnTrack`
   in `KosDomain_New` / `domain_dealloc`.
3. **`KosContext_New`** inheritance change in eval.c (D1/D2).
4. **Tests** (below). The reproducer lands in `tests/reproducers/`.
5. **Docs**: update [domain.md](domain.md) — the "Koskari→Python→Koskari"
   inheritance paragraph, D3/Step 2 status, and testing-matrix item 4
   (which currently sidesteps the shared-domain case by giving each
   context its own domain — after this change that is simply true). Update
   user-facing parameterize docs in `docs/` to state task-isolation
   semantics, without linking design/.

## Testing matrix

Unit (tests/lib, phase_lib):

1. **Fork isolation, child→parent**: create ctx under active ctx (or via
   `KosDomain_Fork` directly); child `set_param` invisible to parent.
2. **Fork isolation, parent→child**: parent `set_param` after fork
   invisible to child (snapshot, not view).
3. **Snapshot captures applied parameterize values**: enter PARAM extent,
   fork, read key in child → parameterized value.
4. **`with Domain():` share preserved**: existing
   tests/lib/test_domain.py must pass unmodified, including
   `test_domain_paramref_after_koskari_return`.
5. **GC**: domain in a reference cycle via param value is collected;
   `gc.is_tracked(Domain())` is true.

Interleave (deterministic, no event loop — drive two coroutine objects
alternately with explicit sends):

6. ctxA enters `parameterize x=1`, suspends; ctxB (forked sibling) reads
   `x` → default; ctxB enters `x=2`, suspends; ctxA resumes, reads `x` → 1,
   exits; ctxB resumes, reads `x` → 2, exits; parent reads `x` → default.
7. ctxA suspended mid-parameterize is dropped (GC'd) without resuming;
   parent and ctxB bindings unaffected.

Full stack (tests/reproducers, phase_reproducers):

8. Port of the reproducer above (asyncio, `create-task` + `defer` +
   `sleep`): assert `results == {"A": "A", "B": "B"}` and the parameter
   resolves to its default after `main` returns.

Regression: existing test_paramref.py, test_parameter.py,
test_push_param.py, test_parameterize.py, and continuation
capture/apply-inside-parameterize tests must pass unchanged.

## Migration notes

The only intentional behavior change is D4 (no write-through from child
contexts). Bootstrap `.ki` sources and examples should be audited for
params used as cross-defer channels; none are expected. Anything found
should switch to returned values or an explicit shared `Domain`.

---

## Out of scope (tracked separately)

- **Generator freeze/install PARAM leak**: generator frames run spliced
  into the *consumer's* context, so a generator frozen mid-parameterize
  leaves its bindings applied to the consumer's domain. Fork does not fix
  this (it is intra-context). Fix: `kos_dyn_gen_freeze_and_release` /
  `kos_dyn_gen_install` should honor the PARAM wind/unwind caps for nodes
  in the spliced segment, mirroring the transfer-plan INSTALL behavior.
- **TLS stack overflow**: `push_active_ctx`/`push_active_token` silently
  drop pushes at `KOS_TLS_STACK_MAX` while pops decrement unconditionally,
  desynchronizing `kos_ctx_current()` — which this design makes
  load-bearing for fork-vs-share. Should hard-error (or grow).
- **PARAM `SAVED` hygiene**: `kos_dynparam_enter` assigns `SAVED` without
  releasing a prior dict and `restore` never clears it; safe today
  (fresh nodes/clones only), leak-prone for any future in-place re-wind.

## Future

The richer design remains chain-based lookup (Racket parameterization):
`parameter` deref walks the dynchain for the nearest PARAM node binding
the key, the domain shrinks to a defaults/root store, and enter/restore,
`SAVED`, and the sentinel disappear — generators and multi-shot
continuations become correct by construction. `version` (D5) plus a
per-context `(key → value, version)` cache is the standard mitigation for
lookup cost. Fork-on-creation is compatible with, and does not prejudge,
that migration.

---

## References

- [domain.md](domain.md): Domain / ParamRef / PARAM design (amended by this doc)
- [extent-wind-unwind.md](extent-wind-unwind.md): unified extent wind/unwind contract
- [freeze-thaw-context-segments.md](freeze-thaw-context-segments.md): context freeze/thaw
- PEP 550 / PEP 567: motivation and semantics for task-local dynamic state
- Racket parameterization & continuation marks: the chain-based end state
