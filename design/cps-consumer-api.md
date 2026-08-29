# Continuation-passing consumer API for Call and Next/Send

Status: design only. Nothing here is implemented. Target is a fresh
branch after the native-map branch merges; that branch's provider
suite, native frame programs, and call-shape machinery are the
substrate this design assumes.

## Motivation

The provider side of the iteration and call interfaces is a small
vtable with a crisp undo-on-error contract, and building KosFilter
from KosMap proved it reusable. The consumer side is not symmetric.
A consumer making a call through `KosEvalState_Call*` must interpret
three success shapes -- `KOS_RES_NORMAL` (frame installed, value
arrives by frame return), `KOS_RES_REPROLOGUE`, and `KOS_RES_RETURN`
(value already in hand) -- and a consumer driving iteration
re-implements the drive epilogue by hand: StopIteration-to-exhaustion
conversion, close-versus-stay-open, throw-flavor exclusion, the
RuntimeError wrap. The second review round of the native-map branch
found defects that were, without exception, divergences among these
hand-rolled interpreters: map and filter not recognizing `RETURN`,
the stepper closing a provider where `KosIterRequest_Next` left it
open, demandgen missing the conversion entirely.

The fix is one delivery channel. The consumer passes the code that
receives the result; the machinery guarantees that code runs exactly
once per materialization with the settled value, however the value
was produced -- synchronous call, frame return, generator step, or an
awaitable that had to be awaited first. Handlers stop interpreting
result kinds, stop duplicating drive epilogues, and stop knowing
whether the operation was async. Ambient await becomes a property of
the delivery point instead of a per-pathway reimplementation.

This also aligns with the planned musttail migration of opcode
dispatch: a resume function and a tail-called opcode handler are the
same shape (function taking state, returning the next signal), so the
eval loop's role as suspend/resume trampoline is shared between them.

## The resume contract

```c
typedef KosOpResult (*KosResumeFn)(
    KosEvalState *es,
    void *state,
    PyObject *value);
```

The value argument carries the CPython calling convention:

- non-NULL: the operation's result, a new reference the handler owns.
- NULL with an exception set: the operation failed. The handler may
  recover, or relay by returning `EVAL_RAISE()`, exactly as if it had
  received the NULL from an inline call.
- NULL with no exception set (Next/Send deliveries only): exhaustion,
  the `PyIter_Next` convention. Call deliveries never produce this.

The handler's return value is the next signal to the loop: another
CPS entry call, `EVAL_RETURN`, `EVAL_RAISE`, `EVAL_AWAIT`, or
`EVAL_REPROLOGUE`. A handler is one trampolined step; anything it
starts goes back through the loop.

A consumer that passes no resume gets today's default behavior:
errors propagate into the raise walk, values ride the existing
delivery paths.

## Public entry points

```c
KosOpResult KosEvalState_CallResume(
    KosEvalState *es,
    PyObject *callable,
    PyObject *const *args,
    Py_ssize_t nargs,
    PyObject *kwnames,
    KosResumeFn resume,
    void *state);

KosOpResult KosEvalState_NextResume(
    KosEvalState *es,
    PyObject *iter,
    PyObject *send_val,
    KosResumeFn resume,
    void *state);
```

(Throw-flavored drives keep riding the raise walk; a thrown exception
is chain-level, not a delivery.)

Every arm of both entries converges on one of two outcomes:

- value ready now: stage the delivery, return to the trampoline. The
  handler runs on the next loop iteration.
- in flight: arm the resume on the calling frame and let the existing
  machinery deliver -- frame return for an installed callee,
  iteration-node delivery for a driven step, awaited-resume for a
  suspension.

Call semantics by callable shape:

- PLAIN/BOXED python callable: invoke now; stage result-or-NULL.
- KosFunction: install the callee with the resume armed on the
  caller's frame; the frame return delivers.
- generator function: construct the AsyncGenerator or demand iter
  immediately (the no-evaluation guarantee stands); stage it.

Next/Send rides KosIterRequest underneath. DELIVERED stages the
value, EXHAUSTED stages NULL-no-error, CONTINUES arms, ERROR stages
NULL-plus-exception when a resume is armed and propagates when not.
The StopIteration conversion, the recyclable close-versus-stay-open
rule, and the async-generator RuntimeError wrap live inside this
entry and its delivery path, once.

Consumer-visible signal vocabulary after migration: `REPROLOGUE`
(bounce), `AWAIT` (suspend), `RAISE` (propagating), `YIELD`.
`KOS_RES_RETURN` disappears from consumer sight; the value always
arrives through the resume.

## Storage: the resume lives on the frame

