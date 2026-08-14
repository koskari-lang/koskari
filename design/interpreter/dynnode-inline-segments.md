# DynNode Inline Segments: Every Extent Owns Its Stack

**Status: Speculative.  Try-it-and-evaluate; depends on
[stack-segment-simplification.md](stack-segment-simplification.md)
landing first.  Adopt only if the staged evaluation below holds up.**

## Purpose

Give **every** dynamic-chain node its own operand-stack segment, with
a small inline segment embedded in the `KosDynNode` base struct that
spills to a heap `KosSegment` on overflow.  Two goals:

1. **Unified extent behavior.**  Today eight node kinds own segments
   (frame, with, try, finally, loop, foreach, collect, prompt) and the
   rest inherit the ancestor's stack (boundary, cc, cc_raise, delim,
   gen, next, ns, param, shift, wind, yield_from).  Owning nodes get
   the safety property for free: an exception unwind or a swallowed
   error discards the node's segment wholesale, so partial operands
   from the extent's body can never pollute the enclosing stack.
   Inheriting nodes rely on discipline instead.  Making ownership
   universal turns "extents do not leak stack upward" from an
   invariant we maintain into one the representation enforces, makes
   stack imbalance a local error caught at the offending extent, and
   collapses the "find innermost stack owner" walks: the live segment
   is always `&dyn_top->stack`.

2. **Locality.**  A node's intrinsic fields and its operand values
   land in one allocation instead of two heap objects, shrinking the
   pointer spread on the hot push/pop paths and dropping the separate
   `NewEmpty` allocation (freelist hit or not) per owning node.

## Design

### Inline segment layout

Embed a genuine `KosSegment` -- header plus `objs[KOS_SEG_INLINE]` --
in the `KosDynNode` base struct, after the fixed fields.  At node
init, `node->stack` points at the embedded segment (`len = 0`,
`max = KOS_SEG_INLINE`).  Because the eval state and all consumers
already work through the `KosSegment **` owning slot, nothing outside
segment alloc/free needs to know whether the segment is inline:

- **Fast paths unchanged.**  The stack.h inlines test
  `len < max` / `len >= count` against a normal segment header; an
  inline segment simply has a small `max` and fails over to the
  out-of-line slow path when full.
- **Spill** (in Grow): when the inline segment is full, allocate a
  heap segment (first heap size = the standard birth size, or
  `2 * KOS_SEG_INLINE` if inline grows to 16), copy the pointer slots,
  retarget `node->stack` through the owning slot.  The node's inline
  storage goes quiet; `len` stays behind as garbage that init resets.
- **Release / Grow discriminate inline.**  An inline segment is never
  realloc'd, never freed, and never enters the freelist.  Discriminate
  by a flag bit in the header (or by address range against the owning
  node); pick whichever costs nothing on the fast path.
- **Clone paths** (`kos_dyn_*_clone`, continuation capture): copy an
  inline source into the destination node's inline segment when it
  fits; a spilled source deep-copies to a heap segment as today.

### Sizing

`KOS_SEG_INLINE = 8` is the starting guess, with a planned check
before commitment.  Call arguments accumulate on the current segment,
so any call of 8+ args (plus whatever else is live in the expression)
spills -- not hard to imagine.  16 is where spills should get rare,
at double the footprint.  Add spill telemetry (a `KOS_TRACE` counter:
spills vs node installs, and the spill trigger depth) and let
profiling data pick 8 vs 16.  If 16, first heap size moves to 32.

Footprint estimate (64-bit): `KosDynNode` today is roughly 40 bytes of
base fields + ~80 bytes of union (the `with` arm dominates) ≈ 128.
An inline segment adds 24 (header) + 8 x `KOS_SEG_INLINE`: ~216 total
at 8 slots, ~280 at 16.  All node kinds share one allocation size for
the single freelist, so the increase applies to every node and the
whole freelist; measure RSS on the churn benchmarks, not just speed.

### Universal ownership and upward delivery

Every node kind allocates nothing extra (the inline segment is part of
the node) and installs its own segment as the live stack on push.
Consequences:

- `kos_dyn_walk_live_from` no longer searches for a stack; the live
  block slot is always `&KosDynNode_STACK(dyn_top)`.
  `kos_evalstate_parent_stack_slot` becomes the `back` link (or one
  parent step) by construction.
- The currently-inheriting kinds need the **upward delivery step**
  added to their pop paths: an extent that completes normally hands
  its result (usually one value, sometimes zero) to the revealed
  parent segment.  This is a one-time tweak per kind, mirroring what
  `kos_dyn_with_post_unwind_deliver` does for with-nodes today.
- **Delivery is refcount-free.**  `KosStack_Deliver(inner, outer_slot,
  n)`: decrement `inner->len` by n, copy the n pointer slots, bump the
  outer segment's `len`.  Ownership moves with the pointers; no
  incref/decref.  The only slow path is a grow barrier on the outer
  segment (which writes back through the owning slot).  This is the
  two-segment composition of what `KosStackSteal` / `PopTailPtr` /
  `PUSH` already do individually.  Deliver runs as part of the
  uninstall transition, targeting the segment being made active -- it
  is not a license to mutate arbitrary non-active segments (see the
  simplification doc's consumer split).
- Transparent kinds whose extent produces no value (ns, param, wind
  teardown paths, boundary) deliver zero values; their pop is just the
  reveal.

### Interactions to work through

- **Continuation capture**: per-node stacks are already deep-copied on
  clone; with universal ownership, capture cost scales with chain
  length rather than owner count.  Chains are short, but measure.
- **`delim.saved_stack`** and the xfer-plan resume stacks hold
  displaced segments by pointer; displaced inline segments cannot
  outlive their node, so displacement must deep-copy or spill first.
  Audit every place a `KosSegment *` is stored away from its owning
  node.
- **Gen freeze/thaw** detaches chain spans; inline segments travel
  with their nodes, which makes this simpler than today (no separate
  stack ownership to reason about), but the back links across the
  detachment boundary must be cleared per the simplification doc's
  validity rule.

## Evaluation plan (staged)

1. **Probe the invariant first, cheaply.**  Before any inline layout
   work: give every node kind a heap segment at push (universal
   ownership only), with delivery steps added to the inheriting kinds.
   Run the full suite and the corpus.  Any code that reads or pops
   across an extent boundary surfaces as an immediate underrun.  This
   validates the semantic change in isolation.
2. **Inline layout.**  Add the embedded segment, spill, and the
   inline-aware Grow/Release.  Native memory gates (`test-asan`,
   `test-valgrind`) are mandatory here; the inline/heap discrimination
   is exactly the kind of thing they exist for.
3. **Telemetry and benchmarks.**  Spill-rate counters decide 8 vs 16.
   Churn benchmarks (`tox -qe churn`, binary_trees) plus RSS decide
   whether the locality win pays for the node-size increase.

Abandon criteria: stage 1 reveals legitimate cross-extent stack reads
that resist cleanup, or stage 3 shows the footprint cost without a
measurable win.

## Non-goals

- No change to the segment growth model past the spill (heap segments
  keep realloc-doubling to the cap).
- No revival of intra-stack block chains; the chain is the dynamic
  chain.  `back` remains the loose unwind cache defined in the
  simplification doc.
