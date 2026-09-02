# Continuation-passing consumer API for Call and Next/Send

Status: archived, implemented 2026-09-02. The entries are
`KosEvalState_CallResult` and `KosEvalState_NextResume`; a resume and
an opcode handler share the `KosThreadFn` prototype; the live transit
is the eval state's pending slot with the frame stack as suspension
storage; an entry reports its outcome and the calling resume ends in
`RESULT_ENTRY`, tail-calling the next resume with a settled value or
threading into an installed callee; a bytecode child's return into a
resume-carrying parent is a tail call from `op_RETURN`. Of the
transport respec below, the declared-destination field and node-hosted
resumes did not land: the resume stays on the frame, and the
destination is read from it. The operational description is
[../EVALUATOR.md](../EVALUATOR.md) §8. The text below is the design as
it evolved, kept for its reasoning.

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

- non-NULL: the operation's result, a new reference the handler owns,
  and **always settled**. A delivery that still needs awaiting is
  awaited at the delivery point before the resume runs, however many
  rounds that takes. A resume never awaits and never tests whether it
  must; that per-consumer duplication is what these entries exist to
  remove.
- NULL with an exception set: the operation failed. The handler may
  recover, or relay by returning `EVAL_RAISE()`, exactly as if it had
  received the NULL from an inline call. Exhaustion arrives this way
  too, as a raised StopIteration.
- NULL with no exception set: not a delivery. A consumer may set a
  resume directly, with nothing staged, to defer its own first step to
  the trampoline -- the cold start a `KOS_ITER_INSTALLED` install
  needs, since `koskari_eval` runs as a separate step after the
  install and never nested inside it. The entries never produce this
  combination.

The handler's return value is the next signal to the loop: another
CPS entry call, `EVAL_RETURN`, `EVAL_RAISE`, `EVAL_AWAIT`, or
`EVAL_REPROLOGUE`. A handler is one trampolined step; anything it
starts goes back through the loop.

