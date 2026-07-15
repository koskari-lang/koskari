# CALL Family Unification and Install-Path Acceleration

**Status: Phases 0-1 COMPLETE (2026-07-15, branch `call_cleanups`);
Phase 1 gate green (suite 4700, native 293, ASAN 293), awaiting review.
Phases gate on review; do not start a phase before the prior phase's
gate is cleared.**

## Purpose

The `CALL_*` family (21 opcodes: callee source x attr x arg style x
tail) is 24.4% of the inlined dispatch body (21,340 B; see
[dispatch-sizing.md](../dispatch-sizing.md)) and the slowest per-event
machinery in the evaluator: a Koskari-to-Koskari call install costs
roughly 150 ns of generality, indirection, and per-call recomputation
of static properties. This document specifies a phased restructuring
with two goals served by the same shape:

1. **Shrink**: every call opcode reduces to an inline prologue that
   gathers the callee and arguments, then makes **one noinline call**
   into a shared install/call core.
2. **Accelerate**: the shared core fronts a monomorphic **simple-call
   fast path** (arity check + direct slot bind) that bypasses the
   general formals machinery, and the per-call recomputation of static
   function properties is eliminated.

Non-goals (this effort): changing the opcode set or compiler emission;
namespace-free frames (future, own design note); the tail-call
promotion bytecode pass ([language/tail-call-bytecode-pass.md](../language/tail-call-bytecode-pass.md),
complementary); PGO ([pgo-build.md](pgo-build.md), complementary).

## Current cost ledger (hot CALL_FAST on a KosFunction)

| step | cost class | static? |
|---|---|---|
| fast deref callable | cheap | -- |
| `KosCallable_Check` | type check per call | per callable |
| `KosStackSteal_Tail` | memcpy args + Free | avoidable (non-tail) |
| `do_frame_prep` / `wants_frame_fn` | indirect call per call | per type/instance |
| frame push (freelist + inline retarget) | cheap (landed) | -- |
| `KosCallable_Call` -> vinstall | indirect call | per type |
| `KosNamespace_Copy(BASE_NS)` | freelist alloc + header + closure-Ref loop | -- |
| `KosFormals_VectorApplyFast` | general formals machine | per function shape |
| `kos_function_bind_self_if_local` | ~~`PySequence_Contains` per call~~ flag test (Phase 1) | **per function** |
| `KosCode_YIELDS` branch | one bit test (ledger over-attributed it) | per code |
| `SetFrameCodeAndNamespace` | refcount churn + retarget check | -- |
| return path | cheap (landed: plain return + DISPATCH) | -- |

Non-vinstall callables (e.g. Expression) additionally pay
`vcall_build_args_tuple` (+ kwds dict) per call through the
`KosCallable_Call` fallback.

## Ownership contract (the spine)

Every phase preserves and is reviewed against this contract:

