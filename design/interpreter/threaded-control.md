# Threaded control: every signal a jump target

## Status

Proposal -- not implemented. Sequenced after the call-path and
loop-transfer work on the churn branch. The stretch goal at the end
depends on changes 2 and 5 and is its own arc.

## Summary

Under threaded dispatch every opcode handler is a `KosThreadFn`
reached by a guaranteed tail call, and control returns to the
trampoline only with a signal. Three of the four things the
trampoline still does are control transfers spelled as a return value
plus a switch, a shape inherited from the loop-bodied evaluator that
could not afford them inline. This proposal turns each of them into a
thread function that ends in `KOS_THREAD_NEXT` or a hop, so the
evaluator has one entry, returns only at a boundary, and the signal
set becomes a set of jump targets.

## What the trampoline does today

From the loop in `koskari_eval` (`koskari/lib/eval.c`, the `prologue`,
`reenter`, `poll`, `thread` and `handle_result` labels) and
`apply_kos_op_result`:

1. **Boundaries.** RETURN at the root or under a GEN parent, YIELD to
   an outside driver, AWAIT on an unsettled awaitable, FATAL. Control
   leaves the evaluator.
2. **Raise.** `process_raise` walks to the handler, then answers
   REPROLOGUE so the loop lands on the handler pc.
3. **The prologue protocol.** `kos_prologue_reenter`: staged delivery,
   pending exception, goal drain, resume delivery, in that order, each
   re-entered through `KOS_REENTER_AGAIN`, with the breaker poll ahead
   of every re-entry.
4. **Hooks.** CONTROL_RETURNED and the with and wind reactions.

Only the first is a boundary. The RETURN and YIELD rows already show
the alternative: `kos_return_deliver` and `kos_yield_deliver` are
thread functions every row of each family tail-calls, and the plain,
region and resume return shapes thread straight on.

## The changes

Each lands green on its own, in this order. Change 2 is the keystone;
1 is small and independent, 3 and 4 follow from 2, 5 is a consequence.

### 1. The breaker service as a thread function

`kos_eval_poll(es, insn, val)`: the GIL drop and `Py_MakePendingCalls`
from the `poll:` label, then a tail into the prologue when a signal
raised, else `KOS_THREAD_NEXT`. The cooperation points stop returning:
`RESULT_NEXT_POLL` tails into it when the breaker is up,
`kos_call_installed` too, and `RESUME_HOP` tails into it in place of
`DISPATCH`. Releasing the GIL inside a tail chain is safe: the chain
holds no C frames. The trampoline's "NORMAL means a cooperation point"
case goes with it.

### 2. The prologue as a thread function

`kos_eval_prologue(es, insn, val)` is `kos_prologue_reenter` with two
changes: `KOS_REENTER_AGAIN` becomes a tail into `kos_eval_poll` when
the breaker is up and into itself otherwise, and its NORMAL exit
becomes `KOS_THREAD_NEXT` with the pc-bounds safety net moved inside.
Boundary kinds return.

With it, `RESULT_RELAY` learns the rest of the vocabulary: NORMAL and
DISPATCH thread, REPROLOGUE tails the prologue, RAISE tails the raise
function of change 3, anything else returns. That one macro is how
helpers keep returning `KosOpResult` while every handler turns the
answer into a jump; the forty `EVAL_REPROLOGUE()` sites in helpers do
not change.

The prologue's order is the one discipline the whole set keeps: raise,
goal drain and resume delivery reach the instruction stream only
through it, since the staged-delivery and goal interactions recorded
in that loop are exactly where order matters.

### 3. Raise as a thread function

`kos_eval_raise(es, insn, val)` holds `process_raise`: a handler found
means the pc is set and it tails the prologue, so the pending-exception
phase still runs first; no handler means the boundary RAISE returns.
`RESULT_RAISE()` becomes that tail call. It is used only in handler
bodies and electors, which carry the prototype, and the compiler proves
it: musttail refuses any other caller. `return EVAL_RAISE()` in helpers
stays a return that `RESULT_RELAY` turns into the jump. Exceptions as
control flow stop paying a trampoline trip.

### 4. The trampoline collapses

`koskari_eval` keeps its entry phases, then one plain call into
`kos_eval_prologue`, then cleanup on what comes back. The five labels
and `apply_kos_op_result` go. `KOS_RES_REPROLOGUE`, `KOS_RES_DISPATCH`
and `KOS_RES_NORMAL` remain as helper answers but never reach the
trampoline; the boundary kinds are YIELD, AWAIT, RETURN, RAISE and
FATAL. The trace's trampoline-step counter becomes the number of
boundary crossings, which is the measurement for the set.

### 5. The remaining events as thread functions