A consumer that passes no resume gets today's default behavior:
errors propagate into the raise walk, values ride the existing
delivery paths. Concretely, `resume == NULL` means "behave exactly
like an ordinary opcode": `CallResume` relays `NORMAL`/`REPROLOGUE`
untouched (an installed callee delivers by ordinary frame return; a
plain callable's result was already pushed by `KosEvalState_Call`
itself) and pushes a synchronous `RETURN` value instead of setting a
resume, applying the same ambient-await check every push already
gets. `NextResume` matches `OBJECT_NEXT`'s own existing default
precisely: a delivered element pushes and returns `NORMAL`; an
in-flight `CONTINUES` returns `NORMAL` only if the install already
left a real bytecode frame current (no trampoline hop needed) and
`REPROLOGUE` otherwise; an in-flight `EXIT` still returns `EVAL_AWAIT`,
since revival for a plain frame with no resume set already works the
way ordinary `AWAIT` revival always has -- nothing new is needed on
the revive side. This makes `resume == NULL` a real, load-bearing
calling convention, not just graceful degradation: it lets `CALL_*`,
`NEXT`, and `FOREACH` opcodes route through these same entry points
well before any bytecode-level resume work exists (see "Adoption in
current code" below).

Decided 2026-08-29: that routing is mandated, not optional. Opcodes
use these entries so every delivery crosses the one delivery point --
uniform delivery is how the ambient-await broadening lands
([ambient-await-handoffs.md](ambient-await-handoffs.md)). The settle
at the delivery point is part of the interface contract, so an opcode
joining the entries adopts the broad await rule with it; the original
"zero behavior change" framing of this paragraph holds only until
that broadening is decided and in force.

Never valid from a codeless caller -- exactly as unsafe as any other
plain frame receiving a return or delivery today. `resume == NULL`
asserts "I am (or behave like) a real bytecode frame with a PC to
continue from"; it is a caller contract, not a new safety guarantee
the API adds.

One delivery shape this does *not* need to special-case: a callee
installed directly as the child of a value-graft iteration node (the
`cs_direct` shape `map_iter_step` still uses unconverted) delivers
through `KosIterRequest_Deliver`/`KOS_IWAKE_VALUE` in
`eval_event_return`'s own case 2b, keyed on the parent node's capability
and mode -- entirely upstream of whether any caller has a resume set.
A consumer built on that shape has no separate caller frame for
`CallResume`'s default to intercept in the first place.

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
- in flight: set the resume on the calling frame and let the existing
  machinery deliver -- frame return for an installed callee,
  iteration-node delivery for a driven step, awaited-resume for a
  suspension.

Call semantics by callable shape:

- PLAIN/BOXED python callable: invoke now; stage result-or-NULL.
- KosFunction: install the callee with the resume set on the
  caller's frame; the frame return delivers.
- generator function: construct the AsyncGenerator or demand iter
  immediately (the no-evaluation guarantee stands); stage it.

Next/Send rides KosIterRequest underneath. DELIVERED stages the
value, EXHAUSTED raises StopIteration, CONTINUES sets the resume,
ERROR propagates.
The StopIteration conversion, the recyclable close-versus-stay-open
rule, and the async-generator RuntimeError wrap live inside this
entry and its delivery path, once.

Consumer-visible signal vocabulary after migration: `REPROLOGUE`
(bounce), `AWAIT` (suspend), `RAISE` (propagating), `YIELD`.
`KOS_RES_RETURN` disappears from consumer sight; the value always
arrives through the resume.

## Storage: the resume lives on the frame

Superseded by "Transport respec (normative)": the slot generalizes
from frames to chain nodes. The placement reasoning below still
binds -- read "the consumer's own chain node" for "frame".

The resume is a (KosResumeFn, void *) pair on the KosDynFrame,
replacing what `np_handler` dispatch over an `nf_phase` enum encodes
today. Frame placement is forced by three requirements:

- Nesting. A consumer's callee can itself make a CPS call, so there
  is a stack of pending resumes, one per live call site. The frame
  chain is that stack. A single slot on the eval state or the context
  holds one.
- Suspension. A set resume must survive an await. The chain
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
resume set on a frame, never live across a suspension. Whether it earns
its keep over always staging through the frame stack is a phase-2
benchmark question, not a design commitment.

## The trampoline

Superseded by "Transport respec (normative)": delivery becomes a
register pending pair consumed by a slim bounce; the frame-stack
transit and full prologue pass described here are what the respec
removes.

The eval loop's prologue grows one step: entering a frame that has an
resume set and a staged delivery, clear it into a local,
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

Superseded in part by "Transport respec (normative)": the
one-convergence-point conclusion stands, the staging it converges
through becomes the pending pair, and the settle broadens to every
delivery, elements included
([ambient-await-handoffs.md](ambient-await-handoffs.md)).

Landed, though not at the staging point this section originally
named. Staging happens in several places -- both entries, and any
consumer deferring its own first step -- but every staged delivery
converges on one function before a resume runs, so that is where the
interposition belongs. `eval_step_resume_delivery` pops the staged
value and, if it still needs awaiting, re-sets the same resume and
hands it to `KosEvalState_PushOrAwait`: it settles eagerly and the
prologue re-enters, or it suspends and the revive re-enters, and a
value settling to a further awaitable simply bounces again.

Whether the value came from a python call, a frame return, a
generator step, or an awaitable a python callable handed back, the
handler receives the settled value and never learns which path it
took.

Interposing at each staging site instead was tried first and was
wrong: the entries handed unsettled values to resumes, map and filter
compensated by re-classifying, and that per-consumer duplication is
the exact defect this API exists to remove. A consumer written without
the compensating check got a raw coroutine as a mapped element, or an
always-truthy coroutine as a filter verdict. One convergence point,
not several staging points.

The per-pathway awaitable checks in map and filter are gone, and the
demolition took the typed wake with them.

## Transport respec (normative)

Decided 2026-08-29, superseding the storage, trampoline, and
eager-await sections above where they conflict. Review of the landed
state found the transport routing deliveries by inference -- stack
depth, PyErr state, `CODE`/`RESUME_FN` conjunctions -- and paying a
stack round-trip plus a full prologue pass per delivery, with both
return fast paths excluding resume-carrying parents outright. The
respec makes the destination explicit and the transit
register-borne.

### Destinations

Every operation's continuation is one declared field on the
consumer's chain node, from a closed set:

- PC -- a bytecode frame: push, dispatch at pc. The builtin default.
- RESUME -- a native (fn, state) pair.
- NODE -- an iteration node: the value emits as an element. This is
  `cs_direct` generalized; a node with no resume set means emit.
- ROOT -- koskari_eval returns the value.

Destinations pass downward into the call and iteration machinery,
which delivers to whichever one applies at the arm that materializes
the value. No result kind is reinterpreted upward: the
`KosEvalState_CallResume` switch over `KosEvalState_Call` outcomes
and the NORMAL ambiguity behind apply's guard dissolve at the
source.

### Channels

- The register pair (pending resume, pending value) on the eval
  state: the only live transit, staged and consumed within one loop
  trip.
- Node stacks: storage across suspension only. Freeze converts a
  pending pair to the stack; revive converts it back before anything
  else runs. The stack never carries a live delivery.
- PyErr: the failure channel. The raise walk offers it to RESUME
  destinations as it does today.

Cold start is a staged pair (resume, NULL) placed in the context's
pending slot at install and thawed at entry like any register state.
Every resume invocation is then a staged pair or a walk offer; the
NULL-no-error overload, the StackLen discriminator, and the
`PyErr_Occurred` probing inside resumes all go.

### Flow table

| event | destination | path |
|---|---|---|
| frame return | PC | retarget, push, `EVAL_DISPATCH`; guard is one field test |
| frame return | RESUME | stage pair, slim bounce, invoke -- no stack, no full prologue |
| frame return | NODE | emit as element |
| frame return | ROOT | eval returns |
| call result / element / awaited settle | PC / RESUME | settle (ambient await, exempt types pass), then as above |
| exhaustion | any | StopIteration through the raise walk |
| failure | RESUME | walk offer, NULL + PyErr |
| yield | -- | transits without consulting destinations |

The settle applies to every delivery through these entries, elements
included -- interface contract, per
[ambient-await-handoffs.md](ambient-await-handoffs.md). Opcodes are
mandated onto the entries and adopt it as they join.

Slim bounce: a staged pair with no PyErr and no plan jumps from
staging to invocation without the full prologue.  Pending-exception
and transfer-drain checks exist for chain surgery, and a staged
handoff performs none.

### No mandated frame nodes

The resume slot generalizes from frames to chain nodes. Map and
filter hang their resume on the iteration node they already have; the
callee installs under the node, and the return walk reaching the node
finds the destination there. The codeless frame, its namespace fill,
and the park cache leave their steps, and every return and yield walk
crosses one less node. `KosResumeFrame_Install` remains only for a
consumer with no node of its own, which today is nobody -- apply
keeps its frame for the reasons in "Apply keeps its guard". Open
mechanical question: callee prep reads the live namespace register
the codeless frame's fill supplied; the node-hosted form must name
its namespace source.

### Yield invariant

A resume can intercept returns, deliveries, and failures, never
YIELD. Yield transit cost is independent of resume presence, and the
yield-from passthru work composes with node-hosted resumes instead
of fighting per-step frames.

### Axis rule

A callable's properties are separate declared facts: who runs it (a
KosCallable in the evaluator, a plain python callable without one),
whether it wants a frame, whether its result carries the
direct-return guarantee. No route derives from an unrelated axis --
BOXED derived "python boundary" from "wants no frame" -- and the
invocation route is chosen by the caller's context, never by the
callable's shape. The same rule rejects resume-implies-codeless as a
load-bearing coincidence: the destination is one declared field, not
a conjunction of properties that happen to travel together.