The armed resume is a (KosResumeFn, void *) pair on the KosDynFrame,
replacing what `np_handler` dispatch over an `nf_phase` enum encodes
today. Frame placement is forced by three requirements:

- Nesting. A consumer's callee can itself make a CPS call, so there
  is a stack of pending resumes, one per live call site. The frame
  chain is that stack. A single slot on the eval state or the context
  holds one.
- Suspension. The armed resume must survive an await. The chain
  freezes with the context; a resume on its frame is captured for
  free. The eval state is the transient register view and anything
  there must be written back into the chain at suspension anyway.
- Capture. Frames clone for continuations; the resume slot is plain
  data in the frame image. See "Continuation capture" below.

Co-location also removes targeting logic: the return machinery
delivers to the parent frame's stack, and the resume that consumes a
delivery is always that frame's own. The prologue check is purely
local. `KOS_FRAME_PARK_TO_CACHE` and `KOS_FRAME_OWNS_NATIVE_STATE`
made the same placement decision for the same reasons.

The eval state keeps one narrow role: the in-flight bounce, a
(resume, value) pair handed off within a single trampoline iteration
on the fully synchronous path. It is register state -- consumed in
the same loop trip or converted by the await interposition into an
armed frame resume, never live across a suspension. Whether it earns
its keep over always staging through the frame stack is a phase-2
benchmark question, not a design commitment.

## The trampoline

The eval loop's prologue grows one step: entering a frame that has an
armed resume and a staged delivery, clear the arm into a local,
invoke the resume, and feed its returned signal into the same
processing opcode results receive. REPROLOGUE re-runs the prologue,
RETURN rides the return machinery, RAISE enters the walk, AWAIT
suspends out to the adapter.

The API never invokes a resume on the caller's C stack. A synchronous
loop written naively in CPS -- a filter rejecting 200,000 consecutive
elements, each rejection fetching the next -- must cost constant
stack. Inline invocation would reintroduce the deep-reject recursion
crash as an API property; the trampoline bounce is what preserves the
evaluator's stacklessness by construction.

## Eager await at the delivery point

Any value about to be staged that is awaitable instead arms the
resume and returns `EVAL_AWAIT`; the suspension surfaces to the
adapter, and the awaited result is what finally reaches the handler.
This interposition happens at the staging point and nowhere else.
Whether the value came from a python call, a frame return, a
generator step, or an awaitable a python callable handed back, the
handler receives the settled value and never learns which path it
took. The `KOS_NFRAME_AWAITED` wake, the adapter await arms, and the
per-pathway awaitable checks collapse into this one site.

## State lifetime

The void * is one of two classes, and nothing else:

- Borrowed: a pointer into refcounted state the frame already keeps
  alive (the provider-object pattern -- KosMap and KosFilter hold
  their state on objects reachable from the frame). The handler does
  no cleanup; abandonment and cloning are covered by refcounting.
- Owned: rides `KOS_FRAME_OWNS_NATIVE_STATE`, freed by
  `np_free_state`, cloned by `np_clone_state`. The handler treats a
  NULL delivery as act-then-relay; teardown covers the paths the
  handler never sees.

Never a bare allocation the handler is trusted to free. The
error-mode-cleans-up convention only covers paths where the handler
runs; two paths bypass it. A parked continuation abandoned without
resuming never invokes its handler (the coroutine tp_finalize
problem), and a cloned frame duplicates the pointer, turning
handler-owned cleanup into a double free. Both are already handled by
the descriptor slots; the rule routes all state through them.

## Continuation capture

Capture clones frames. The resume slot clones as data, so every
materialization carries the arm, and the trampoline's destructive
clear is correct per materialization: the live copy spends its arm,
the snapshot keeps its own, and a continuation reinstated N times
fires the resume N times with no once-only bookkeeping.

Consequences:

- Borrowed state is shared across reinstatements, not snapshotted.
  Resuming a captured continuation twice observes the provider state
  wherever the first run left it. The iteration nodes already clone
  with a shared `REQ->iter`, so multi-shot over stateful iteration is
  already observably stateful; this is the standing rule, stated.
- Owned state is per-copy via `np_clone_state`; N materializations
  mean N teardowns and N frees. A resume with owned state and no
  clone hook refuses capture with an error at capture time (the NULL
  `np_clone_state` rule today).
- A staged-but-unconsumed delivery is a value on the frame stack; it
  clones with the frame and each reinstatement re-delivers it.
- The eval-state bounce must be invisible to capture. Capture happens
  inside calls, by which point any bounce has settled into a consumed
  delivery or an armed frame. Assert under `KOS_EVAL_SAFETYNET`.
