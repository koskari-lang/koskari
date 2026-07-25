# Delimited handler frames: ownership and SHIFT-pop provenance

## Status

Proposed. Diagnosis is verified against the implementation; the plan is not
yet started. Companion to [callcc.md](callcc.md) and
[../archive/interrupted-transfer-plans.md](../archive/interrupted-transfer-plans.md).

Reproducers live in `tests/reproducers/test_delimited_cross_frame.py`
(cross-frame capture, one red) and
`tests/reproducers/crashers_delimited_cross_frame.py` (quarantined).

## The three cases a rule has to cover

A `shift` pushes a SHIFT node delimiting the **handler body**, and `POP_SHIFT`
at the end of that body pops it. Who pops it, and what must stay alive for
them to do so, differs by shape.

| # | Shape | Handler runs in | Continuation exits the handler's frame? |
| - | ----- | --------------- | -------------------------------------- |
| 1 | prompt and shift in the same frame | the prompt's frame | **No** -- it ends at POP_PROMPT inside that frame |
| 2 | frames interleaved between prompt and shift | a callee frame | **Yes** -- the callee's return is part of the continuation |
| 3 | k stored, invoked after the prompt unwound naturally | nothing; no live handler | N/A -- the SHIFT on the chain is a clone from the graft |

Case 1 works today. Case 2 crashes. Case 3 works for the shapes covered by
`TestDynWindContinuations` but has no rule distinguishing its SHIFT from
case 1's.

The asymmetry is the whole problem. In case 2 the callee frame does two jobs
at once: it hosts the handler body, and it is part of the captured
continuation. Resuming runs the callee's remainder and then **returns out of
the very frame the handler still has to come back to**. Case 1 never notices
because the handler's frame is the prompt's frame, which the continuation
terminates inside rather than exiting.

## Verified diagnosis for case 2

Timeline, with F the callee frame:

1. handler body runs in F
2. `(k v)` invoked -- `kos_delimited_invoke_inplace` displaces F's stack/pc
   with the captured resume state and saves the handler's stack/pc on the
   delim (`saved_stack`, `saved_pc`, `saved_acting_pc`)
3. the slice runs **in F** (the resumed callee remainder), so F must be on
   the chain and current
4. the slice's callee-return exits F. `kos_dyn_walk_return` loops
   `while (dyn_top != frame)`, so it stops at F itself and never reaches the
   DELIM below it -- the continuation is **not** truncated, which was an
   earlier suspicion and is wrong. F is popped **and freed** to the node
   freelist.
5. the caller's remainder runs, reaches POP_PROMPT, routes to the delim,
   and calls `kos_dyn_inplace_restitch`
6. the handler was supposed to continue at the invoke site, then POP_SHIFT

Step 4 invalidates everything step 5 depends on. Restitch then does three
wrong things:

* `ret_frame = KosDynDelim_RETURN_FRAME(delim)` is a **borrowed** pointer to
  freed F, and `KosDynFrame_PC(ret_frame)` / `ACTING_PC` are written into it
* `resume_frame` is resolved by walking **at-or-below the prompt**, which
  finds the *caller's* frame, so the handler's `saved_stack` is restored over
  the caller's stack. This is the same defect already corrected on the
  invoke side (resolve the frame owning the SHIFT), not yet on this side.
* the stashed span, SHIFT included, is re-linked above the prompt with no
  frame for the handler to run in

The observable end state is a SHIFT node at `dyn_top` when the enclosing
scope's POP_NS runs. Before the kind check added in `kos_dyn_pop_ns`, that
was a SIGSEGV via an unconditional `Py_DECREF` of a NULL namespace.

## What the delim already has, and what it lacks

The evaluator needs four things to resume the handler: **code**, **ns**,
**block**, **pc**.

| item | where it lives |
| ---- | -------------- |
| block | `delim.saved_stack` -- already on the delim, and **owned** |
| pc | `delim.saved_pc` / `delim.saved_acting_pc` -- already on the delim |
| code | only on the frame node (`KosEvalState_CODE(es) = KosDynFrame_CODE(ft)`) |
| ns | innermost NS provider on the chain |

So half the state is already saved and the other half depends on a live
frame node.

**Rejected: add `code` and `ns` fields to the delim.** Consumers resolve the
current frame by walking the chain for a FRAME -- `kos_chain_first_frame`,
`KosEvalState_FRAME`, exception dispatch. A handler running with code and ns
reconstituted from the delim but no FRAME on the chain breaks those, and
synthesising one to satisfy them re-invents the node. The delim arm is also
already the largest in the `KosDynNode` union, so two more pointers grow
*every* node in a freelist-hot allocator, to duplicate what a node already
carries.