### Asserts

Under `KOS_EVAL_SAFETYNET`: at most one pending pair, consumed
in-trip or converted at freeze; capture never observes a live pair;
a PC destination never carries a resume until bytecode's builtin
resume replaces the implicit default; the destination field is valid
for the node kind.

### Sequencing

The respec lands before any opcode migrates onto the entries: an
element delivered through the interface must cost what its raw push
costs today, which needs the register pair and the slim bounce in
place first. Order: transport rework, then the opcode roster
([ambient-await-handoffs.md](ambient-await-handoffs.md) step 3),
then bytecode's builtin resume at the musttail boundary.

## State lifetime

The void * is borrowed, and nothing else: a pointer into refcounted
state the frame already keeps alive (the provider-object pattern --
KosMap and KosFilter hold their state on objects reachable from the
frame; apply passes NULL and stages its parts on the frame stack).
The resume does no cleanup; abandonment is covered by refcounting.

The owned class this section originally defined
(`KOS_FRAME_OWNS_NATIVE_STATE`, freed by `np_free_state`, cloned by
`np_clone_state`) went with the native-program demolition; nothing
used it by then. It is foreclosed until a consumer needs state the
chain does not keep alive. Reintroduction means a descriptor with
free and clone entries riding the resume -- never a bare allocation
the resume is trusted to free, because two paths bypass any
error-mode-cleans-up convention: a parked continuation abandoned
without resuming never invokes its resume (the coroutine tp_finalize
problem), and a cloned frame duplicates the pointer, turning
resume-owned cleanup into a double free.

