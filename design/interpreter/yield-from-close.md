# Closing a delegate through `yield-from`

A throw into a generator suspended in `yield-from` is thrown into the
delegate: the frame's resume kind names a throw kind, the ingress sets
it on the frame with the exception as its delivery, and the throw kind
calls the delegate's `throw`, `athrow` or `ki_throw`. GeneratorExit is
the exception that is not routed. It raises at the delegating frame,
the raise walk clears the frame's resume, and the kind's clear
(`yield_from_state_clear`, `opcode/YIELD_FROM.c`) closes what it can
without awaiting. That last step is where the delegate kinds part
ways.

**See also:** [generator-extent-freeze.md](generator-extent-freeze.md)
(the other yield-time question), [extent-wind-unwind.md](extent-wind-unwind.md),
[demand-flattening.md](demand-flattening.md).

## What exists today

| Delegate kind | On the delegating generator's close |
| --- | --- |
| sync iterator | nothing to do |
| sync generator | `close()` is called; an error from it is discarded |
| Python async iterator | in-flight awaitable dropped; nothing else |
| Python async generator | in-flight awaitable dropped; `aclose` is not called |
| KosIter, including a Koskari generator | `ki_close`: a transform closes and closes its sources; a Koskari generator runs its teardown, so its `finally` runs now; declined while a step is in flight |

The Python async generator row finalizes later, through the
interpreter's asyncgen finalizer hook. A Koskari generator delegate's
`finally` runs at the `aclose` that closed the relay
(`tests/reproducers/test_yield_from_resume.py`,
`test_close_closes_the_delegate`).

GeneratorExit never enters the throw path, by the same split the
forwarding fix made on purpose, so that a sync delegate is closed
rather than thrown into.

## What the rule should be

PEP 380 for `yield from` on GeneratorExit: close the subiterator, then
re-raise. For an async delegate that means awaiting `aclose`, and for
a Koskari generator it means running its teardown drive, which can
suspend. The raise walk is the wrong place for either: it is a
structural unwind that cannot await, which is why the routine drops
the awaitable and moves on.

The throw path can. A GeneratorExit taken by the resume would reach
the drive's throw kind, and the python async generator's throw kind
already builds an awaitable for a thrown exception and suspends on it.
The shape would be: route GeneratorExit through the throw kind like
any other throw; the sync kind calls `close()` and re-raises
GeneratorExit; the async kind awaits `aclose()` (not
`athrow(GeneratorExit)`, which would let the delegate yield past it),
then re-raises; the KosIter kind drives the provider's own close, then
re-raises. The kind's clear stays as the fallback for a frame torn
down by anything other than a close.

CPython's only async relay shape, `async for x in sub: yield x`,
leaves `sub` to the finalizer hooks when the outer is closed, so
today's behaviour is not worse than Python's for that shape. It is
worse than `yield from`, which is what `yield-from` names.

## Open questions

- The sync arm's `close()` errors are discarded today (plan item on
  swallowed close errors); a close routed through the drive would
  propagate them, which is CPython's rule.
- A delegate that ignores GeneratorExit and yields: `close()` raises
  RuntimeError for a generator; the async and KosIter arms need the
  same answer.
- Ordering against the delegating generator's own `finally`: the
  delegate must be closed first, before the outer unwinds past the
  halt, which the throw path gives for free and the raise walk gives
  only by running the kind's clear ahead of the frame's extents.
- The teardown drive at deallocation
  (`asyncgen_teardown_chain_on_ctx`) and a close driven through the
  relay must not both run; the closed mark on the wrapper is the
  guard to check.
- `KosPyIter` (`iterate.c`) closes through the boundary with its own
  context; whether the flattened close should reuse that drive.

## Status

Investigate. The forwarding of every other throw is implemented;
GeneratorExit keeps the raise-walk close. `design/TODO.md` carries
the stub.
