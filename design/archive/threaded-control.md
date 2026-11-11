# Threaded control: every signal a jump target

## Status

Implemented on the churn-test-performance branch (2026-09-17): changes
1 to 5 and 7; change 6 assessed and closed with nothing to do. The
reads that call and reads that wait, which build on change 7, are
designed in [../interpreter/resumable-reads.md](../interpreter/resumable-reads.md).

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
   REPROLOGUE so the loop arrives at the handler pc.
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

Each is completed green on its own. The order that held: 1, then 3 and 5,
then 2 and 4 together. The prologue can only be a tail target once no
raw result needs the trampoline's post-processing, since a tail-entered
chain has no one to post-process it, and that is what 3 and 5 provide.

### 1. The breaker service as a thread function

`kos_eval_poll(es, insn, val)`: the GIL drop and `Py_MakePendingCalls`
from the `poll:` label, then a tail into the prologue when a signal
raised, else `KOS_THREAD_NEXT`. `RESULT_NEXT_POLL` tails into it when
the breaker is up; a frame install answers the loop-internal
`KOS_RES_POLL`, which the handler's `RESULT_RELAY` turns into the same
tail call. A plain NORMAL relay is not a cooperation point: a signal is
taken at back-edges and frame entry only, which the signal reproducers
assert, and the native call API translates POLL to REPROLOGUE so its
callers see what they saw before. `RESUME_HOP` keeps staging through
`DISPATCH`, which reaches the prologue's poll after change 2. Releasing
the GIL inside a tail chain is safe: the chain holds no C frames.

### 2. The prologue as a thread function

`kos_eval_prologue(es, insn, val)` is `kos_prologue_reenter` with two
changes: it polls in place, through the plain `kos_eval_poll_now`, at
its head and on every pass that goes round again with the breaker up,
and its NORMAL exit becomes the instruction fetch and a tail call into
the handler, with the pc-bounds safety net moved inside. Boundary kinds
return.

With it, `RESULT_RELAY` tails the prologue on REPROLOGUE, `DISPATCH`
tails it after staging, `kos_eval_poll` tails it when the poll raised,
and every event thread function tails it when its event answers
REPROLOGUE. That is how helpers keep returning `KosOpResult` while every
handler turns the answer into a jump; the forty `EVAL_REPROLOGUE()`
sites in helpers do not change.

The prologue's order is the one discipline the whole set keeps: raise,
goal drain and resume delivery reach the instruction stream only
through it, since the staged-delivery and goal interactions recorded
in that loop are exactly where order matters.

### 3. Raise as a thread function

`kos_eval_raise(es, insn, val)` holds `process_raise`: a handler found
means the pc is set and it tails the prologue, so the pending-exception
phase still runs first; no handler means the boundary RAISE returns,
and `kos_eval_prepare_boundary_raise` marks it terminal on the eval
state so the trampoline leaves with it instead of walking it again.
`RESULT_RAISE()` becomes that tail call. It is used only in handler
bodies and electors, which carry the prototype, and the compiler proves
it: musttail refuses any other caller. `return EVAL_RAISE()` in helpers
stays a return that `RESULT_RELAY` turns into the jump. Exceptions as
control flow stop paying a trampoline trip.

### 4. The trampoline collapses

`koskari_eval` keeps its entry phases, then one plain call into
`kos_eval_prologue`, then cleanup on what comes back. The five labels
go. A result the chain hands back is a boundary when the eval state's
`terminal` mark says so: a raise walked to no handler, a recorded AWAIT
or YIELD suspension, a root return, or FATAL. `apply_kos_op_result`
stays as the fallback for an unmarked result, one a producer returned
raw instead of tailing into its event, applied as the loop-bodied
evaluator did before the prologue is called again. `KOS_RES_REPROLOGUE`,
`KOS_RES_DISPATCH`, `KOS_RES_POLL` and `KOS_RES_NORMAL` remain as helper
answers. The trace's trampoline-step counter becomes the number of
fallback applications, which is the measurement of what still returns
raw.

### 5. The remaining events as thread functions

`kos_return_deliver` takes the general RETURN path itself after its
plain arms, and the other producers of a RETURN reach it: POP_PROMPT
and POP_FINALLY through `RESULT_RETURN`, the values and call-core
termini and a resume's completion through `RESULT_RELAY`.
`kos_yield_deliver` runs the freeze for every generator, a root one
included. The with and wind control-returned hook becomes
`kos_eval_control_returned(es, insn, val)`, and `eval_event_await`
`kos_eval_await(es, insn, val)`, both reached through the relay and,
for AWAIT, through `RESULT_AWAIT` from the handlers that returned it
raw. Each event marks the eval state terminal where its result becomes
a boundary, and the resume-delivery hub leaves with a marked result as
the trampoline does. `RESULT_RELAY` speaks the whole vocabulary: NORMAL
and DISPATCH thread, POLL polls, RAISE, AWAIT, RETURN, YIELD and
CONTROL_RETURNED go to their event, REPROLOGUE to the prologue, FATAL
returns.