## Continuation capture

As implemented, capture across a frame carrying a resume refuses by
name (`kos_dynframe_clone`): no caller produces a resume_state a
clone could copy, so there is no clone story to get wrong. For map
and filter this is behavior parity -- their native programs refused
capture too, deliberately, since replaying a captured step would
re-consume source elements.

Clone-as-data remains the intended model for a resume whose state is
clonable: the slot copies with the frame, every materialization
carries the resume, and the trampoline's destructive clear is correct
per materialization -- the live copy spends its own, the snapshot
keeps its own, and a continuation reinstated N times fires the resume
N times with no once-only bookkeeping. Landing it requires the owned
channel above for per-copy state, and these rules, which hold now and
then:

- Borrowed state is shared across reinstatements, not snapshotted.
  Resuming a captured continuation twice observes the provider state
  wherever the first run left it. The iteration nodes already clone
  with a shared `REQ->iter`, so multi-shot over stateful iteration is
  already observably stateful; this is the standing rule, stated.
- A staged-but-unconsumed delivery is a value on the frame stack; it
  clones with the frame and each reinstatement re-delivers it.
- The eval-state bounce must be invisible to capture. Capture happens
  inside calls, by which point any bounce has settled into a consumed
  delivery or a frame carrying a resume. Assert under
  `KOS_EVAL_SAFETYNET`.
- Resume functions are re-entrant with respect to their state: no
  already-ran flags outside cloned owned state.

## Adoption in current code

- map and filter: **landed as described.** The `nf_phase` enums and
  the wake-dispatch switches became resume functions; filter's
  FETCH/INVOKE/VERDICT progression is carried by which resume is set,
  so the phase field disappeared rather than being translated. Both
  lost the three-way result ladder and the RETURN staging. One thing
  the description missed: a rejected element in filter loops by
  bouncing through the trampoline once per element, where the wake
  protocol looped in a flat C `for(;;)`. Stack safety is preserved --
  the bounce is what preserves it -- but a long reject run does more
  work per element.
- apply: **superseded -- apply is not a conversion target.** The
  flatten arm's frame-identity check cannot disappear, and apply must
  not set a resume on its own frame at all. See "Apply keeps its
  guard" below.
- demand: `demand_sync_ex`'s post-call interpretation is consolidated,
  but **not onto a `KosResumeFn`** -- see "Demand classifies, it does
  not resume" below. The DEMAND-flag frame drive is unchanged, as
  stated.
- stepper and demandgen adapters: **the Next entry cannot serve them**
  -- both call `koskari_eval` themselves and have no frame to stage a
  delivery on, the same shape demand has. What they actually duplicate
  is the drive epilogue, and that is what gets extracted. See "The
  drivers share an epilogue, not an entry" below.
- `KosIterRequest_Next` in-eval consumers (NEXT, foreach, native
  RequestNext): the request sidecar becomes the implementation layer
  under `KosEvalState_NextResume`; iteration nodes deliver into
  resumes set on frames instead of through per-node-type deliver
  functions.
- bytecode frames: a bytecode frame's resume is one builtin --
  push the value, re-enter dispatch at the PC. Once it exists, CALL
  and the await opcodes go through the same CPS entries native code
  uses, and suspension handling is identical for bytecode and native
  frames. This is the boundary with the musttail branch: opcode
  handlers and resume functions share a calling convention, and
  converting dispatch from computed goto to tail calls becomes a
  calling-convention change.

