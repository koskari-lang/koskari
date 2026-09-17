# Loop transfer fast path: CONTINUE and BREAK with the region on top

## Status

Proposal -- not implemented. Sequenced after
[item-access-opcodes.md](item-access-opcodes.md).

## Summary

CONTINUE and BREAK reach their loop through the general transfer
walk: find the innermost LOOP or FOREACH region, prove it lies in the
current frame, unwind every node above it with the event offered to
each, then set the result, clear the segment, and move the pc. In the
common case the region is the node on top of the chain, nothing
stands between the transfer and its loop, and every step of the walk
is a check that answers immediately. This proposal gives both
handlers an arm for that case, and gives the FOREACH back-edge a
single dispatch.

## What the handlers do today

CONTINUE (`koskari/lib/opcode/CONTINUE.c`) pops or synthesises the
result, then calls `kos_dyn_unwind_to_loop`
(`koskari/lib/dynchain/dyn_walk.c`):

1. `kos_dyn_find_loop` walks from DYN_TOP toward the root for the
   first LOOP or FOREACH node.
2. A second walk from that region toward the root proves the current
   frame is an ancestor; a region in another frame raises.
3. While DYN_TOP is not the region: offer the event to the top node
   through `kos_dyn_handle` (a FINALLY consumes it and runs its body
   first), then `kos_dyn_unlink_top`; a leave that suspends parks the
   result on the node and returns.
4. For CONTINUE: swap the result into the region, clear the live
   segment (the region's own, which is how the extent resets whatever
   the body left), set the pc to the continue target, return NORMAL.
   For BREAK: read the break target, pop the region, drop its
   segment, push the result on the parent segment, jump.

The handler then threads on through `RESULT_NEXT_POLL`, the loop's
cooperation point for the eval breaker.

ITER_NEXT (`koskari/lib/opcode/ITER_NEXT.c`), the FOREACH continue
target, runs `kos_dyn_find_loop` again to reach the region whose
iterator it steps.

## Measurements

From `tox -qe churn-trace` over all kernels and the per-kernel
fannkuch trace (cycle counter, trace build, dispatch floor about 7ns):

| op | count | avg | note |
|---|---|---|---|
| CONTINUE (all kernels) | 2.50G | 7.8ns | |
| CONTINUE (fannkuch) | 25.5M | 8ns | 5.5% of the kernel |
| ITER_NEXT (all kernels) | 914M | 14.6ns | second find walk per iteration |
| BREAK (all kernels) | 19.2K | 20ns | cold on every kernel |
| PUSH_LOOP (fannkuch) | 12.7M | 48ns | 16.4% of the kernel |
| POP_LOOP (fannkuch) | 12.7M | 9ns | |

CONTINUE on a `while` loop is already within a nanosecond of the
floor: with the region on top, step 1 returns on its first node, step
2 on its second, and step 3 runs zero iterations. The walk-free arm
buys the branches and the two calls, not a walk. The FOREACH
back-edge is different: CONTINUE plus ITER_NEXT is two dispatches and
two find walks per element, and ITER_NEXT is twice the floor.

## Proposal

### 1. Top-is-region arm in CONTINUE and BREAK

Before the general walk:

    KosDynNode *top = KosEvalState_DYN_TOP(es);
    if (likely(top && (KosDynNode_IS_LOOP(top) || KosDynNode_IS_FOREACH(top))
               && KosDynNode_PARENT(top) == (KosDynNode *) KosEvalState_FRAME(es)))

The parent test is the same-frame proof for the depth-one case: the
region's parent is the frame, so the frame is its ancestor. A region
on top whose parent is a `let` NS node or another region inside the
same frame also qualifies, so the arm should test "the region is on
top" and walk parents only until it meets the frame, which is the
existing step 2 bounded to the nodes actually present. The handle
phase and the unlink loop are skipped outright: with the region on
top there is nothing to offer the event to.

CONTINUE then does exactly step 4 in place: result swap, segment
clear, pc set, `RESULT_NEXT_POLL`. BREAK does its own step 4 and
takes the arm for symmetry; its counts say the arm will not be
measurable.

The general walk remains the only path when anything sits above the
region (a `try`, a `with`, a nested `let`), which is where the offer
and the suspension handling live. The arm changes no observable
behaviour: it is the general walk with its zero-iteration loop
removed.

### 2. One dispatch for the FOREACH back-edge

With the region on top, CONTINUE knows it is a FOREACH and holds the
node. Two routes to a single-dispatch step:

- **Tail into the step.** CONTINUE's arm, after the result swap and
  segment clear, tail-calls the iterator step with the region in hand
  instead of threading to ITER_NEXT. ITER_NEXT keeps its handler for
  the first entry and for any CONTINUE that took the general walk.
  The step's else target is read from the ITER_NEXT row at the
  continue pc.
- **Node-style elector on the FOREACH node.** The open TODO entry
  "Electors: remaining homes" names the FOREACH step as the next
  node-style home: the step is elected onto the node at push, and
  CONTINUE's arm hops to it as YIELD_FROM's phase resume does. This is
  the larger change and the one that also removes ITER_NEXT's own
  find walk, because the elected step carries its node.

The first route is small and local; the second is where the
electors work is already headed. The recommendation is to take the
first as the measurable step and let the second replace it when the
FOREACH elector lands.

### 3. Result handling

CONTINUE 0 increfs None and the region releases the previous result
on every iteration. With the region on top and the previous result
also None, the swap is an incref and a decref of the same immortal
object; a `result == previous` test skips both. Trivial, but it sits
on every `while` iteration.

## Related: PUSH_LOOP

PUSH_LOOP is the expensive end of the same extent, at six times the
CONTINUE cost on fannkuch. It walks the extent table for its break
target, allocates the node, installs the inline segment, and runs
the full live-register resync (`retarget_after_push`, 20ns of the
48ns on the fannkuch trace). The break target could come from a
cache slot or from a finalize-written operand, which removes the
walk and the descriptor lookup; the resync half needs a push for a
node that changes only the block pointer. That is its own step and
is not part of this proposal.

## Testing

- `phase_lib` tests for CONTINUE and BREAK with the region on top,
  with a `let` between the region and the frame, and with a
  `try`/`finally` above the region (the general walk, unchanged).
- `phase_basics` coverage of `for-each` with `continue` in the body,
  with and without `else:`, and of a `break` value out of both loop
  kinds.
- `list_each` and `loop` under `tox -qe churn` as the measurement.

## Related

- [electors.md](electors.md) -- the node-style home and the FOREACH
  step as its next site.
- [extent-wind-unwind.md](extent-wind-unwind.md) -- the transfer
  walk contract the arm specialises.
- `koskari/lib/dynchain/dyn_walk.c` -- `kos_dyn_find_loop`,
  `kos_dyn_unwind_to_loop`.
- `koskari/lib/opcode/CONTINUE.c`, `BREAK.c`, `ITER_NEXT.c`,
  `PUSH_LOOP.c`.