### 6. What the shape then allows

Assessed after change 5 and closed with nothing to do: a root return
already reaches its boundary in one hop; a provider step's answers are
relayed into their events by the handler; the event trace wrappers keep
the per-event timing the trace build reports; and the result struct's
sixteen bytes cost nothing at a boundary. Kept as the record of the
allowances:

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

### 7. The resume kind

Before this change a frame's resume was three words: `resume_fn`,
`resume_state` and `resume_name`. The state was a borrowed pointer, and
what it pointed to was governed by a convention per user and by three
sites in the frame code: `kos_dynframe_free` nulled the pair;
`kos_dynframe_clone` copied a NULL-state pair and refused any other by
name; the transfer restore in `xfer_goal.c` copied the pair verbatim.
The one non-trivial state in the tree, the provider record a transform
step sets, survived because the iteration node that steps it owns it,
and a step abandoned without a raise walk left that record marked busy
with its pending value held.

The change puts the behaviour beside the function, once, in a static
record every frame that sets that function shares:

```c
typedef struct KosResumeKind {
  const char *name;                 /* what resume_name carried */
  KosThreadFn fn;                   /* the resume itself */
  void (*clear)(void *state);       /* teardown with no delivery coming;
                                       NULL: the state is borrowed */
  void *(*clone)(void *state);      /* capture and multi-shot application;
                                       NULL: refuse by name */
} KosResumeKind;
```

The frame holds `resume_kind` and `resume_state`, two words. A resume
is set when `kind != NULL`. The state stays per frame; the kind never
holds anything that varies between frames.

The lifecycle sites are one rule each: free, park and tail reset go
through `kos_dynframe_clear_resume`, which calls `clear` when the kind
has one and then nulls the pair; clone calls `clone` and copies the
kind, or refuses by `kind->name`; the transfer restore record takes
`clone` of the template's state, so a multi-shot application never moves a
state out of its template, and applying it clears the live frame's
resume first. Clean completion stays the owner's business: the node or
provider clears its own fields when the step finishes, and `clear` runs
only when no delivery will come. The failure delivery is unchanged: a
live NULL into `kind->fn` with the error set.

Every site that sets a resume and the kind it declares:

| site | kind |
|---|---|
| provider step (`transform.c`, through `KosResumeFrame_Install`) | one static per provider phase: `clear` is `kos_transform_step_clear`, releasing `busy` and `pending` on the record; no `clone` |
| the CPS entries `KosEvalState_CallResult` and `NextResume` | the native caller hands in its kind with the state; the entries stop taking a bare function |
| yield-from phases (`dyn_yield_from.c`) | one static per phase: no `clear`, `clone` is `kos_resume_clone_shared`, since the node it serves clones with the chain |
| `DISPATCH` and `RESUME_HOP` (`eval.h`) | take the kind and stage or tail into `kind->fn` |
| `KosResumeFrame_Install` | takes the kind in place of the name and sets it at install; setting the state is all the step does afterwards |

The delivery step and the failure walk call `kind->fn`; the hop tails
into it through one load from a static record. What each user gains:
providers get their abandonment cleanup, yield-from loses nothing, and
a resume whose state is an integer becomes capturable for one identity
function.

What it touched: the frame union, the lifecycle sites, the two CPS
entry signatures and their callers in the provider suite and the
koscallables, the two macros, `KosResumeFrame_Install`, and the lines
setting a resume in the fourteen providers.

## What does not change

The three boundaries and every call into a Python callable are C
returns; the frame-depth limit exists for that recursion and the tail
chain does not touch it. FATAL stays a return.

## Testing

- Per change: the full suite; the trace build's trampoline-step
  counter, which now counts only raw fallbacks, on the churn kernels.
- Change 3: the exception reproducers and the raise-heavy kernels under
  `tox -qe churn`.
- Change 7: a step abandoned by a coroutine closed mid-suspension
  leaves its provider clear; capture across a yield-from phase still
  works; capture across a provider step still refuses by name.

## Related

- [EVALUATOR.md](../EVALUATOR.md) §1.2, §2.6, §3 -- the loop phases and
  the signal vocabulary this replaces.
- [../interpreter/electors.md](../interpreter/electors.md) -- the elector prologue, the first
  thread-function helper handlers hop into.
- `koskari/lib/eval.c` -- `koskari_eval`, `kos_eval_prologue`,
  `apply_kos_op_result`, `process_raise`, `kos_return_deliver`,
  `kos_yield_deliver`.
- `koskari/lib/eval.h` -- `TAILCALL`, `RESUME_HOP`, `RESULT_RELAY`,
  `RESULT_RAISE`, `RESULT_NEXT_POLL`.
