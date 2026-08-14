# DynNode Inline Segments: Every Extent Owns Its Stack

**Status: Stages 1 and 2 IMPLEMENTED on branch `simplify-stackblock`;
all gates green including ASAN/Valgrind and the cache cross-check
build.  Stage 1 (universal region ownership) revised the design:
ownership is universal across *region* kinds, not across every node --
see the region/bookmark taxonomy below, which the probe surfaced
immediately.  Stage 2 embedded the inline segment
(`KOS_SEGMENT_INLINE_SLOTS` = 8, discriminated by `max` below the heap
birth size): region constructors aim `stack` at the node's embedded
storage via `kos_dynnode_use_inline_segment`; `KosStack_Grow` spills
inline contents to a heap segment through the owning slot;
`KosStack_Release` empties inline segments in place; `KosStack_Detach`
covers the one displacement site (the delim stash) where a segment is
stored away from its node; segment cloning is centralized in
`kos_dyn_clone` (into the clone's inline storage when it fits, heap
DeepCopy otherwise) and the eight per-kind clone copies were deleted.
Continuation resume-stack aliasing was audited: originals alias
chain-owned segments and are cleared without release; clones and plans
always hold heap deep-copies, so inline aliases never escape their
node's lifetime.  Remaining stages: telemetry/sizing, then the end
state.  Builds on the landed simplification pass (archived spec:
[archive/stack-segment-simplification.md](../archive/stack-segment-simplification.md)).**

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

### Region kinds vs transfer bookmarks (stage 1 finding)

Universal ownership applies to **block-structured region kinds**:
frame, with, try, finally, loop, foreach, collect, prompt (the
pre-existing owners) plus param, ns, and wind (added by stage 1).
These compile as `PUSH_X body POP_X` blocks and isolate their body's
operands on their own segment.

