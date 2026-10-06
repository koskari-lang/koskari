# Extents across a generator yield: freeze/install today, wind/unwind later?

A generator freeze moves the span above the GEN off the live chain as
it is and the install moves it back. Nothing between the frame and the
GEN acts on a yield: no `finally`, no `__exit__`, no after-thunk. That
is the Python rule, and it holds for every extent whose effect is the
node itself. It does not hold for an extent whose effect lives outside
its node.

**See also:** [extent-wind-unwind.md](extent-wind-unwind.md) (the
extent contract), [with-dyn-with.md](with-dyn-with.md),
[demand-flattening.md](demand-flattening.md) (why a Koskari consumer
steps a Koskari generator on its own chain).

## What exists today

A flattened generator runs on its consumer's context. Its `parameterize`
swaps cells into the consumer's parameter table, and the frozen span
used to leave those cells behind: the consumer read the generator's
binding while the span was frozen, and when the generator's block later
exited it wrote back the cell it had recorded at its first install,
long after the consumer's own `parameterize` had popped. The adapter
path never showed this because an async generator drive owns a
context of its own.

The fix is a pair of per-kind hooks beside the extent contract,
`kos_dyn_extent_freeze` at the freeze and `kos_dyn_extent_install` at the
install (`dynchain.c`). PARAM is the only kind with a body: the freeze
restores the cells it displaced, innermost first; the install re-enters
them outermost first over the table as it is at that resume, so the
record each node keeps of what sits beneath it is current. A failed
install restores what it had re-entered and the GEN stays frozen. Every
other kind is a no-op, and the two switches are where a further kind
would go.

## The wider question

A `dynamic-wind` inside a generator has the same shape. Its before and
after thunks exist to ensure a state on entry and exit of the extent:
a lock, a redirected stream, a mode. Across a yield the state stays
as the before-thunk left it while the consumer runs, and the resume
does not re-establish it. A `with` whose manager guards a resource is
the same case with different spelling. PARAM was fixed because its
effect is data the runtime owns and can move; a thunk's effect is not.

So the question is whether yield and resume should run a leave and a
re-enter over the span, without popping the nodes and without
rebuilding them:

- **Python model (today).** Yield is not an unwind. Thunks and
  managers run once, at entry and at the block's exit or the
  generator's close. Simple, cheap, matches every Python generator a
  user has seen, and wrong for a wind whose job is to hold a state
  only while its body is running.
- **Scheme model.** Yield is a control transfer out of the extent and
  resume is a transfer back in: after-thunks run at the freeze,
  before-thunks at the install, `__exit__` and `__enter__` likewise
  for a multi-shot `with`. Correct for the guard case, and a cost on
  every step of every generator whose span holds such a node.
- **Opt-in.** A node declares that it wants the walk (a capability,
  much as `KOS_DYN_CAP_NEEDS_INSTALL_WIND` already marks the nodes a
  transfer goal re-enters), and only spans holding one pay for it.
  PARAM is then the first such node and the freeze/install hooks
  become the general leave/re-enter.

## What the walk would be

The transfer goals already know how to leave and re-enter a span of
extents in order (`xfer_goal.c`, LEAVING then ENTERING), with a thunk
or a manager call able to suspend and resume the walk. A generator
yield would start the same kind of walk over the frozen span, with two
differences: the nodes are never unlinked, and the walk ends by
completing the freeze rather than delivering a value. The resume is
the mirror. Whether the goal machinery fits as it stands or the yield
needs its own walker is the first thing to find out.

## Open questions

- Which nodes should re-fire: WIND only, WIND and multi-shot WITH, or
  anything with `CONTROL_HOOK`? A single-shot `with` cannot re-enter,
  so under the Scheme model a yield inside one would have to be a
  refusal, as a transfer across one is today.
- Cost. A flattened `for-each` over a generator freezes and installs
  on every element. The walk must be free when the span holds nothing
  that asks for it.
- A thunk that suspends at a yield: the freeze is then itself a
  pending operation with a resume, on a path that today never
  suspends.
- Errors. An after-thunk raising at a yield, or a before-thunk at a
  resume, has no obvious frame to land in; the transfer goals answer
  this for shift and reset and the answer may carry over.
- Interaction with `yield-from`: the span of a relay holds the inner
  generator's span, so the walk crosses a GEN node.

## Status

Investigate. PARAM is fixed through the freeze/install hooks; nothing
else changes until the walk is designed. `design/TODO.md` carries the
stub.