Deleted: `KosNativeFrameEvent` and the typed wake, `np_handler`, the
phase enums in map and filter, and `KosNativeFrame_Install`. The drive
epilogue turned out to be two epilogues, not four copies of one --
see "The drivers share an epilogue, not an entry". `KOS_RES_RETURN`
still has a consumer-facing role: `CallResume` interprets it, and
apply and values still return it.

### Apply keeps its guard

Two things this document originally asked of apply are not achievable,
established while converting filter and demand:

**The frame-identity check is load-bearing.** `kos_vinstall_function`
never pushes a frame -- it fills the current one in place, and its
generator-function branch pushes a value and returns `KOS_RES_NORMAL`.
So a `NORMAL` out of apply's inner install is ambiguous: either "frame
filled, real bytecode to run" or "value delivered, apply's code-less
frame has nothing to dispatch." Only apply can tell them apart, by
checking `CODE`/`NATIVE_PROG` on its own captured frame pointer. No
generic entry point can absorb that, because every other caller reaches
`kc_vinstall` through `do_frame_prep` and is guaranteed a frame with
code.

**Apply must not set a resume on its own frame.** The prologue checks
`resume_fn` before dispatching bytecode, so a resume set on a frame the
callee just filled would run *instead of* that callee's body. And
`values_dispatch` pops apply's frame before re-installing, so after
some calls the captured pointer is freed -- comparing it (what the
guard does) is fine, writing to it is not.

Apply's completion is therefore synchronous in every reachable case,
which leaves nothing for a resume to do. Its remaining arms -- relaying
`RETURN`, `RAISE`, and `FATAL` -- are correct as they stand, not
divergent interpretations to unify. Apply was measured against a direct
call for a partial over a coroutine function and behaves identically,
confirming it adds no ambient-await divergence of its own.

### The drivers share an epilogue, not an entry

`stepper_drive` and `demandgen_iternext` (koskari/lib/iterate.c) are
Python-boundary drivers: each calls `ki_step`, then calls
`koskari_eval` itself. They have no current frame, so
`KosEvalState_NextResume` -- which stages onto one -- cannot be their
entry, exactly as with demand.

Their real duplication is the epilogue after `koskari_eval` returns:
StopIteration-to-exhaustion conversion, truncation versus close,
excluding a thrown StopIteration from truncation, and the "async
generator raised StopIteration" RuntimeError wrap. Three near-identical
copies. Extracting one helper both drivers call is the fix, and is what
the motivation section means by the copies of the drive epilogue.

### A note on this document's prescriptions

Three of the per-consumer prescriptions above (apply, demand, the
drivers) named a mechanism the code does not permit, while correctly
naming the duplication to remove. The pattern in each case: a
`KosResumeFn` runs at the delivery boundary, on a frame, inside the
loop. A consumer that has no frame, or that must act before the loop
starts, or that must refuse a suspension rather than receive its
result, cannot be served by one no matter how the entry is shaped.
Check that a consumer is actually in-eval with a frame before reaching
for these entries.

### Demand classifies, it does not resume

`demand_sync_ex` installs on a fresh context where there is no caller
frame, then drives `koskari_eval` itself. A `KosResumeFn` runs at the
*delivery* boundary, after a suspension has already been permitted;
`KosEvalState_CallResume`'s AWAIT arm sets a resume and relays
`EVAL_AWAIT` rather than offering the caller a veto. A demand root must
never suspend and has no channel to carry `AWAIT` out, so it has to
intercept at the *classify* boundary instead -- one shared classifier
called synchronously, not a resume. The CPS entry points are
deliberately unused here.

## Pitfalls and solutions

- Inline invocation reintroduces unbounded recursion. Solution: the
  trampoline contract; the API stages and bounces, never calls the
  resume on the caller's stack. Safetynet assert on re-entry depth.
- Handler-owned cleanup leaks on abandonment and double-frees on
  clone. Solution: the two state classes above; no third class
  exists.
- The bounce fast path observable to capture. Moot: the fast path was
  never built, so every delivery stages through the frame stack.