- **argv window (borrowed)**: for non-tail, non-star calls the argument
  vector is a borrowed pointer into the caller's operand block
  (`TailPtr` shape). The core binds from it, then drops the window from
  the caller block (`DropNoErr`). No block event (frame push is not
  one for the *caller's* block; see reorder below), no stack mutation,
  and no suspension may occur between taking the window and dropping it.
- **steal (owned)**: tail calls (frame reset clobbers the caller block)
  and star/names marshalling produce an owned `KosStackSteal` /
  marshalled vector. The core consumes it; `KosStackSteal_Free` runs on
  every exit path.
- **error paths**: on any failure the core owns cleanup of whatever it
  was handed (mode-dependent), never the prologue. Prologues only clean
  up what they created before the core call.
- Every phase gate runs the full suite, native container tests, and
  ASAN.

## Measurement protocol

- Meters: `tox -qe churn` full run; `churn_method` and `churn_tree` are
  the primary call meters. Take a **fresh baseline** at Phase 0 (the
  list-opcode work moved `list_each`).
- Dispatch body: regenerate [dispatch-sizing.md](../dispatch-sizing.md)
  per its embedded procedure after Phases 2 and 3.
- Transition/behavior counters: `KOS_TRACE_EVENTS` additions only if a
  phase needs a question answered; remove or keep per finding.

---

## Phase 0 -- Baseline

No code change. Record: fresh churn run (all pairs), current dispatch
fn size, and (optional, one traced run) opcode timing for the CALL
family. Commit nothing; keep numbers in the PR description or notes.

## Phase 1 -- Low-hanging fruit (no structural change) [COMPLETE]

Each item was independently assessed; one landed as code, the rest
resolved as audit verdicts that reorder work into Phase 2/3.

1. **`bind_self` at creation.** LANDED. `KosFunction.bind_self_state`
   (0 unresolved / 1 bind / 2 no-bind), computed lazily on first call
   -- safe because `name` is read-only externally and both internal
   assignments precede any call; the zero default makes constructors
   fail-safe. Pre-binding into `BASE_NS` was rejected: it guarantees a
   function -> base-ns -> function reference cycle; the flag form does
   not. Measured: churn_method 13.20 -> 12.01 s (-9.0%), list_each
   -6.2%, tree -2.5% (the per-call `PySequence_Contains` was ~24 ns on
   every named-function call).
2. **Install selection at creation.** NO-OP. `KosCode_YIELDS` is
   already a single flag-bit test; the ledger over-attributed it.
3. **`wants_frame` audit.** VERDICT: keep the hook. `demand` and
   `partial` are genuinely per-instance dynamic. For
   KosFunction/Expression the fn is the YIELDS bit behind an indirect
   call, eliminated by Phase 2's classify-once core -- an interim
   constant-fold would be churn.
4. **Steal -> borrowed window.** DEFERRED INTO PHASE 2, with a
   sharpened reason beyond the bind-before-push reorder: **no-frame
   installs push their result onto the caller's block** (generators --
   `wants_frame` is 0 for yielding code -- plus continuation invokes,
   demand, partial). With a borrowed window still occupying the
   caller's stack tail, the install's result lands on top of the args
   and the post-call drop consumes the result. Steal avoids this by
   clearing the args first. Only the Phase 2 core, which sequences
   bind -> drop -> result-push per install class, can host the window
   safely.
5. **`KosCallable_Check` audit.** VERDICT: defer. It is a real
   `PyType_IsSubtype` mro walk per call, but Phase 3's simple-call
   guard is an exact-type pointer compare that subsumes it on the hot
   path; a `tp_flags` bit now would be churn in CPython-owned flag
   space for an interim win.

**Gate: pause for review.** CLEARED PENDING SIGN-OFF: suite 4700
passed / 10 skipped, native 293, ASAN 293; churn deltas above. No
Phase 2 work before sign-off.

## Phase 2 -- Prologue / shared-core split

The structural phase. Target shape:

```
op_CALL_FAST:            /* inline: ~50-150 B */
  callable = fast_deref(slot)
  argv     = tail_ptr(n)              /* borrowed window */
  return kos_call_invoke(es, callable, argv, n, NULL, KOS_CALL_PLAIN);

kos_call_invoke:         /* ONE noinline shared core */
  [Phase 3 fast path lands here]
  classify callable -> install (KosFunction / generator / Expression /
    instance-call inject / plain Python vectorcall / tail variants)
  bind ns -> drop argv window -> [then, per class] push frame + set
    code+ns, OR create/push the no-frame result (agen, invoke, ...)
```

Design points:

- **Window sequencing is the core's first invariant** (the Phase 1.4
  finding): the borrowed argv window must be dropped from the caller's
  block **before anything is pushed onto it**. Frame installs push
  nothing onto the caller block, but no-frame installs (generator agen
  creation, continuation invokes, demand, partial) deliver their result
  there. The core's per-class sequencing -- bind, drop window, only
  then produce the result -- is what makes the steal removable at all;
  no prologue-level ordering can achieve this.
- **Bind-before-push reorder.** The core prepares the namespace (bind
  from the borrowed window), drops the window, and only then pushes the
  frame and installs code+ns. `PrepareCallNamespaceFromVector` does not
  need the frame; the generator branch already installs with no frame
  push. Verify no dependency on frame-before-bind (demand-flag
  inheritance rides the push and is unaffected). This makes the
  borrowed-window contract natural and confines steal to tail/star.
- **Core signature.** Vector shape throughout:
  `(es, callable, PyObject *const *argv, Py_ssize_t nargs,
  PyObject *kwnames, unsigned flags)` where flags carry tail-ness and
  argv ownership mode. Attr variants inject self during gathering
  (prepend into the steal, or the method-cache prologue passes self as
  argv[0] via the existing TailPrepend shape).
