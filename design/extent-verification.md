# Extent verification, CLEAR semantics, and the dead-region sweep

Successor to the extent-related open items from
[archive/bytecode-processor.md](archive/bytecode-processor.md), which
carries the history. This doc stands alone: it describes what the
three existing layers already guarantee about extent structure, the
one verification that remains worth adding, and the questions that
gate it.

## What exists today

Extent structure is computed and checked in three places, each with a
different job:

- **The promotion pass** (`promote_tail_calls`,
  `koskari/compile/processor.py`) maintains a transient
  discardability stack over the pre-finalize rows: push on
  `EXTENT_DISCARDABLE` keys, pop on `EXTENT_POPS`, deny promotion when
  any open entry is non-discardable. It is a legality filter, not a
  verifier: it trusts the stream (`if extents: extents.pop()`
  tolerates underflow silently) and its judgments can be wrong only if
  the emitter produced unbalanced extents -- which the next layer
  refuses.
- **Bytecode finalization** (`derive_extents_fill`,
  `koskari/lib/bytecode.c`) walks the finished instruction stream at
  Code construction to build the extent table (`KosExtentDesc`:
  kind, push_pc, pop_pc, parent), and it already fails loudly on
  structural damage: POP with no matching PUSH, a POP kind that does
  not pair with the open PUSH, push_pc >= pop_pc, an unclosed extent
  at end of code, and nesting beyond the derivation stack. This is
  the layer the archived doc's "safetynet verification" asked for,
  and it already exists for balance and pairing. It lives in C at
  construction time and cannot be overridden or mocked out.
- **The runtime tail handoff** (`kos_dyn_drop_discardable_to_frame`,
  `koskari/lib/dynchain/dyn_frame.c`) uninstalls discardable nodes
  (NS, LOOP, FOREACH) above the frame at tail-call frame reuse and
  refuses any other open node kind with a RuntimeError. This is the
  backstop: even a hand-assembled TAIL op inside a try cannot
  silently discard an extent whose uninstall must run.

## The verification that remains

What no layer checks statically: a TAIL-family opcode sitting inside
a non-discardable extent. The promotion pass never emits that shape,
so today it can only arrive by hand-assembled bytecode, and the
runtime backstop catches it at execution. Promoting the check to
finalization would fail such code at construction instead.

The decided placement: **the check lives in `derive_extents_fill`**,
where the walk already holds the open-extent stack at every pc. On a
TAIL-family op, scan the open stack; any kind outside {NS, LOOP,
FOREACH} is a construction error naming the pc and the extent kind.
Cost is one opcode-class test per instruction plus a short stack scan
at tail sites. The discardable set then exists in three places -- the
processor's `EXTENT_DISCARDABLE` table, the runtime's switch, and the
finalize check -- and that duplication is deliberate, not a hazard to
engineer away. The Python table is the malleable experimentation
surface; the C copies are the legality rule. They are kept textually
and mechanically separate so that churn on the Python side cannot
silently rewrite what C enforces: a wrong Python-side tweak produces
bytecode that C refuses loudly, which is the boundary working. Do not
derive the copies from a shared declaration.

Not worth building: verification inside the promotion pass itself.
The pass runs pre-finalize in Python, where it can be bypassed,
patched, or fed synthetic rows; a check there would be advisory.
Anything the pass gets wrong is caught by finalization (structure)
or the runtime refusal (discardability), and those are the layers
with authority.

## Open question: CLEAR semantics

`CLEAR_TRY`, `CLEAR_FINALLY`, `CLEAR_LOOP`, and `CLEAR_FOREACH`
disable a node at runtime without popping it. Neither the extent
derivation nor the promotion pass gives them any meaning today: the
static extent runs [push_pc, pop_pc] regardless, and the pass keeps
denying promotion through a cleared try. Two consistent readings:

- **Close**: a CLEAR ends the extent's obligations, so a tail call
  after `CLEAR_TRY` but before `POP_TRY` could legally promote (the
  handler is disabled; only the node's memory remains, and the tail
  drop could release it). This buys promotion coverage for the
  try-body-tail shape at the cost of teaching all three layers that
  CLEAR downgrades a kind to discardable.
- **Restart**: a CLEAR re-enables the node within a live extent
  (the loop forms reuse it per iteration), so it says nothing about
  obligations and the conservative deny stands.

The loop CLEARs are restart-shaped and the try/finally CLEARs are
close-shaped, so the answer is probably per-kind, not global. Decide
before the finalize check lands, because the check must either treat
cleared extents as open (restart) or track CLEAR ops in the walk
(close).

## Settled: the two walks stay separate

The promotion pass's transient stack echoes the shape of the finalize
walk, one in Python over pre-finalize rows (labels and positions
still present), one in C over final instructions. The archived doc
asked whether the computation could be unified so nesting is computed
once; the answer is no, by design and not only by the representation
gap. The Python walk belongs to the compiler's experimentation layer,
where rules are cheap to tweak and juggle; the C walk is the ultimate
legality rule, expressed in its own terms and deliberately harder to
move. How each applies its logic stays separate along with the
rulesets themselves. The C walk is the authority; a Python walk that
drifts produces output the C layer rejects, and that rejection is the
designed failure mode.

## Deferred: dead-region sweep and label GC

Carried unchanged from the archived doc, last if at all. With a label
refcount service (one schema-driven pass over LABEL-carrying
operands), unreachable rows after an unconditional transfer up to the
next live label are deletable, which is what actually reclaims
constants from the pool; a label whose count drops to zero unblocks
rules that break at LABEL rows. Structured emission implies a dead
region contains balanced PUSH/POP pairs; the sweep must refuse a
deletion that would straddle a pair. Repeatable-segment detection is
graph-shaped and stays out of scope.