**Transfer bookmarks stay transparent**: cc, cc_raise, delim, shift,
gen, next, yield_from.  The compiler emits their pushes
mid-expression -- `PUSH_CC` lands inside an open call window with the
callable already on the stack; `OBJECT_NEXT` sits between operand
loads -- so their extents legitimately straddle operand windows on the
enclosing segment and must not capture them.  The suite fails within
seconds if a bookmark kind is given a segment (call underruns in
continuations, generator delivery losses), which is exactly the probe
working as designed.  Enforcement is `kos_dyn_ensure_region_segment`
at the push entry points; the per-kind clone gap is closed generically
in `kos_dyn_clone` (a clone whose per-kind body copied no stack deep-
copies the source's).

Delivery for the stage-1 additions: transparent-extent values flow
upward by **deliver-all on normal pop** (`kos_dyn_steal_extent_values`
/ `kos_dyn_repush_stolen`, refcount-free steal around the pop) --
reproducing exactly what the shared segment did, with no per-kind
count knowledge -- and are discarded on unwind pops, where the old
leftovers were cleared by the catching handler anyway.  Wind is the
one special case: its no-after-thunk uninstall arm delivers
`body_result` to the enclosing owner's segment through the owning
slot (a justified parent-delivery site; the wind's own segment dies
with the node).  POP_WIND and POP_SHIFT already extract their value
before popping and needed nothing.


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

## End state: retiring the retarget walks

Where this ultimately points.  Evaluation requires three live facts on
the eval state -- the frame (code and PCs derive from it), the operand
segment slot, and the namespace -- and the retarget walks exist only
because a popped node can shadow any subset of them without the chain
recording what it displaced.  The end state makes each fact evident on
push and pop with no walk, by a mechanism chosen per fact:

1. **Segment: by ownership, for region kinds.**  Universal region
   ownership (this document) makes the live slot `&STACK(dyn_top)`
   whenever the top is a region node.  Transfer bookmarks stay
   transparent (see the taxonomy above), so the fully walk-free
   variant needs bookmarks to *alias* the enclosing slot at install --
   a borrowed `KosSegment **` under the same install-time/detach
   discipline as the current back link -- or the pop reveal skips
   bookmarks with a bounded hop.  Either way the segment `back` link
   on region segments dies: parent delivery becomes a walk of at most
   the bookmark run between owner and enclosing owner.

2. **Namespace: by owned fill.**  Every node's existing `ns` field is
   filled at push -- its own namespace when it introduces one, an
   incref of the inherited active namespace when it does not.  The
   active ns is then `NS(dyn_top)`, always, and it is owned state
   correct by construction rather than a cached answer: frozen spans,
   stashed slices, and clones carry strong refs that stay valid
   wherever the span goes, so the entire ns detach-hygiene class
   (and the resumed-region namespace-loss bug family) disappears.
   A back link on the namespace object itself does NOT work: segments
   are single-owner, namespaces are shared (the module namespace is
   the extent of every module-level frame concurrently; closures keep
   function namespaces re-runnable), and two installs of one shared
   object would race one field.
   Costs: one incref per non-introducing push and one decref at free
   (off the per-opcode path; frames already own theirs); free,
   traverse, and every per-kind clone treat the base `ns` field as
   uniformly owned; and -- the one silent hazard -- `NS(node) !=
   NULL` stops meaning "introduces a namespace scope".  Every reader
   using the field as an ownership discriminator (the walk, the
   inherit arm of retarget_after_push, snapshots, capture logic
   seeking the innermost ns owner) must switch to a kind- or
   flag-based test.  That audit is its own work item.

3. **Frame: by back-frame restore.**  Dynnodes are not refcounted, so
   a uniform per-node frame stamp would be a borrowed pointer with
   hygiene obligations on every node.  Frame needs less: popping a
   non-frame node never changes the live frame, so the only event
   needing an answer is popping a frame, and a `back_frame` link
   recorded on the frame at install (the frame it displaced --
   generalizing what `eval_ng_plain_return` already does by hand)
   covers exactly that.  One borrowed cache with a narrow validity
   story: cleared at park, clone, and freeze, restamped at splice.

With all three in place the retarget walks reduce to a splice-time
restamp (`rebuild_live_from_chain` becomes "restamp the span", run
once per splice instead of once per pop) plus the
`KOS_DEBUG_PATCH_DYN_CACHES` cross-check, which should stay enabled
through the whole transition; the walk code is retired only after the
stamped world has soaked under it.  Note the deliberate asymmetry of
mechanisms -- ownership, owned fill, borrowed restore -- each is
locally optimal, and unifying them in either direction would be wrong:
ns cannot be ownership-based (sharing), segment needs no restore
(universal ownership), frame cannot be owned fill (nodes are not
refcounted).

Sequencing: path 2 as specified above (kills the block walk and the
segment back link), then ns owned-fill and the frame back-frame as a
separate step (kills the remaining walk arms), then walk demotion to
debug-only.


## Evaluation plan (staged)

1. **Probe the invariant first, cheaply.**  DONE (stage 1, branch
   `simplify-stackblock`).  Heap segments at push for the previously
   inheriting kinds, deliver-all on their normal pops.  The probe
   worked exactly as designed: underruns immediately partitioned the
   kinds into regions and bookmarks (taxonomy above); two rounds of
   fixes (the predicate, the wind delivery arm) took the suite, the
   native/ASAN/Valgrind gates, and the cache cross-check build to
   green.
2. **Inline layout.**  DONE (stage 2).  Embedded segment, spill in
   Grow, inline-aware Release, Detach at the delim stash, centralized
   clone copy.  The fast paths in stack.h needed no changes; ASAN and
   Valgrind ran clean on the first build.  Native tests cover inline
   capacity, spill (slot update, value and back carry-over, inline
   left empty), and detach semantics.
3. **Telemetry and benchmarks.**  DONE (stage 3).  Spill counters
   (`seg_inline_install` / `seg_inline_spill` KOS_TRACE events, visible
   via `tox -qe churn-trace`) measured the full churn battery at
   **134,965,324 inline installs and 56 spills** -- a spill rate of
   4e-7.  **KOS_SEGMENT_INLINE_SLOTS stays 8**; the 16-slot variant is
   moot.  RSS is a non-issue: peak 35.4 MB on the tree-heavy koskari
   benchmarks, and the dyn-node freelist cap (128) bounds the
   node-growth footprint at ~11 KB.  Branch medians (5 samples):
   binary_trees 2.90x, method 2.82x, match_case 2.99x, nbody 2.65x,
   loop 1.52x ki/py.  A heap-forced A/B (region nodes given heap
   segments, inline shrunk to 1) isolated the cost: call-heavy
   workloads (binary_trees, method) ran ~5-8% faster with small nodes,
   locality workloads (nbody, match_case) ~4-5% faster with inline --
   the node-growth cost was the two full-struct memsets in the node
   alloc/freelist cycle, not the layout.  Fix: both memsets now stop
   at `offsetof(KosDynNode, iseg)` -- the embedded tail needs no
   zeroing, its header is initialized on use and its slots are gated
   by len.  With the trim, inline-8 is best-of-all-configs on
   binary_trees (0.1392s, 2.67x -- at the pre-branch 2.70x mark),
   method, and match_case, keeping nbody's locality win.  Valgrind
   runs clean, confirming the len-gating of the unzeroed tail.

Abandon criteria: stage 1 reveals legitimate cross-extent stack reads
that resist cleanup, or stage 3 shows the footprint cost without a
measurable win.

## Non-goals

- No change to the segment growth model past the spill (heap segments
  keep realloc-doubling to the cap).
- No revival of intra-stack block chains; the chain is the dynamic
  chain.  Within this document's scope `back` remains the loose unwind
  cache defined in the simplification doc; its eventual removal
  belongs to the end-state step above.