The general RETURN path from POP_PROMPT, the values and call-core
termini and a resume's completion tail `kos_return_deliver` as the
RETURN rows do. The with and wind control-returned hook becomes
`kos_eval_control_returned(es, insn, val)`, reached from the region
return, and `KOS_RES_CONTROL_RETURNED` goes. `eval_event_await` splits
the same way into `kos_eval_await(es, insn, val)`: an awaitable already
settled threads on, an unsettled one returns the boundary.

### 6. What the shape then allows

Each is its own later step:

- Finalize-written control rows. THEEND is already a barrier handler;
  a root-return row and a generator-exhausted row can tail straight to
  the boundary exit with the frame in hand, as CONTINUE now steps its
  own iterator.
- A native provider's synchronous step completion as a hop on the
  resume mechanism as it stands, so only a step that must wait returns
  to Python.
- The post-op trace wrappers around the events go; the events are
  handlers and get handler timing.
- `KosOpResult` at the trampoline reduces to the boundary kinds plus
  the value; the sixteen-byte round trip stays only where a helper
  answers a handler.

## What does not change

The three boundaries and every call into a Python callable are C
returns; the frame-depth limit exists for that recursion and the tail
chain does not touch it. FATAL stays a return.

## Stretch goal: resumable reads

Today a `KosDynamicRef` wraps its get function in `KosDemand_New`
(`ref.c`), so a read runs a nested evaluator to completion on the C
stack: the demand path, and the reason a read can never suspend. With
changes 2 and 5 in place a deref may answer "not yet" and the reader
settles instead of forcing.

- A ref kind whose deref can answer with an awaitable instead of a
  value: a future ref for a backgrounded binding, and the dynamic
  ref's getter unwrapped from its demand so a coroutine getter answers
  the same way. `kos_ns_fast_deref` and `frame_ns_deref` return that
  awaitable with a distinct answer, one extra branch on the non-raw
  arm only; raw slots are untouched.
- One thread function, `kos_eval_settle(es, insn, awaitable)`: install
  a resume on the frame whose state is the ref, then tail into
  `kos_eval_await`. An awaitable already done delivers on the spot; one
  that is not returns the AWAIT boundary, and the Python loop runs the
  background work. Under a demand driver the boundary forces as it does
  today, so the same code is transparent in both drives.
- The resume, on delivery: store the value into the ref, so it is
  settled for every later read, then thread at `acting_pc`, re-executing
  the instruction, which now reads a plain value. A failed future
  delivers its exception to the same resume, which raises at the read
  site.

Re-execution is the whole trick. Every row that derefs a namespace ref
does so before it touches the stack: VAR and FAST, the VAR2 to VAR4 and
FAST2 to FAST4 loads, and every fused VAR and FAST row and elector read
their operands from the row. The stack rows never deref a ref; their
operands were pushed by reads that already settled. So settle-and-retry
is correct for every reader by construction, and no handler learns
anything: the deref answers "not yet", the handler tails
`kos_eval_settle`, one line in the deref's failure arm.

What it gives: `(define x (background (compute)))` as an opt-in binding
form that stores a future ref; every read of `x` is a plain FAST until
the first read that arrives before the value, which suspends the
context and resumes at that instruction when the value lands. A
suspended read is a frame with a resume armed, so the frame cache
refuses it under the existing rule. The binding form must produce a
non-raw ref, which promotes the slot exactly as a closure capture does;
the template reset returns it to raw at the cache store.

The ref kinds and the deref answer are independent of the evaluator
work and can be built first. Getting the settle right across the
delimited-continuation and generator paths is the serious part: a
frame suspended on a settle sits on the chain like any await, and
every capture and freeze rule that applies to an await applies to it.

## Testing

- Per change: the full suite and the `KOS_EVAL_SAFETYNET=1` suite; the
  trace build's trampoline-step counter falling to the boundary count
  on the churn kernels.
- Change 3: the exception reproducers and the raise-heavy kernels under
  `tox -qe churn`.
- Stretch goal: reads that settle immediately, reads that suspend and
  resume under an asyncio driver and under a demand driver, a failed
  future raising at the read site, a settle inside a generator and
  inside a delimited region, and the frame cache refusing a suspended
  read.

## Related

- [EVALUATOR.md](../EVALUATOR.md) §1.2, §2.6, §3 -- the loop phases and
  the signal vocabulary this replaces.
- [electors.md](electors.md) -- the elector prologue, the first
  thread-function helper handlers hop into.
- `koskari/lib/eval.c` -- `koskari_eval`, `kos_prologue_reenter`,
  `apply_kos_op_result`, `process_raise`, `kos_return_deliver`,
  `kos_yield_deliver`.
- `koskari/lib/eval.h` -- `TAILCALL`, `RESUME_HOP`, `RESULT_RELAY`,
  `RESULT_RAISE`, `RESULT_NEXT_POLL`.
- `koskari/lib/ref.c` -- `KosDynamicRef` and its demand wrapping.