- Resume functions are re-entrant with respect to their state: no
  already-ran flags outside cloned owned state.

## Adoption in current code

- map and filter: `nf_phase` enums and the wake-dispatch switches in
  the nf handlers become resume functions -- filter's
  FETCH/INVOKE/VERDICT states are three resumes (or one loop driver
  re-armed per step). The sync FRAME arms lose the three-way result
  ladder; the step arms lose the RETURN staging added by the round-2
  fixes.
- apply: the flatten arm's frame-identity check and value pop
  disappear; apply passes its own completion resume.
- demand: `demand_sync_ex`'s post-call interpretation becomes a
  resume; the DEMAND-flag frame drive is unchanged.
- stepper and demandgen adapters: retarget onto the Next entry and
  delete their hand-rolled ERROR/RAISE conversion arms; the epilogue
  lives inside delivery.
- `KosIterRequest_Next` in-eval consumers (NEXT, foreach, native
  RequestNext): the request sidecar becomes the implementation layer
  under `KosEvalState_NextResume`; iteration nodes deliver into
  armed resumes instead of through per-node-type deliver functions.
- bytecode frames: a bytecode frame's resume is one builtin --
  push the value, re-enter dispatch at the PC. Once it exists, CALL
  and the await opcodes go through the same CPS entries native code
  uses, and suspension arming is identical for bytecode and native
  frames. This is the boundary with the musttail branch: opcode
  handlers and resume functions share a calling convention, and
  converting dispatch from computed goto to tail calls becomes a
  calling-convention change.

Deleted at the end: `KosNativeFrameEvent` and the typed wake,
`np_handler`, the phase enums in map and filter, the non-CPS consumer
entries, the four copies of the drive epilogue, and
`KOS_RES_RETURN`'s consumer-facing role.

## Pitfalls and solutions

- Inline invocation reintroduces unbounded recursion. Solution: the
  trampoline contract; the API stages and bounces, never calls the
  resume on the caller's stack. Safetynet assert on re-entry depth.
- Handler-owned cleanup leaks on abandonment and double-frees on
  clone. Solution: the two state classes above; no third class
  exists.
- The bounce fast path observable to capture. Solution: invariant
  plus safetynet assert; the frame-staged path is the canonical
  model and the bounce an optimization proven equivalent.
- Hot-path regression. The cs_direct frameless map arm currently
  delivers by plain frame return; the CPS path trades the phase
  switch for an indirect call plus a loop bounce. Phase 2 benchmarks
  (maptime, filtertime, churn) against the native-map baseline decide
  whether the bounce fast path is needed. A regression beyond noise
  on the sync map path blocks the migration until recovered.
- Two consumer models coexisting mid-migration. Solution: the wake
  protocol and the resume slot are orthogonal frame features; a frame
  uses one or the other, asserted never both. Migration order keeps
  each phase reviewable under the size cap.
- Errors arriving at a handler that cannot act. Solution: the relay
  convention -- return `EVAL_RAISE()` untouched -- is the documented
  default body, and the no-resume form remains for consumers with no
  recovery behavior at all.

## Migration order

1. Plumbing, inert: resume slot, arming API, trampoline invocation in
   the prologue. Nothing armed; zero behavior change; safetynet
   asserts in place.
2. KosMap end-to-end as the proving ground, with benchmarks against
   the native-map baseline. KosFilter stays on the wake protocol to
   prove coexistence.
3. KosFilter, apply, demand.
4. Stepper and demandgen onto the Next entry; delete their epilogues.
5. Eager-await centralization at the staging point; delete the
   per-pathway awaitable interpretation.
6. Demolition: typed wake, np_handler, phase enums, non-CPS entries.
7. Bytecode's builtin resume and opcode unification -- either this
   branch's last phase or the musttail branch's first.

Each phase lands green through the full gate set (suite, native,
ASAN, valgrind, gdb env, benchmarks) and stays under the review size
cap so it is reviewed as it lands.

## Open questions

- Exhaustion via NULL-no-error mirrors `PyIter_Next` but makes "did
  it end or fail" a `PyErr_Occurred()` check inside every Next
  handler. An alternative is a separate exhaustion resume; that is a
  second function pointer per arm and was rejected above for signal
  count, but the ergonomics deserve one pass during phase 2.
- Whether the bounce fast path earns its complexity (phase-2
  benchmark decision).
- Where the StopIteration payload (generator return value) surfaces
  in the Next contract: today `KosIterRequest_Next` hands it through
  `fast_out` on EXHAUSTED; NULL-no-error has no payload channel.
  Candidates: a per-frame last-exhaustion slot, or accepting that
  resumes needing the payload use a distinct arm.
