# GEN Yield-From Passthru (Proposal)

**Status: PROPOSAL -- a recorded possibility, undecided and not
scheduled.**  This document evaluates the idea; nothing here is
committed work.  If rejected, archive with a note; if accepted, split
into staged implementation specs.

**Note (2026-09-04):** the per-hop cost in section 2 predates the
node-style yield-from in [electors.md](electors.md). The delegating
frame no longer re-dispatches the YIELD_FROM opcode or decodes a phase
machine; each hop is now the freeze, one prologue bounce, and the
armed phase resume. What passthru would still remove is the freeze and
the bounce themselves, so re-evaluate it against that residue.

Companion docs:
- [../archive/boundary-control-reemit.md](../archive/boundary-control-reemit.md)
  (archived; the implemented boundary model this would extend)
- [../archive/dispatch-sizing.md](../archive/dispatch-sizing.md) (YIELD_FROM size history)
- [evaluator.md](evaluator.md)
- [../archive/cps-consumer-api.md](../archive/cps-consumer-api.md) (the NEXT/ITER_NEXT
  resume machinery this proposal's stepping unification lands on; see §10)
- [stencil-compilation.md](stencil-compilation.md) (why shrinking YIELD_FROM
  matters beyond this opcode)


## 1. The idea

Put a `yieldfrom` field directly on the GEN dynnode.  Re-entering a
generator whose GEN carries the field dispatches straight through to the
delegate -- the generator's own frame is not thawed.  The frame is
reconstructed only when the delegate raises or completes: thaw at the
yield-from position and raise/complete from that point.

The enabling invariant: **a generator suspended at YIELD_FROM keeps its
frozen frame untouched until the delegation ends.**  Nothing in the frame
changes while values shuttle between the consumer and the delegate; only
the pending delegation state mutates.  Passthru simply acts on that.

This is the delegation optimization CPython lacks (each value through a
`yield from` chain resumes every intermediate frame; PyPy special-cases
it), generalized to Koskari's dyn-chain model.


## 2. What it replaces

Today each value through a delegating generator costs, per hop: thaw the
delegating frame, run the prologue, re-dispatch the YIELD_FROM opcode,
decode the phase machine (including the EXPECT_SEND stack-pop dance),
step the delegate, walk/freeze again.  Flattened Koskari delegates
additionally ping-pong through `KOS_YF_PHASE_EMIT`
(`kos_dyn_yield_from_deliver_inner_yield` hands the value to the
delegating frame, which re-emits it as its own yield on redispatch).

Under passthru:

- **Entry** (send/next into the generator): see `GEN.yieldfrom`, step the
  delegate directly with the send value.  No thaw, no opcode dispatch,
  no phase decode, no stack juggling -- the send value arrives as
  `send_val` at entry and goes straight to the delegate.
- **Delegate yields**: the value passes back out through the (still
  frozen) delegating generator.  Nested delegation is a pointer walk
  down the GEN chain, not a cascade of frame resumes.
- **Delegate raises/ends**: reconstruct -- thaw the delegating frame at
  the yield-from PC, deliver the completion or route the exception
  through `process_raise` so the frame's try nodes see it normally.
- **throw/close**: PEP 380 ordering agrees with this split -- both go to
  the delegate first; the delegating frame participates only when the
  delegate raises or finishes.

The YIELD_FROM opcode retains first-entry normalize + field install and
the completion/raise reconstruction target.  The per-kind stepping
machinery (already decomposed into shared helpers, 2026-07-18) becomes
eval/dynchain-owned delegate stepping, shared with NEXT / ITER_NEXT.


## 3. Prerequisites (already landed)

- **Generic boundary dispatcher** (`dynchain/dyn_boundary.c`): routing
  keys on `KOS_DYN_CAP_RECEIVES_ITER_BOUNDARY` only.  A GEN carrying
  `yieldfrom` joins by carrying the capability and adding a delivery
  case; the finder and the eval loop do not change.
- **Drive-family decompose**: the yield-out / exhaustion / harvest /
  await-resume motifs are shared noinline helpers a GEN-entry path can
  call directly.


## 4. Complexity by delegate situation

The cost/benefit is very uneven across delegate kinds.  Foreign
delegates are nearly trivial; the flattened Koskari delegate is where
all the hard questions live.

| delegate kind | step mechanism | passthru complexity | payoff |
|---|---|---|---|
| sync iterator (`KOS_YF_SYNC_ITER`) | `PyIter_Next` call | trivial: self-contained call; no send, close is a no-op | full: zero frame work per value |
| sync generator (`KOS_YF_SYNC_GEN`) | `send`/`throw`/`close` calls | easy: self-contained calls; StopIteration harvest at the boundary | full |
| Python async iter/gen (`KOS_YF_PY_ASYNC_*`) | build awaitable, eager-step, may suspend | medium: the in-flight await must resume off the field, not a frame PC (§6.2) | full, once resume is field-keyed |
| flattened Koskari async gen (`KOS_YF_KOS_ASYNC_GEN`) | attach delegate frame; runs in the same eval loop | hard: chain topology, lazy reconstruction cascade, extent visibility (§5) | partial: the innermost delegate still thaws/freezes per value; passthru saves the *intermediate* hops and the EMIT ping-pong, so the win grows with nesting depth |

For the three foreign kinds, the frozen delegating frame is inert
throughout: "step the delegate" is a self-contained CPython call that
returns a value or an exception.  Nothing else in the evaluator needs to
know delegation is happening.


## 5. The flattened Koskari delegate

### 5.1 Chain topology: who hosts the running inner frame

The delegate's GEN/frame must be installed and executing while the
delegating generator stays frozen.  Two options:

- **(a) Frozen-on-chain passthru nodes**: intermediate GENs sit on the
  active chain in a new "installed but frozen" state.  Every chain
  walker (freeze walk, unwind, transfer goals, snapshot, dyn_walk) must
  learn the state.  Rejected direction: it is exactly the half-alive
  node state the evaluator has been eliminating.
- **(b) Direct attach + side bookkeeping** (preferred): attach the
  innermost delegate's GEN directly into the real consumer's extent --
  where the YIELD_FROM node sits today -- and keep the passthru linkage
  purely as GEN-field bookkeeping consulted at raise/end/close time.
  The active chain preserves its invariant (installed implies live),
  and from the dispatcher's point of view the situation is
  indistinguishable from a NEXT-style boundary driving one generator.

### 5.2 Lazy reconstruction cascade

With (b), completion and raise become: thaw the *immediate* parent
delegating gen at its yield-from PC, splice its GEN back into the chain
in place of the delegate, deliver there.  If that frame does not handle
a raise, propagation reaches the next passthru GEN and repeats -- one
thaw per level, only on the unhappy path.  The chain surgery is the same
species as `kos_dyn_gen_freeze_and_release` and the attach helpers; the
raise side belongs to the unified `process_raise` ingress.

### 5.3 Extent visibility (semantic hazard, not mechanical)

Today, because the whole delegation stack is flattened live, a
delegating generator that establishes parameterize or dyn-wind before
its yield-from has those extents active while the delegate runs.  Freeze
the intermediate and the delegate no longer sees them.

- **try nodes are fine**: lazy reconstruction delivers exceptions to
  them correctly, just later.
- **parameterize / wind are not**: skipping per-crossing extent actions
  or hiding parameter bindings is an observable behavior change.

Guard: at field-install time, scan the generator's chain segment between
its GEN node and the suspension point; any wind or parameterize marks
the gen **no-passthru** and it falls back to the current path.  Plain
`(yield-from x)` -- the overwhelming case -- stays on the fast path.


## 6. Design questions to settle before building

1. **State home.**  The KosDynYieldFrom node currently holds delegate,
   kind, phase, pending arg/type, inflight.  One field on GEN suffices
   (a frame suspends at exactly one PC, so at most one active delegation
   per generator; nesting is a chain of GENs).  Decide: fold the state
   into GEN, or hang the existing payload struct off it.  Folding
   retires the KOS_DYN_YIELD_FROM node kind and its whole lifecycle
   (push/pop/unlink/clone/walk cases).
2. **In-flight await resume.**  For Python async delegates the
   suspension currently records a rolled-back frame PC and resume
   re-dispatches the opcode.  Under passthru the await belongs to no
   frame: resume must key off the field's INFLIGHT state at generator
   entry.  Subtlest piece -- demand_sync denial and the
   flush/mark-dead-after-yield discipline included.
3. **Continuation capture.**  The yf clone logic moves to GEN cloning;
   captured chains must see the field.  Same delegate-sharing caveats as
   today for foreign delegates under multi-shot -- no regression, but
   state it.
4. **Completion value.**  The current implementation discards the
   delegate's StopIteration value (void completion).  The
   reconstruction point is where that decision gets implemented exactly
   once if yield-from is ever to evaluate to the delegate's return
   value.


## 7. Pros and cons

Pros:

- Per-value hot path drops thaw + prologue + opcode dispatch + phase
  decode + freeze for every delegation hop (foreign delegates), and all
  intermediate hops plus the EMIT ping-pong (flattened chains).
- Send-value plumbing simplifies: entry `send_val` goes straight to the
  delegate instead of the EXPECT_SEND stack dance.
- YIELD_FROM shrinks to normalize/install + reconstruction target;
  stepping unifies with NEXT / ITER_NEXT machinery.
- Potentially retires the KOS_DYN_YIELD_FROM node kind entirely.
- Aligns with the capability-keyed dispatcher: a passthru GEN is just
  another receiver carrying the cap.

Cons / costs:

- A new resume path keyed off field state rather than frame PC (§6.2)
  -- the highest-risk piece, touching await freeze/thaw discipline.
- Reconstruction machinery (thaw-at-PC + chain splice + re-raise
  cascade) that exists today only implicitly.
- The extent-inert guard adds install-time analysis and a second
  (fallback) execution path to keep correct forever.
- Introspection/snapshot surfaces must learn the field.
- Payoff is partial for the flattened kind at depth 1 (the innermost
  delegate still thaws per value); the full win needs nesting or
  foreign delegates.
- Testing burden: delegation x {send, throw, close, await, callcc,
  transfer goals} x {passthru, fallback} matrix.


## 8. Staging (if accepted)

1. **Stage 1 -- foreign delegates only** (sync iter, sync gen, py
   async).  No topology questions, no extent hazard (foreign delegates
   cannot see Koskari extents), frozen frame inert throughout.
   Delivers most of the opcode shrink and all of the foreign per-value
   win.  Requires §6.1 and §6.2.
2. **Stage 2 -- flattened Koskari delegates.**  Topology option (b),
   the reconstruction cascade, the extent-inert guard (§5).  Requires
   everything in §6.

Each stage lands independently green against the existing yield-from
suites (`tests/lib/opcode/test_yield_from.py`,
`tests/reproducers/test_yield_from_koskari_gen.py`, native eval tests).
Benchmark with the `yieldfrom` churn case plus a new nested-depth
variant -- depth scaling is where passthru should separate from the
current implementation.


## 9. Non-goals

- No language-level semantics change for delegation (the §5.3 guard
  exists precisely to preserve extent semantics).
- No new bytecode opcodes.
- No caching of the innermost delegate pointer across sends; the active
  chain and the field chain stay the source of truth (no stale-frame
  targeting, per the archived boundary doc's invariants).


## 10. Relationship to CPS consumer API and stenciling

The per-delegate-kind stepping this proposal unifies onto NEXT / ITER_NEXT
machinery is the same stepping [../archive/cps-consumer-api.md](../archive/cps-consumer-api.md)
formalizes as `KosEvalState_NextResume`: DELIVERED/EXHAUSTED/CONTINUES/ERROR
handled once, at the entry, instead of once per delegate kind inside
YIELD_FROM. Landing this proposal on top of that entry rather than the
current hand-rolled phase machine is what makes §7's "YIELD_FROM shrinks to
normalize/install + reconstruction target" literal, not aspirational.

That shrink matters past this opcode. [stencil-compilation.md](stencil-compilation.md)
excludes `KOS_OP_HANDLER_COLD` handlers from stenciling; YIELD_FROM's current
size is why it carries that designation. Every opcode being stencilable is
the stated end goal, and CPS-based slimming -- moving delegate-stepping into
shared, already-stenciled infrastructure instead of reimplementing it
in-opcode -- is the mechanism for getting YIELD_FROM (and any other COLD
opcode carrying similar bulk for the same reason) off that list, not a
special case argued for the stencil pipeline itself.

<!-- The end. -->