- **Gathering helpers.** NAMES gathering is inline-cheap (kwnames tuple
  is already vector shape). STAR splat expansion is a cold noinline
  gather helper that produces an owned vector, then calls the same
  core.
- **Kill the tuple fallback.** Give every KosCallable a vinstall
  (Expression et al.); delete `vcall_build_args_tuple` / kwds-dict
  building from the call path. The install slot API unifies on the
  vector signature.
- **Conversion order** (each step suite-green before the next): FAST
  family, then CALL/TOS, then ATTR (method cache stays in the
  prologue), then VAR (cold by VAR->FAST lowering), then STAR/NAMES
  gathering, then TAIL (flag into the core; the core owns
  reset_for_tail).
- The core is `KOS_NOINLINE`. Whether the Phase 3 fast path later earns
  inline promotion into the prologues is an explicitly deferred
  measurement question (PGO may resolve it for free).

Expected outcome: dispatch fn sheds most of the CALL family body
(~15 KB); per-call cost neutral or better (one call/ret added, layers
of duplication and dead branches removed).

**Gate: pause for review.** Regenerated dispatch-sizing, churn deltas,
suite + native + ASAN.

## Phase 3 -- Simple-call fast path

- **Classification at construction** (`BUILD_FUNCTION`):
  `KOS_FN_SIMPLE` iff formals are a plain positional run (no pos_only /
  kwd / kwd_only / star / starstar / by_refs), **no defaults (v1)**,
  code is non-yielding, and bind_self is resolved (Phase 1 flag).
  Store: flag + expected arity.
- **Fast install** at the top of `kos_call_invoke`:
  ```
  if (KosFunction_Check(c) && KOS_FN_IS_SIMPLE(c)
      && nargs == arity && !kwnames && !(flags & TAIL)) {
    ns = KosNamespace_Copy(BASE_NS);        /* freelist */
    for i in 0..nargs: INCREF argv[i]; raw slot i = argv[i];
    [bind self slot if flagged]
    drop argv window; push frame; set code+ns; return NORMAL;
  }
  ```
  A pure arity check and a direct slot loop -- `KosFormals_VectorApplyFast`
  is bypassed entirely. Implemented as a dedicated helper (or a
  simplified formals apply) so the tail-call variant can share it.
- **v2 (separate, after v1 measures)**: defaults (arity range
  [min, max], fill trailing slots from the defaults tuple); simple-tail
  sharing with the frame-reuse path.

**Gate: pause for review.** churn_method / churn_tree are the verdict;
target is a meaningful cut of the ~150 ns install cost.

## Phase 4 -- Per-site call cache (investigative, gated on Phase 3)

A `KOS_CACHE_CALL` kind on the landed per-site cache framework: guard
on callable identity, cache the classification verdict + arity. Honest
framing: the callable deref must still happen (rebinding is legal), so
a hit only skips the type check and flag loads -- which Phase 3 already
makes cheap. **Measure first; build only if a real workload shows the
win** (same bar as the other cache-family TODOs). Version-independent
(no CPython internals), all supported Pythons.

## Deferred / adjacent

- **Namespace-free frames** (slot array in the dyn frame tail,
  namespace materialized on demand): the strategic lever against the
  remaining CPython gap; own design note when Phases 1-3 numbers say
  the namespace is what is left.
- **Tail-call promotion bytecode pass**: multiplies these wins; shares
  the Phase 2/3 core.
- **PGO**: re-layouts whatever stays cold; may obsolete inline-promotion
  questions.

## Risks

- The install path is the widest correctness surface in the evaluator:
  generators (agen creation without frame push), demand, partial,
  instance-call self-injection, tail semantics, and every error-path
  refcount. Hence: strict conversion order, the ownership contract
  reviewed per step, and ASAN in every gate.
- The bind-before-push reorder changes the observable state during
  install failures (no pushed frame to unwind). Audit the error paths
  that currently rely on the empty frame existing (e.g. traceback
  position on install-time TypeError).
- **No-frame result delivery** (Phase 1.4 finding): any install class
  that delivers onto the caller's block is ordering-sensitive with the
  borrowed window. Every new install class added to the core must
  declare which side of the drop its result lands on; the review
  checklist for Phase 2 conversions includes this per class.

<!-- The end. -->