- Hot-path regression. Measured, none found. The sync fast paths in
  both providers are untouched, and the driven paths came in within
  noise of the wake protocol. `cs_direct` stays unconverted, so map's
  graft arm still delivers by plain frame return.
- **A missed pitfall, found the hard way: a return fast path that
  bypasses the prologue also bypasses the resume check.**
  `kos_return_parent_is_plain` and `kos_return_is_extent_plain` return
  `EVAL_DISPATCH` and jump straight to the next instruction, which for
  a resume-carrying (and therefore codeless) parent meant dispatching
  a NULL code pointer. Both now exclude such a parent. Any future
  path that skips the prologue must do the same.
- Two consumer models coexisting mid-migration. Solution: the wake
  protocol and the resume slot are orthogonal frame features; a frame
  uses one or the other, asserted never both. Migration order keeps
  each phase reviewable under the size cap.
- Errors arriving at a handler that cannot act. Solution: the relay
  convention -- return `EVAL_RAISE()` untouched -- is the documented
  default body, and the no-resume form remains for consumers with no
  recovery behavior at all.

## Migration order

Landed:

1. Plumbing, inert: resume slot, setting API, trampoline invocation in
   the prologue and the raise-walk offer. Nothing set; zero behavior
   change; safetynet assert that a frame never carries both a native
   program and a resume.
2. KosMap end-to-end. Two pre-existing return fast paths
   (`kos_return_parent_is_plain`, `kos_return_is_extent_plain`) had to
   learn to exclude a resume-carrying parent: both bypass the prologue
   with `EVAL_DISPATCH`, which skipped the resume check and dispatched
   a codeless frame.
3. KosFilter. Apply and demand turned out not to be conversion
   targets; see their sections above.
4. Eager await centralized at the delivery point. This was a
   correction, not planned work: the entries had been handing
   unsettled values to resumes, and map and filter compensated by
   re-classifying -- reproducing the very duplication these entries
   exist to remove.
5. The BOXED call shape retired. `kc_wants_frame == 0` had been read as
   "invoke through the Python boundary", forcing a fresh context and a
   nested loop for calls the running evaluator could make.

6. The stepper's truncation check and error mapping shared between its
   two sites. `demandgen_iternext` keeps its own: on the sync surface
   StopIteration is the protocol's end signal rather than something to
   wrap, and a root exhaustion closes the provider instead of
   truncating one drive.
7. Demolition. Apply's non-flattening path became a resume, which left
   nothing installing a native program, so `KosNativeProgram`, the
   typed wake, `KosNativeHandlerFn`, `KosNativeFrame_Install` and the
   frame's `native_prog` slot are all gone. `native_state` survives as
   `park_cache`, the role it had been reduced to.

Remaining:

8. The transport respec (normative section above): declared
   destinations, the register pending pair, node-hosted resumes, the
   slim bounce. Lands before any opcode migrates onto the entries.
9. The opcode roster onto the entries
   ([ambient-await-handoffs.md](ambient-await-handoffs.md)), then
   bytecode's builtin resume and opcode unification -- the boundary
   with the musttail work.

Each phase lands green through the full gate set (suite, native,
ASAN, valgrind, benchmarks) and stays under the review size cap so it
is reviewed as it lands. gdb is diagnostic, never a gate.

## Open questions

- Exhaustion never took the NULL-no-error shape this document
  proposed. `KosEvalState_NextResume` raises StopIteration on
  EXHAUSTED and lets the raise walk offer it, so a resume sees NULL
  only with an exception set, and the "did it end or fail" check
  inside every Next handler never appeared. The StopIteration payload
  question goes with it: `KosIterRequest_Next` still hands the
  generator return value through `fast_out`, and no resume needs a
  payload channel yet. Revisit only if one does.
- Whether the bounce fast path (a `(resume, value)` pair handed off
  within one trampoline iteration, skipping the frame stack) earns
  its complexity. Not built. Every delivery currently stages through
  the frame stack, which is the canonical model this document
  prescribes; the fast path stays a measured optimization if the
  trampoline work in phase 8 wants it.
- Whether `cs_direct` -- map's graft arm, where the callee's own frame
  return is the emitted element -- should join the resume model. It
  installs no frame of its own, so it would need a root-level slot
  this document only gestures at. Unconverted, and correct as it
  stands.