**Chosen: the delim owns the handler's frame node.** `delim.chain` is
already `owned when mode == INPLACE`, so node ownership on a delim is an
established pattern. Code and ns then resolve exactly as they do today.

## Proposed rule

> **A SHIFT node is popped by whoever pushed it. A live handler's frame stays
> alive until its POP_SHIFT runs.**

| case | who pops SHIFT | what must hold |
| ---- | -------------- | -------------- |
| 1 | POP_SHIFT in the prompt's frame | nothing extra |
| 2 | POP_SHIFT in the callee | the handler frame is **pinned**: unlinked but not freed at step 4, re-pushed by restitch before the handler resumes |
| 3 | **graft teardown**, not POP_SHIFT | the grafted SHIFT is a clone with no handler body to re-enter and must never be left for a POP_SHIFT to find |

Case 3 is why "always let POP_SHIFT do it" is wrong; case 2 is why "always
let the graft do it" is wrong. The split is on **provenance** -- live handler
versus clone -- which is known at push/clone time and belongs on the node as
a flag.

Note that **case 2 with multi-shot is case 2 plus case 3**: only the first
invoke runs in place, and re-invokes fall through to the PUSH graft where the
handler frame is not live at all. That is the still-failing `k-callee-loop`
shape, and it is unblocked by the case-3 work rather than the case-2 work.

## Implementation plan

Each phase is independently verifiable. `tools/delimited_sweep.py` runs the shape matrix, one
subprocess each, because a dynchain fault corrupts state for whatever runs
next in the same process.

### Phase 0 -- detection before repair

* Add the missing sweep shapes: cross-frame abandonment (a shift that never
  invokes k, which the pytest suite caught and the sweep does not cover),
  reset-inside-a-let, and stored-k-invoked-after-unwind.
* Add a POP_PROMPT invariant: no SHIFT between the prompt and `dyn_top` may
  lack a live handler frame. It should fire on the current bug, which is how
  we know it detects.
* Extend the kind check from `kos_dyn_pop_ns` to the sibling
  `kos_dyn_pop_*` functions. Each is a latent SIGSEGV on the same class of
  topology fault.

Exit criteria: the new invariant fires on P3/N1/N6 with a topology message;
no behaviour change elsewhere.

### Phase 1 -- pin the handler frame

* On INPLACE invoke, transfer ownership of the resume frame node F to the
  delim.
* Teach the frame pop path that a pinned frame is **unlinked, not freed**.
  Simplest mechanism is a `KosDynFlags` bit set when the delim takes it and
  cleared when the delim releases it; the freelist return path already
  consults node state, so this is a narrow addition.
* Release on delim teardown, including the error paths in
  `kos_delimited_invoke_inplace` and `kos_dyn_inplace_restitch`.

Exit criteria: valgrind clean on P3 -- no write to freed memory at
`KosDynFrame_PC(ret_frame)`. Still failing, but for the next reason.

### Phase 2 -- restitch targets the right frame

* Replace restitch's at-or-below-the-prompt walk with the delim's owned
  frame, mirroring the invoke-side correction.
* Re-push F onto the chain above the prompt before re-linking the span, so
  the handler has a frame and the frame-walkers resolve.

Exit criteria: P3/N1/N6 produce their expected values. Sweep back to at
least 24/25, and the `test_cross_frame_abandonment` reproducer should be
re-examined -- it is the same "handler's frame is gone before its
bookkeeping runs" family.

### Phase 3 -- SHIFT provenance for case 3

* Flag SHIFT nodes at push and at clone with their provenance.
* Graft teardown pops cloned SHIFTs; POP_SHIFT asserts it is popping a
  live-handler one.

Exit criteria: `k-callee-loop` passes; the existing remote-invoke tests in
`test_reset_shift.py` stay green.

### Phase 4 -- close out

* Re-test the quarantined crashers. They may still fail: their backtrace
  goes through `eager_step_awaitable` / `push_or_await_plain`, a nested eval
  under an eager awaitable step, which is a **distinct** crash path from the
  one this plan addresses and needs its own diagnosis.
* Full suite, `make lint`, and a valgrind pass over the reproducers.
* Update [OPCODES.md](../OPCODES.md) §20, whose account of the prompt owning
  the region's stack block and of the continuation being "the code between
  POP_SHIFT and POP_PROMPT" has already misled two rounds of work.

## Open questions

* Whether a pinned frame needs to stay GC-traversable while unlinked. It
  holds a `KosCode` strong reference, so the delim's traversal must cover it
  or the collector can miss the cycle.
* Whether the flag or an explicit owner pointer is the better pin mechanism.
  The flag is cheaper; an owner pointer is self-describing when debugging.
* Whether case 3's graft should reuse the pin machinery for symmetry, or stay
  clone-only.

# The end.
