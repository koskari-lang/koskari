# Dynchain unification: design and implementation

Status: planned, nothing implemented. Baseline: `master` at `ee92028`
(PR #159). Line numbers below are from that commit.

**See also:** [extent-wind-unwind.md](extent-wind-unwind.md) (the extent
contract), [generator-extent-freeze.md](generator-extent-freeze.md) (the
freeze and install hooks this replaces),
[with-dyn-with.md](with-dyn-with.md), [../FLATTENING.md](../FLATTENING.md).

## 1. Goals

One node model for try/finally, with, dyn-with, dyn-wind, parameterize,
loops, generators, reset/shift/abort, call/cc and await, where every
chain operation costs in proportion to the nodes that have work to do,
not to the length of the chain.

In priority order:

1. **Generator re-entry does not get slower.** A yield or re-entry over
   a span with no skip-bit nodes costs O(1) in the chain code. With k
   such nodes it costs O(k) plus their own work. Only user thunks may
   cost more. A generator that uses no dynamic extents pays nothing for
   winding across a yield.
2. **The call path pays nothing measurable.** Frame push and pop stay
   within the budgets of section 9.
3. **Semantics change only where a decision in section 10 says so.**
   Abort-first stays: a shift is an abort that keeps; the handler runs
   outside the region's extents; with, try and finally are local to
   transfers.
4. **Each stage is completed alone**, with every test run green and its
   counters reported, before the next starts.
5. **An await suspension is never a transfer.** It leaves the chain
   where it is, runs no extent code, and cannot be captured across.

Not in scope: operand-segment ownership (regions keep their segments),
the transfer goal states (LEAVING, ATTACHING, ENTERING, DELIVERING), the
bytecode, the Python boundary. The bytecode ABI does not change.

## 2. Generators and coroutines

CPython builds coroutines on the generator mechanism. Koskari does not.

| | Generator yield | Await suspension | Await of a Koskari coroutine from Koskari |
| --- | --- | --- | --- |
| What it is | a transfer to the consumer, on the same thread of control | a cooperative thread release, a swap between threads of control | a call: the coroutine flattens onto the awaiter's chain |
| dyn-wind, dyn-with, parameterize | wind out at the yield, in at re-entry | nothing runs | nothing runs |
| with, try, finally | nothing runs (Python rule) | nothing runs | nothing runs |
| Runtime-owned state (parameter cells, handled exception) | swapped out and in | swapped only when the chain moves to another context | not applicable |
| Continuation capture across it | refused (GEN) | refused | allowed |
| close or throw | re-entry winds in, then the raise at the yield unwinds | raise at the suspension point; no re-entry, no before code | not applicable |

The consumer of a generator runs inside the same thread of control and
shares its parameter cells and handled exception, so the generator's
dynamic extents are left and re-entered. A suspended coroutine is a
thread that is not running: a descheduled thread runs no dynamic-wind
thunk. This keeps await compatible with free threading, where a
coroutine and a thread should look alike.

An async generator takes both rules: its yields wind, its awaits do
not.

Code follows the same line. No coroutine or await path calls the
generator freeze, install or winding functions. A chain moved between
contexts (section 4.4) shares only the per-node effect functions.

## 3. Baseline facts

| Item | At `ee92028` | Where |
| --- | --- | --- |
| Node header | 40 bytes: `parent`, `kind` (enum), `flags`, `caps`, `node_id` (32 bits each), `stack`, `ns` | `dynchain.h:531` |
| Node size | one size, payload union sized by WITH, 8-slot inline segment | `dynchain.h` |
| Node freelist | one list, `KOS_FREELIST_CAP(128)` | `dynchain.c:kos_dynnode_alloc` |
| Capability bits | `CATCH` 0, `LOOP_REGION` 2, `WINDS` 4, `CONTROL_HOOK` 6, `BARRIER` 7, `INTERCEPTS_RAISE` 10, `OWNS_NS` 11 | `dynchain.h:119` |
| `INTERCEPTS_RAISE` carriers | TRY, FINALLY, GEN | `dyn_try.c:37`, `dyn_finally.c:37`, `dyn_gen.c:56` |
| Direct parent writes | 30 over 9 files | section 6.1 |
| Whole-field caps writes | `kos_dynnode_init`, `func.c:1870` (`= OWNS_NS`), `dyn_frame.c:527` (`= 0`) | |
| Yield freeze | walks every span node, counts frames, calls `kos_dyn_extent_freeze` on each | `dyn_walk.c:27` |
| Install | recursive walk of the span when any effect left | `dyn_gen.c:176` |
| Effectful kinds | PARAM always; TRY and FINALLY while handling | `dynchain.c:1112` |
| `kos_dyn_extent_freeze` | does the swap and returns whether it did | `dynchain.c:1112` |
| Resting span | bottom is the generator's root frame; the GEN rests apart in `state->node` and is relinked at each install | `dyn_gen.c:65`, `:287` |
| Span detach, attach | whole-chain walks, in the generator file, through the generator hooks | `dyn_gen.c:467`, `:505` |
| Splice room check | `kos_dyn_frame_count` over the whole chain | `coro.c:1027`, `iterate.c:1054` |
| Frame depth | parent frame's plus one at every push; a tail swap keeps it; `kos_dyn_renumber_frames_counted` numbers top down | `eval.h:1346`, `dyn_frame.c:356`, `:423`, `:628`, `:706` |
| Lookups by kind | `kos_dyn_find_gen`, `kos_dyn_find_cc`, `kos_prompt_seek`, `KosDynNode_IS_FRAME` | `dyn_gen.c:126`, `dynchain.c:2677`, `:2829` |
| PARAM on a transfer | has no leave (`kos_dyn_extent_leave` raises for it); its unlink is `kos_dyn_param_unwind` in the uninstall handler | `dynchain.c:979`, `:2296` |
| Plain WITH on a transfer | aborted, not left | `dynchain.c:2153`, `:2399` |
| FOREACH on a keep | `kos_dyn_foreach_keep` | `dynchain.c:2332` |
| Shift | keeps the live nodes into the template; does not clone them | `xfer_goal.c:469`, `dynchain.c:2381` |
| Clone attach | already in pieces split at `WINDS` nodes | `xfer_goal.c:575`, `KosCloneShape.pieces` |
| Delimited apply | clones the whole template on every call | `xfer_plan.c:36` |
| Yield-from | the inner generator freezes into its own state per element; the outer frame then emits through its resume | `opcode/YIELD_FROM.c` |
| Fresh coroutine awaited | marked done at the take; its frame replaces the await frame | `coro.c:1094`, `:1144` |
| Spliced coroutine awaited | chain attached under a native frame whose resume kind has no clone and finishes the object | `coro.c:960`, `:1018` |

## 4. Design

### 4.1 One capability field

`caps` is 16 bits and holds capabilities and roles alike. `kind` is the
payload's discriminator and selects a node's own behaviour (construct,
clone, clear, traverse, describe). A node is found by a bit, never by
kind. A switch on kind is allowed on a node already in hand.

| Bit | Name | Meaning | Kinds | Baseline |
| --- | --- | --- | --- | --- |
| 0 | `KOS_DYN_CAP_INTERCEPT` | may consume or deny a transit | TRY, FINALLY, GEN, PROMPT | `INTERCEPTS_RAISE`, plus PROMPT |
| 1 | `KOS_DYN_CAP_LEAVES` | runs user code on a local unwind; has an obligation slot | WIND, WITH | `CONTROL_HOOK` |
| 2 | `KOS_DYN_CAP_WINDS` | a transfer and a yield run it out and in | WIND, dyn-with, PARAM | `WINDS` |
| 3 | `KOS_DYN_CAP_EFFECT` | holds runtime-owned state outside itself | PARAM, TRY, FINALLY | kind switch in `kos_dyn_extent_freeze` |
| 4 | `KOS_DYN_CAP_BARRIER` | a plain pop refuses to unlink it | PROMPT | `BARRIER` |
| 5 | `KOS_DYN_CAP_LOOP_REGION` | break/continue target while its body runs | LOOP, FOREACH | `LOOP_REGION` |
| 6 | `KOS_DYN_CAP_CATCH` | may swallow an exception during its unwind | WITH | `CATCH` |
| 7 | `KOS_DYN_CAP_OWNS_NS` | `ns` is the node's own scope | FRAME with code, NS, COLLECT | `OWNS_NS` |
| 8 | `KOS_DYN_CAP_FRAME` | an activation record | FRAME | `KosDynNode_IS_FRAME` |
| 9 | `KOS_DYN_CAP_GEN` | a generator span's root marker | GEN | `KosDynNode_IS_GEN` |
| 10 | `KOS_DYN_CAP_MAJOR` | owns an operand segment; selects the freelist | all but PROMPT, CC, GEN | new |
| 11 | `KOS_DYN_CAP_CC` | a call/cc block | CC | `KosDynNode_IS_CC` in `kos_dyn_find_cc` |

`KOS_DYN_CAP_SKIP` is bits 0 to 3. FRAME, CC, NS, COLLECT, LOOP and
FOREACH carry no skip bit. Every BARRIER or GEN node has INTERCEPT, so
neither needs a place in the mask.

`FRAME`, `MAJOR`, `GEN` and `CC` are set at init and never cleared.

### 4.2 The skip link

`skip` is the nearest ancestor with a skip bit, or NULL. The header
stays at 40 bytes: `kind` and `flags` become `uint8_t`, `caps`
`uint16_t`, `node_id` stays `uint32_t`, and `KosDynNode *skip` takes the
eight bytes freed.

```c
static inline void kos_dynnode_link(KosDynNode *node, KosDynNode *parent) {
  KosDynNode_PARENT(node) = parent;
  node->skip = ! parent ? NULL
             : (KosDynNode_CAPS(parent) & KOS_DYN_CAP_SKIP) ? parent
             : parent->skip;
  kos_dynnode_note_parent(node);
}
```

Rules. The native tests check the skip links (`kos_dyn_skips_hold`).
The safety net does not: it turns a memory fault into an exception and
audits no structure.

- **R1.** A node may lose a skip bit at any time. A walk re-tests
  `caps` on every node reached through `skip`. Bits outside the mask
  never affect links.
- **R2.** A node may gain a skip bit only while it is `dyn_top`.
- **R3.** A segment that moves keeps its internal links. Its outbound
  links are repaired at attach. The nodes whose skip-bit ancestor lies
  outside the segment form its tail: from the bottom up to and
  including its lowest skip-bit node. They all name one node, `outer`.
  - A cut (a parent set to NULL to take a segment off) leaves `skip` as
    it is. Nothing walks a resting segment by `skip`.
  - A skip walk over a segment still attached stops at `outer`,
    computed before the cut.
  - Attach links the bottom with `kos_dynnode_link` and writes the same
    value up the tail.
  - A resting generator span's bottom is its root frame, and its tail
    names the GEN. The state reuses that GEN node, so the tail is
    already right at install and only the GEN is relinked: O(1). The
    install compares the bottom's `skip` with the GEN and relinks the
    span (`kos_dyn_relink_skips`) only when they differ.
  - A chain moved between contexts need not end in a GEN. The attach
    relinks the whole span with `kos_dyn_relink_skips`.
  - A clone attached in pieces repairs the first piece's tail only. A
    later piece's bottom goes back onto the split point it was cut
    from, which is its `skip` already.
- **R4.** Clones and kept nodes.
  - `kos_dyn_clone_segment` walks top down. Clones with no skip-bit
    ancestor yet are threaded through their own `skip` fields; a clone
    with a skip bit resolves them all. Those still pending at the end
    are the tail: their `skip` is set to NULL and
    `KosCloneShape.tail` records the lowest skip-bit clone.
  - A node kept by a shift keeps its links. One whose `skip` is the
    live PROMPT is re-pointed at the template's prompt clone.

### 4.3 Effects at a yield

A generator's state holds an effect list (`KosDynEffects`): the span
nodes whose state is swapped out while it rests, innermost first, the
order they go out in. They go back in from the list's end.

- The freeze is a skip walk from `dyn_top` to the GEN. It collects with
  a predicate that changes nothing, then acts. The walk fills the room
  the list has as it counts, and is made a second time only when the
  list had to grow. The list's allocation is the only step that can
  fail, and it fails before the chain changes: `MemoryError` at the
  yield, span and effects untouched (`kos_dyn_gen_freeze` answers -2
  and the yield raises).
- A span with no skip-bit node is one compare in the freeze: the top's
  skip link is the GEN.
- Two entries are inline. A longer list uses a heap array the state
  keeps and grows, so a step never allocates once the array is sized.
- The install walks the list and nothing else. An empty list does
  nothing.
- A span's frame count is not stored. Depths are contiguous, so it is
  the top frame's depth minus the bottom frame's plus one.

### 4.4 A chain moved between contexts

A coroutine or a generator suspended at a Python await on its own
context, then awaited from Koskari, has its whole chain moved onto the
awaiter's. This is a context switch (D7): no leave, no enter, no thunk.
Only `EFFECT` nodes act, through the effect functions. The code lives
in its own fragment and has its own effect list in `KosDynSpan`.

The room check is the depth of that context's top frame, since its
lowest frame is numbered 1.

### 4.5 One exception slot

```c
typedef struct KosExcSlot {
  KosExcState saved;
  uint8_t state;   /* KOS_EXC_NONE, KOS_EXC_HANDLING, KOS_EXC_FROZEN */
  uint8_t lent[3]; /* the embedding payload's own small fields */
} KosExcSlot;
```

Embedded in TRY, FINALLY and WITH in place of `saved`, `exc_saved` and
`exc_frozen`. `kos_exc_slot_out` and `kos_exc_slot_in` are the only
exchange code; `kos_exc_slot_displace` and `kos_exc_slot_leave` are
the owner taking over as handler and giving the displaced state back.
FINALLY keeps its separate `handling` state.

The state byte would cost each payload a word of padding, and WITH is
the largest payload, so the node would grow to 240 bytes. The slot
lends the rest of that word instead: `enabled`, `handling_raise`,
`handling_control`, `self_bind`, `entered` and `phase` live in `lent`
behind their accessor macros. TRY stays 40 bytes, FINALLY 64, WITH
104, and the node 232.

### 4.6 One extent machine

`KosExtent { KosObligation ob; }` in WIND and WITH, with the phase and
the entered flag in the obligation's `lent` bytes (`KosExtent_PHASE`,
`KosExtent_ENTERED`), as the exception slot lends its own: an extent
is 32 bytes, the obligation's size. One machine implements enter,
leave, settle and abort, answering NORMAL, REPROLOGUE, AWAIT or RAISE.
The kind supplies two calls: WIND runs its before or after Code
constant as an aside, WITH calls `__enter__` or `__exit__`.
`KosDynWindPhase` and `KOS_WITH_PHASE_*` go.

The phases are `KOS_EXTENT_IDLE`, `_ENTERING`, `_LEAVING` and `_LEFT`.
A WIND's former POST_BEGIN is IDLE with `entered` set.

### 4.7 One walker

```c
typedef struct KosTransit {
  uint8_t event;   /* KosXferEvent */
  uint16_t offer;  /* KOS_DYN_CAP_INTERCEPT or 0 */
  uint16_t run;    /* KOS_DYN_CAP_LEAVES, KOS_DYN_CAP_WINDS, or 0 */
  uint8_t fate;    /* FREE, KEEP */
} KosTransit;
```

`kos_dyn_walk(ctx, const KosTransit *t, stop)`, `static inline` with
`always_inline`, over a `static const KosTransit` per caller.

- **Visiting** (FREE, KEEP): every node above the stop leaves. A node
  with no skip bit costs one test and its release. A node with a skip
  bit is offered the event when its caps meet `t->offer`, then:
  - caps meet `t->run`: `kos_dyn_leave`;
  - `LEAVES` not met by `t->run` (a plain with on a transfer):
    `kos_dyn_extent_abort`;
  - `EFFECT` with FREE: the release restores it;
  - `EFFECT` with KEEP: `kos_dyn_effect_out`.
  PARAM has no `kos_dyn_leave` case: it leaves by its release on FREE
  and by its effect on KEEP. The release is by kind on the node in
  hand; FOREACH's keep action stays there.
- **Skipping** (nothing leaves): follow `skip`. The freeze, the context
  move's detach, the abort's prompt seek, `kos_dyn_find_gen`,
  `transform_chain_gens`.

Per-bit dispatch is a `switch` on kind inside one `static inline`
function per bit: `kos_dyn_offer`, `kos_dyn_leave`, `kos_dyn_enter`,
`kos_dyn_effect_live`, `kos_dyn_effect_out`, `kos_dyn_effect_in`. No
indirect call on these paths.

Two walks stay visiting loops over parents:

- `kos_dyn_find_loop`: the region must be in the running frame, and a
  frame has no skip bit.
- The shift's prompt seek: `kos_dyn_node_capturable` is asked of every
  node.

### 4.8 Two node sizes

| Size | Kinds | Bytes |
| --- | --- | --- |
| Minor (no operand segment) | PROMPT, CC, GEN | 56: header 40, largest minor payload 16 |
| Major | the rest | header, largest major payload, inline segment |

A node's size is not stored. `MAJOR` selects the freelist at alloc,
clone and free. Caps: minor 64, major 128, both through
`KOS_FREELIST_CAP`.

### 4.9 Dynamic extents wind across a yield

The freeze appends every `WINDS` node of the span to the effect list
with the live `EFFECT` nodes, in chain order.

- A node with `EFFECT` runs its effect functions. A `WINDS` node
  without `EFFECT` runs `kos_dyn_leave` and `kos_dyn_enter`. PARAM has
  both bits, and its effect is its wind.
- **Yield.** Innermost first. Before a winder's leave, the span is cut
  just above it, so the thunk runs with the chain as it stood outside
  the nodes it encloses and every inner effect already out.
- **Re-entry.** Outermost first. The span is attached up to and
  including the next winder, its enter runs, then the effects up to the
  following winder go in.
- A thunk may install a frame or suspend, so both run through the
  transfer goal machinery as a goal with no target: LEAVING without
  unlinking at the yield, ENTERING over the attached pieces at
  re-entry. The existing piecewise attach (`KosXferPiece`) is the
  model.
- A step taken from Python reaches the install outside a running goal.
  With a winder in the list the install stages an ENTERING goal and the
  step's `koskari_eval` runs it before delivering the sent value. A
  list with no winder installs inline.
- The generator is busy for the whole sequence. A re-entrant `next` or
  `close` from a thunk raises as a re-entrant step does.
- `KosExtent.entered` records which nodes are wound in.
- Yield-from needs nothing: the inner generator winds out at its own
  yield, the outer at its emit, and re-entry runs outer then inner.

Failures propagate by the ordinary raise walk from the level of the
chain where they happen:

- **Leave fails at a yield.** The node is still on the chain. The raise
  walks outward from it; a handler in the generator may catch it;
  otherwise it leaves through the GEN. No value is presented and the
  generator is not frozen.
- **Enter fails at re-entry.** The chain holds the extents outside the
  failing node. The raise unwinds those. The part not yet attached
  never entered and is freed as a transfer frees nodes.
- **close and throw at a yield.** Re-entry winds in, the exception is
  raised at the yield, the raise unwinds. Each thunk runs once.
- **Finalizer close at a yield.** The same, on the generator's own
  context. An error is reported as unraisable.
- **close of a coroutine suspended in an await.** A raise at the
  suspension point with no re-entry. Only the ordinary unwind runs.

### 4.10 Capture across a flattened coroutine

A coroutine awaited from Koskari is finished as an object at the moment
its program moves to the awaiter, however it arrived:

- Fresh: marked done at the take (baseline).
- Suspended at a Python await after a Python `send`: marked done once
  its chain is attached under the awaiter's native frame.

That frame's resume kind holds no state. Its resume returns the value
from the frame; it has no clear; its clone hands the NULL state on. The
frame clones, keeps and moves like any other, so a shift or call/cc
across it captures.

A real await suspension in flight (a Python awaitable being stepped)
still refuses capture.

In comments and documents this frame is a coroutine frame, not an await
frame.

### 4.11 Moving an unreachable template

Continuations stay multi-shot. When a delimited continuation is
reachable only from the call in progress, the apply attaches the
template itself and skips the copy. The attach fix-ups (frame
numbering, register settling, tail repair) remain. If no call route
gives an exact reachability test, this stage is dropped; nothing marks
or restricts a continuation.

## 5. Stages

| Stage | Title | Needs |
| --- | --- | --- |
| 0 | Kernels and baseline counters | |
| 1 | Capability field and skip link | 0 |
| 2 | Yield and re-entry by effect list; context move separated | 1 |
| 3 | Exception slot | |
| 4 | Extent machine | |
| 5 | Walker, one walk per commit | 1 |
| 6 | Two node sizes | 3, 4 |
| 7 | Winding across a yield | 2, 4, 5 |
| 8 | Capture across a flattened coroutine | |
| 9 | Template move (dropped, section 6.9) | 1 |

Gate for every stage: `make test`, `make test-native`, `make test-asan`
and `make test-valgrind` green; counters taken with
`tox -qe perf -- -- --log <file>` against the stage's budget; a miss is
reported with its numbers before the next stage starts. One stage per
commit, except stage 5.

## 6. Implementation

### 6.0 Stage 0: kernels and baseline

Add under `examples/churn/ki/`, with a Python twin under
`examples/churn/py/` where one fits, registered in
`examples/churn/registry.py`:

- `gen_depth`: a generator yielding inside N nested `let` scopes, N in
  {0, 4, 16}, stepped by `for-each`. Confirm first that `let` pushes an
  NS node in the compiled code; otherwise use another extent with no
  skip bit.
- `gen_try`: the same with a try handling an exception around the
  yield.
- `raise_unwind`: a raise through K frames to a handler, unless a
  kernel covers raise already.
- `gen_wind`: a generator yielding inside a dyn-wind with trivial
  thunks, and inside a parameterize.

Take the counters at `ee92028` in the environment every later stage
uses.

### 6.1 Stage 1: capability field and skip link

`dynchain.h`:

- Header: `kind` and `flags` to `uint8_t`, `caps` to `uint16_t`, add
  `skip`. `KosDynKind` stays an enum type for parameters; the field
  narrows. Check every `KosDynNode_KIND`, `_FLAGS` and `_CAPS` use that
  takes the address or assumes the width.
- Redefine the bits per 4.1. Rename `CONTROL_HOOK` to `LEAVES` and
  `INTERCEPTS_RAISE` to `INTERCEPT` everywhere.
- `KosDynNode_IS_FRAME`, `_IS_GEN` and `_IS_CC` test bits.
- Add `kos_dynnode_link`, and `tail` to `KosCloneShape`.

The caps fixed for a kind come from one table, `kos_dyn_kind_caps` in
`dynchain.c`, which `kos_dynnode_init` adds to what its caller passes.
An init site passes only what varies within its kind.

Whole-field caps writes become bit operations:

- `func.c:1870`: `|= KOS_DYN_CAP_OWNS_NS`.
- `dyn_frame.c:527` (`kos_dynframe_cache_inert`):
  `&= ~KOS_DYN_CAP_OWNS_NS`.

`kos_dyn_node_same_phase` (`dynchain.c:2453`) compares whole `caps`
fields. That stays correct: the new bits are fixed per kind and copied
by the clone.

PROMPT gains `INTERCEPT`, so the raise walk's cap test
(`dyn_walk.c:571`) now calls `kos_dyn_handle` on a prompt, which passes.
No behaviour change; counted in the stage 5 raise budget.

Parent writes:

| Site | Write | Becomes |
| --- | --- | --- |
| `dynchain.c:140` `kos_dynnode_init` | parent at init | `kos_dynnode_link` |
| `dynchain.c:248` freelist | link reuse | unchanged |
| `dynchain.c:723`, `:2180`, `:2187`, `dyn_frame.c:522`, `:571`, `:640`, `dyn_loop.c:126`, `func.c:1866` | NULL as the node is freed or cached | raw write: the freelist or cache link sets `skip` at reuse |
| `dynchain.c:1407`, `:1464` `kos_dyn_clone_segment` | clone links | R4 pending list |
| `dynchain.c:2412` `kos_dyn_unlink_top_keep` | NULL on a kept node | cut: `skip` kept |
| `xfer_goal.c:475`, `:476`, `:479` `goal_keep_node` | kept node into the template | raw writes; re-point a `skip` naming the live prompt (R4) |
| `dynchain.c:2787`, `xfer_plan.c:104` | clone bottom off its stop | cut |
| `xfer_goal.c:599` | rest of the target off its split point | cut |
| `xfer_goal.c:606` `goal_attach` | piece bottom onto its base | `kos_dynnode_link`; first piece repairs the tail from `shape->tail` |
| `eval.h:1338` frame push | parent is `dyn_top` | written out in `kos_dyn_push_ready_frame`: a frame on a frame copies its parent's `skip` inside the parent-kind test it already makes |
| `dyn_frame.c:618` tail swap | ready frame takes the live frame's parent | parent and `skip` copied from the live frame; the live frame is `dyn_top`, so nothing names it |
| `dyn_walk.c:78` freeze | span bottom off the GEN | cut |
| `dyn_gen.c:68` `kos_dyngen_rest` | GEN off the consumer | `kos_dynnode_link(gen, NULL)` |
| `dyn_gen.c:287`, `:419` | GEN onto `dyn_top` | `kos_dynnode_link` |
| `dyn_gen.c:288`, `:406` | span bottom onto the GEN | `kos_dynnode_link` |
| `dyn_gen.c:541` `kos_dyn_span_attach` | root onto `dyn_top` | `kos_dynnode_link`, then `kos_dyn_relink_skips` over the span |

Lookups to convert: `kos_dyn_find_gen`, `kos_dyn_yield_gen`
(`dyn_gen.c:126`), `kos_dyn_find_cc` (`dynchain.c:2677`), the PROMPT
and GEN cases of `kos_prompt_seek` (`dynchain.c:2838`, by `BARRIER` and
`GEN`), `dyn_gen.c:379`, `eval.c:841`, `:2771`, `:2850`, `:3036`,
`transform.c:429`, `iterate.c:1060`. Grep `KosDynNode_KIND(` and
`KosDynNode_IS_` in conditions outside a behaviour switch and list any
left.

The native tests check the skip links after a span install, a
context-move attach and a clone attach. No safety-net check reads them.

Nothing else reads `skip` in this stage.

### 6.2 Stage 2: effect list and the context move

`dynchain.h`:

- `KosGenState`: drop `nframes` and `installs`; add `fx_count`, the
  inline pair and the kept heap array. Free the array in
  `kos_gen_state_clear` (`dyn_gen.c:85`).
- `KosDynSpan`: drop `nframes` and `installs`; add `top_frame` and an
  effect list of its own.

`dynchain.c`:

- Split `kos_dyn_extent_freeze` (`:1112`) into `kos_dyn_effect_live`
  (PARAM: always; TRY, FINALLY: handling and not frozen, the test at
  `:1034`) and `kos_dyn_effect_out`. `kos_dyn_extent_install` (`:1133`)
  becomes `kos_dyn_effect_in`. The three are static to `dynchain.c`,
  beside the list functions `kos_dyn_effects_collect`, `_out` and
  `_in` that the freeze, the install and the context move share.
- `kos_dyn_keep_leave` (`:2326`) calls `kos_dyn_effect_out` for PARAM,
  TRY and FINALLY.

`dyn_walk.c:kos_dyn_gen_freeze`:

- Replace the loop at `:89` with a skip walk from `dyn_top` that stops
  at the GEN: count the live effects, size the array, fill it, then run
  `kos_dyn_effect_out` innermost first.
- The walk and the allocation run before `:78` cuts the span.

`dyn_gen.c`:

- `kos_dyn_gen_install` (`:212`): count is
  `DEPTH(state->frozen.frame) - DEPTH(state->bottom) + 1`, or 0 with no
  frozen frame. Replace `kos_dyn_gen_install_span` (`:176`) with a loop
  over the list; keep its roll-back of the entries already put back
  when one fails.
- `kos_dyn_gen_install_bootstrap` (`:352`) keeps its counting walk: it
  runs once per generator.
- The computed counts get no safety-net audit. A native test compares
  them with an `IS_FRAME` walk.

New fragment `dynchain/dyn_span.c`, included from `dynchain.c` and
added to `depends` in `setup.py`:

- Move `kos_dyn_span_detach` and `kos_dyn_span_attach` there from
  `dyn_gen.c:467` and `:505`.
- Detach: skip walk to `outer`, effect functions only, the thread
  exception swap at `:484` kept. Record `top_frame`. The walk does not
  reach a bottom with no skip bit, so the span records no root: it
  records `gen`, the GEN at the chain's bottom when the outermost
  skip-bit node is one with no parent. The detach can refuse, with
  `MemoryError` and nothing changed, when its list cannot be sized.
- Attach: room check from `DEPTH(top_frame)`; effects in from the
  span's list; then one walk that renumbers the frames, sets their
  demand bit and finds the bottom; `kos_dyn_relink_skips` repairs the
  skip links. Nothing is renumbered before a refusal, so a second
  attach reads the same count. The renumber and the demand-bit
  assignment stay (D2), as that one walk.
- `kos_dyn_span_drop` gives up the list of a span that could be
  attached nowhere.

`coro.c:1027` and `iterate.c:1054`: the room check reads the depth of
the context's top frame.

### 6.3 Stage 3: exception slot

- `dynchain.h`: `KosExcSlot`; replace the fields at `:616`-`:621`,
  `:628`-`:630` and `:657`, `:663`.
- `dynchain.c`: `kos_dyn_region_exc_slots`, `_exc_freeze`,
  `_exc_install`, `kos_dyn_region_frozen` (`:1002`-`:1095`) reduce to
  the two slot functions; `kos_dyn_region_leave_exc` (`:765`),
  `kos_dyn_install_regions` (`:1098`) and the clone's `frozen` test
  (`:1404`) read the slot state.
- `dyn_walk.c:639`-`:650`: the raise walk fills the slot.
- `kos_dyn_node_same_phase` (`:2458`, `:2467`) compares whether each
  slot holds anything: a template's region is FROZEN where the live
  one is HANDLING, and the two are the same phase.
- `dyn_with.c`: the WITH's `exc_saved` and `saved`.
- `sizeof` of the three payloads is unchanged: 40, 64 and 104.

### 6.4 Stage 4: extent machine

- `dynchain.h`: `KosExtent`; replace `ob`, `entered`, `phase` in WIND
  (`:610`) and WITH (`:651`); delete `KosDynWindPhase` (`:162`) and
  `KOS_WITH_PHASE_*`.
- The WITH payload is `mgr`, `enter`, `exit`, `name`, `resume_pc`, the
  extent, then the slot, whose `lent[0]` is `self_bind`: 104 bytes,
  unchanged, and still the union's largest. WIND drops from 56 to 48.
- One machine in `dynchain.c` behind `kos_dyn_extent_enter`, `_leave`,
  `_settle`, `_abort`, `_in_flight` (`:956`-`:1187`). `dyn_wind.c` and
  `dyn_with.c` keep payload setup, their two calls
  (`kos_dyn_wind_call_enter`, `_call_leave`, `kos_dyn_with_call_enter`,
  `_call_leave`), what each does with a returned call's result, clone
  and traverse.
- `kos_dyn_node_ob` (`:800`) returns `&extent->ob`.
- Update the phase reads in `kos_dyn_node_same_phase` (`:2473`,
  `:2480`), `dyn_with.c:116` and the capturable tests.
- `kos_dyn_node_same_phase` matched a live WIND only in POST_BEGIN,
  which a wind with no before segment never reached. That is kept: a
  live WIND matches its template only when it has a before segment.
- A WIND that was left before it entered no longer runs its before
  segment on a later enter, as a WITH never did. Nothing enters a node
  that has left.
- The WITH's check for a live frame at a settle applies only while a
  call is in flight.

### 6.5 Stage 5: walker

Add `KosTransit`, `kos_dyn_walk` and the per-bit functions to
`dynchain/dyn_walk.c`. Then one commit per port, each compared with the
loop it replaces:

1. `kos_dyn_unwind_raise` (`dyn_walk.c:461`). The frame crossing, the
   GEN teardown and the wind exception preserve stay in the wrapper.
2. `kos_dyn_unwind_return` (`:223`).
3. `kos_dyn_unwind_to_loop` (`:308`); `kos_dyn_find_loop` stays.
4. `kos_dyn_unwind_to_node` (`:148`).
5. `goal_leave` with FREE (`xfer_goal.c:503`, over
   `kos_dyn_unlink_top_take`, `dynchain.c:2123`).
6. `goal_leave` with KEEP (over `kos_dyn_unlink_top_keep`, `:2381`).
7. Skipping walks: `kos_abort_prompt` (`:2870`), `kos_dyn_find_gen`,
   `transform_chain_gens` (`transform.c:415`).

A port that misses its budget after one optimization attempt keeps its
own loop over the shared per-bit functions, and the gap is reported.

As implemented:

- `KosTransit` is the event and the `offer` mask. `run` and `fate` are
  not fields: `kos_dyn_unlink_top` already runs, aborts or restores a
  node by the event it is given, and that dispatch is not copied into
  the walker.
- The shared step is the test of the skip bits. A node with none
  leaves through `kos_dyn_release_top` (freed) or `kos_dyn_keep_top`
  (kept for a continuation): no offer, no unlink dispatch, no
  obligation, no goal bookkeeping. A node with a skip bit goes through
  `kos_dyn_handle`, the offer, and `kos_dyn_unlink_top` as before.
- `kos_dyn_walk` serves the return walk, the loop walk and
  `kos_dyn_unwind_to_node`.
- The raise walk keeps its loop, for the frame crossing, the GEN
  teardown and the wind exception preserve, and takes the same release
  for a node with no skip bit.
- `goal_leave` keeps its loop, in `xfer_goal.c`, a unit of its own,
  and takes the release or the keep for a node with no skip bit. FREE
  and KEEP are one port.
- The offer and the leave keep their names, `kos_dyn_handle` and
  `kos_dyn_extent_leave`; no `kos_dyn_offer` or `kos_dyn_leave` is
  added over them.
- Skipping walks: the abort's prompt seek, `kos_dyn_find_gen` and
  `transform_chain_gens` follow `skip`.

### 6.6 Stage 6: two node sizes

- `dynchain.c`: `kos_dynnode_alloc` (`:180`) takes the `MAJOR` bit;
  `kos_dynnode_clone_base` (`:209`) passes the source's;
  `kos_dynnode_return_to_freelist` (`:226`) reads it. Two lists, two
  trace labels, both drained in `koskari_dynchain_clear` (`:3258`).
- With `KOS_FREELIST_CAP` at 0 the size still comes from `MAJOR`.
- A minor node ends before `iseg`. Check that
  `kos_dynnode_use_inline_segment`, `kos_dyn_ensure_region_segment`
  (`:577`), `kos_dynnode_free` (`:1223`) and the traverse never touch
  `iseg` or the major payloads of a minor node; the asan run is the
  proof.
- Set the major size from the largest major payload measured after
  stages 3 and 4.
- No `sizeof(KosDynNode)` outside the allocator.

As implemented:

- `kos_dynnode_alloc` takes the kind and reads its `MAJOR` cap from
  the per-kind table; a constructor passes a constant, so the test
  folds away.
- The clone and the free do not read the cap either: each kind's clone
  and free know their size. A PROMPT and a CC clone from
  `kos_dynnode_clone_base_minor`, and a PROMPT, CC and GEN go back
  through `kos_dynnode_return_minor`, which has no segment to
  release. With the cap read at clone and at free, `shift_amb` and
  `shift_generator` cost 1.1% to 1.4% more; without, the continuation
  kernels gain 0.4% to 1.5%.
- `kos_dyn_kind_size` reports a kind's size, for the native tests.
- The major size is unchanged at 232 bytes: WITH is still the largest
  payload at 104.

### 6.7 Stage 7: winding across a yield

Tests first (section 8), then:

- `kos_dyn_gen_freeze`: the list also takes every `WINDS` node. With a
  winder present the yield stages a goal with no target whose LEAVING
  does not unlink; each step cuts the span above the next winder and
  runs its leave. The freeze completes when the last leave has.
- `kos_dyn_gen_install`: with a winder present, attach in pieces and
  stage ENTERING, reusing `KosXferPiece` and `goal_attach`
  (`xfer_goal.c:575`). The sent value is delivered after the last
  enter.
- `xfer_goal.c`: the two targetless goal forms, and the unwind of a
  failed re-entry through the entered nodes only.
- The yield sites: `eval.c:1803`, `:3362`.
- Steps taken from Python: `iterate.c`, `iter.c`,
  `eval/eval_iter.c:kos_close_drive`, `iter.c:asyncgen_finalize`. Each
  stages the goal and lets its `koskari_eval` run it.
- Busy state: `KosGenState.busy` covers the leave and enter sequences.
- User documentation and corpus records for dyn-wind, dyn-with and
  parameterize in generators change with D1. Each existing expectation
  that changes is reported before it is edited.

As implemented:

- **Two goal phases.** `KOS_GOAL_WINDING_OUT` and `KOS_GOAL_WINDING_IN`
  work through the generator's own records. Neither has a target clone
  or a seam, and neither runs `goal_leave` or `goal_attach`: the nodes
  stay the generator's, and a cut is one parent write.
- **The list is mixed.** The freeze lists the winders among the
  effects, in chain order, and answers 1 with nothing changed. The
  yield then records each winder's piece, the node resting on it and,
  as each is reached, the registers live with it at dyn_top
  (`KosXferPiece`, in `KosGenState.pieces`). Effects go out as the walk
  down the list passes them, so each leave runs with every inner effect
  out. Re-entry walks the list from its end.
- **The resting node is found by a parent walk**, from dyn_top down to
  the outermost winder, at a yield whose span has a winder. A yield
  written directly in a wind's body walks nothing. This misses the
  "one cut per winder" line of section 9 by the plain nodes inside the
  outermost winder. Recording the node at push was rejected: it puts a
  test and a store on every push onto a non-frame node, for every
  program. Leaving the upper pieces linked and bounding the goal's free
  and traverse by the winder's address would remove the walk; it gives
  up the rule that a held piece never points into a live chain, and is
  a tuning item.
- **One cut at a time.** The goal's `target` is the span's top while a
  cut exists. Going out, the cut above the next winder is made before
  the previous one is healed; going in, the same. A collection at any
  point sees the held nodes once.
- **A left extent is one never entered.** `kos_dyn_gen_wind_left` puts
  the extent back to IDLE with `entered` clear, so the enter path is
  unchanged and a generator dropped while it rests frees the node
  without a second leave. A suspended extent cannot be told from one
  never entered. Nothing needs the difference today. If something
  does, a `KOS_EXTENT_SUSPENDED` phase that the enter accepts is the
  remedy: the phase byte has room, and every phase switch gains a case.
- **A leave does not move the pc.** A with's exit moved the live pc
  past its block whenever it handled no exception, whoever had it
  leave, and the winding goal recorded the pc before each leave and
  put it back. A shift's abort did not: the next extent's leave frame
  flushed the moved pc into the frame being kept, and every copy of
  it resumed past the dyn-with with the node still on the chain. The
  move is now made only where the running frame's own leave completes
  later (`kos_dyn_with_exit_resume`, from `kos_dyn_settle_top`), and
  the goal's record is gone.
- **The plain yield and step execute nothing new.** A span with no
  skip-bit node takes the one-step path as before. A wound span rests
  with `effects.count` 0 (the list's length is in `wound_count`) and
  `frozen.top` NULL, so the install's existing tests send it to
  `kos_dyn_gen_install_winding` and no plain install tests for it.
  `kos_dyn_gen_freeze_wound` is the freeze specialised for it. The
  winder test sits in the default arm of the collect's kind switch, so
  a TRY, FINALLY or PARAM costs the walk nothing for it. The winding
  fields of `KosXferGoal` are written and read by winding goals only,
  and a goal given up goes through `kos_xfer_goal_discard`, which is
  where a winding goal empties its generator's lists; the transfer's
  own pop stays as it was.
- **A running consumer gets no frame to thread into.** The install
  leaves the frame register NULL on a running context, so the step's
  existing test (`kos_iter_installed`) sends it to the prologue, whose
  drain runs the goal; the goal loads the registers recorded for the
  outermost winder. A context that is not running gets those registers
  at once and `KOS_SUSP_WIND` as its suspension event, so its next
  entry hands the goal the sent value or the throw instead of reviving
  a yield.
- **Delivery after the last enter.** The goal carries the sent value
  or the thrown exception. A frame waiting on a resume takes it as its
  delivery, a throw through `KosContext_ThrowIntoResume`; any other
  frame takes the value on its stack or the raise at its yield. No
  placeholder is pushed for a wound span.
- **Failures.** A refusal while the outermost batch of effects goes
  back takes the span off its GEN again and leaves it resting. Any
  later failure raises on the chain as section 4.9 says, and the goal's
  death frees the pieces not attached.
- **`KosGenState` is 136 bytes**, 16 more: the pieces array, its count
  and capacity, and `wound_count`.
- **`gen_wind` costs 57% more.** Its two segments are `None`, and both
  now run at every yield and step: two frames, two goals (the second
  allocated), and two passes of the prologue. The other generator
  kernels are within 0.1% of stage 6, `gen_try` 0.14% under.
- **Tests.** `tests/reproducers/test_wind_across_yield.py`, 22 tests,
  and a native test of the cut and the attach
  (`test_wind_out_and_in`). No existing assertion changed. The prose
  that changed: the `dyn-wind`, `dyn-with` and `parameterize` reference
  pages, the YIELD entry of the VM reference, the `gen_wind` line of
  the churn README, and the docstrings of
  `tests/reproducers/test_param_generator_extents.py`.

Tuning, after every stage:

- **The winding goal is the generator's storage.** The allocation that
  holds `KosGenState.pieces` begins with one `KosXferGoal`, which the
  yield's goal and then the step's goal use in turn
  (`kos_xfer_goal_run_kept`, `in_place` 2). A yield and a step allocate
  nothing, a paused goal is not moved, and the install has no
  allocation to fail. `kos_xfer_goal_free` drops the generator's
  reference last, since that can free the storage. `gen_wind` fell
  3.9%, a tenth of what stage 7 added.
- **Where the rest goes**, by an exact count (callgrind) over a
  generator with one dyn-wind, per yield and step together: about 900
  instructions run the two segments (the aside frame's allocation and
  release, the fire, the settle, the return and its prologue pass) and
  about 1,300 are the goal's own work, in straight-line code across
  some twenty functions with no loop among them: `goal_wind_in` 226,
  `goal_drive` 223 over four calls, `goal_wind_out` 202,
  `eval_yield_unfrozen` 154, `kos_dyn_gen_install_winding` 106,
  `kos_dyn_resync_chain` 79, the drain 71. No single change removes
  much of it.
- **A fired segment's frame is threaded into.** A run that leaves its
  goal waiting on a frame a step installed answers NORMAL when the
  frame can be threaded into and the breaker is down
  (`goal_run_answer`), for every transfer, and `kos_yield_deliver`
  threads on that answer. The for-each and yield-from step run the
  goal a wound install pushed themselves
  (`kos_xfer_goal_run_pushed`), after the step has handed it what it
  sends, and relay the answer as an instruction's: a failed enter
  takes the raise ingress from there, as it did from the prologue's
  drain. Three of the five prologue passes of a yield and step went
  with that, and `gen_wind` fell 1.46%. A step through
  `kos_iter_installed` (a transform's request) still goes by the
  prologue.
- **A segment's return runs its goal.** The return of a frame onto a
  region node already takes the region arm of `kos_return_deliver`,
  and answers REPROLOGUE there only because a goal is set. When that
  goal will drive, the arm tail calls `kos_eval_goal_thread`, which
  runs it (`kos_xfer_goal_run_pushed`), rests and presents a yield
  whose last winder is left, and relays any other answer. No test is
  added to a return that takes an earlier arm. The other two prologue
  passes are gone, and `gen_wind` fell a further 2.20%.
- **A winder's registers are not kept between yields.** Keeping them
  would save the resync, about 80 instructions of 2,200, but they
  depend on the chain under the generator, which is the consumer's
  and may differ at every step. Reusing them would take a record on
  the generator of what the span was last attached onto, by a counter
  since an address can be reused, and a compare at each step. Not
  built.
- **A dyn-wind does not keep its segment's frame.** A wind node that
  held the frame its last segment returned from, for the next segment
  to run in, was built and measured on `wind_plain`, a kernel of one
  dyn-wind per step with no generator. A reuse cost 101 instructions
  against 120 for a new frame, the test for a frame worth keeping
  cost 40 at each return, and the step fell by 20 of about 1,900:
  `wind_plain` 0.51%, `gen_wind` 1.90%, with `async_generators` 1.81%
  over from a cause not found. It added a field on the wind node, a
  second node held per live dyn-wind and a traverse case. Not built.
  The frame's cost is its install (the flush of the live registers,
  the stamp of its segment, the load) and its return route, not its
  allocation (section 11).

- **Continuation kernels.** Against the commit before stage 1,
  `shift_generator` stood 1.34% over and `shift_amb` 1.12% over. An
  exact count put the capture's share in two functions the stage 5
  split had left out of line, each now with two callers:
  `kos_dyn_keep_node` and `goal_keep_node` are inlined again. The
  clone no longer threads every node through its skip field; it
  remembers the topmost clone still waiting for a skip-bit clone and
  walks down from it when one arrives, and a clone that meets none
  keeps the NULL it was made with. `shift_generator` is 0.41% over.
  `shift_amb` is 0.94% over, about 50 instructions per application:
  the skip links of each cloned node, the repair of the bottom run at
  the attach, and the caps copy. That is the price of the links, which
  `shift_abort` (4.2% under) and the generator kernels are paid from.
- **The yield's parent walk stays.** Leaving the pieces above the
  outermost winder linked, and bounding the goal's free and traverse
  by the winder's address, would save the walk over those plain nodes:
  about 4 instructions a node, one node in `gen_wind`, some 65
  instructions of 2,200 under sixteen nested scopes. It gives up the
  rule that a piece a goal holds never points into a live chain, and
  the bound would have to hold after a raise has freed the winder. Not
  built.
- **`gen_try`** is 0.8% under the commit before stage 1; nothing to
  tune.

### 6.8 Stage 8: capture across a flattened coroutine

`coro.c`:

- `coroutine_splice` (`:1018`): after a successful attach, mark the
  object done (`coroutine_finish`; its context is already released at
  `:1057`), drop the reference, and set the frame's resume with a NULL
  state. The `in_send` hold across the splice goes.
- `coroutine_stitched_kind` (`:998`): resume returns the value from the
  frame; no clear, no traverse; clone is `kos_resume_clone_shared`.
  Delete `coroutine_stitched_clear` and `coroutine_stitched_traverse`.
- Rename the kind and its comments to a coroutine frame. Update the
  terminology in [../FLATTENING.md](../FLATTENING.md) and
  [../EVALUATOR.md](../EVALUATOR.md).

`iterate.c:stepper_splice` (`:1054`): the same review for the stepper's
resume kind.

As implemented:

- **`coroutine_frame_kind`** replaces `coroutine_stitched_kind`. Its
  resume returns the value from the frame, it has no clear and no
  traverse, and its clone is `kos_resume_clone_shared`.
  `coroutine_splice` finishes the object and drops its reference once
  the attach has succeeded; `in_send` is no longer set across the
  splice.
- **The object is done while the awaiter is still suspended.** A
  `send` or `throw` there raises "cannot reuse already awaited
  coroutine", where it raised "coroutine already being awaited", and
  `close` returns None, where it raised. One existing assertion
  changed for this, in `test_splice_resuspends_on_the_parked_await`.
- **The await frame stays.** A fresh coroutine's frame replaces the
  await frame; a spliced chain is attached above it, so the stack has
  one native frame more than a call would leave. Its resume is the
  same shape as any native frame's that returns its callee's value.
- **`stepper_splice` needed nothing.** The stepper is spent at the
  splice already, and the await frame's resume is the step kind with
  the provider as its state, as for a flattened step. Capture across
  it stops at the generator's GEN.
- **No test for capture across a real await.** An await frame carries
  the revive kind, which has no clone, only while its context is
  suspended, and a suspended context runs no `shift`. The refusal is
  not reachable from the language.
- **Tests.** Eight added to
  `tests/reproducers/test_coroutine_splice.py`: done from the splice,
  a refused second await, the object dropped, a shift applied twice,
  a continuation applied after its reset returned, one dropped,
  `call/cc`, and an applied copy that awaits again.

### 6.9 Stage 9: template move

- Determine the reference count the invoke path holds on the
  continuation for each call route: the CALL opcodes, a transform
  callback, a `Demand` over a continuation.
- If one test is exact on every route: in `kos_invoke_prepare`
  (`xfer_plan.c:36`) for a delimited continuation, take the template in
  place of `kos_dyn_clone_segment`, compute its `KosCloneShape` by a
  walk, and leave the continuation empty.
- Otherwise drop the stage and report the routes.

As implemented: dropped. No code changed.

The continuation's install (`kos_vinstall_continuation`, `cont.c`)
takes the callable borrowed and is not told who holds it. The count it
reads there, by route:

| Route | References at the install | Reachable afterwards |
| --- | --- | --- |
| `CALL`, `CALL_NAMES`, `CALL_STAR` and their tail twins, callable computed onto the stack | 1, the stack slot | no |
| `CALL_FAST`, `CALL_VAR` and their variants | 2 or more: the variable, and the handler's owned read | yes, through the variable |
| `CALL_ATTR` and its variants | 2 or more: the attribute's holder, and the handler's owned read | yes |
| A transform's callback (`map`, `filter` and the rest, through `KosContext_Call`) | 1, the transform's field, borrowed for the call | yes: the transform calls it for the next element |
| A `partial` or a `Demand` over a continuation | 1, the wrapper's field, borrowed for the call | yes: the wrapper can be called again |

A count of 1 is read both on a route where nothing can call the
continuation again and on routes where the holder will, so the count
is not an exact test at the install. Making it exact needs each route
to say whether the reference it holds dies with the call, which is a
new argument on every `kc_vinstall`, or a mark on the continuation,
which D4 excludes.

The exact case is also the rare one. A continuation is named by its
`shift`, so an apply goes through `CALL_FAST` or `CALL_VAR` with the
variable still holding it: `shift_generator` applies `k` with three
holders (its variable, the tuple it arrived in, the handler), and
`shift_amb` applies each `k` more than once by design. `shift_abort`
applies none. Only a continuation applied straight off the stack,
never bound, would move, and no kernel does that.

The delimited apply keeps its clone, and the stage 9 row of the
budget and the "Delimited apply" row of the walks table do not apply.

## 7. Invariants

- I1. The chain is one parent-linked list from `dyn_top` to the root.
  Every node is owned by its context, a resting span by its generator's
  state, a template by its continuation.
- I2. `skip` obeys R1 to R4.
- I3. A generator's effect list names the span nodes whose effect is
  out while it rests, and after stage 7 its winders.
- I4. A clone or a kept node never carries `KOS_DYNF_GOAL_WAIT`.
- I5. A node never unlinks itself and never reads transfer state; enter
  and leave are total and re-entrant.
- I6. No walk allocates unless it builds a template, a clone or an
  effect list past the capacity it already has.
- I7. No await suspension runs a leave, an enter or a thunk.

## 8. Tests

`tests/native/` (C):

- Skip links after push, clone (skip-bit node at top, middle, bottom),
  keep, span freeze and install.
- Freeze and install with 0, 1, 2 and 3 effects; order innermost first;
  the array reused on the next yield.
- Effect list allocation failure at a yield: `MemoryError`, span and
  effects unchanged.
- Skip links after a context move whose chain does not end in a GEN,
  and after a template applied on another chain.
- `FRAME | MAJOR` survive the `func.c` and `dyn_frame.c` caps writes.
- Minor or major size of every kind, normal build and
  `KOS_FREELIST_CAP` at 0.

`tests/` (Python):

- A parameterize, a try handling an exception, and a with in a
  generator, across many yields, from a Koskari consumer and from a
  Python `next`.
- A parameterize and a dyn-wind inside a coroutine spliced from a
  Python `send`: the cell is swapped and no thunk runs.
- dyn-wind, dyn-with and parameterize across yields:
  - an after thunk raising at a yield, caught in the generator and
    uncaught;
  - a before thunk raising at re-entry with one and two extents outside
    it;
  - `close` and `throw` at a yield inside a dyn-wind, each thunk once;
  - the same through the finalizer, a raising thunk reported as
    unraisable;
  - a coroutine suspended in an await inside a dyn-wind, closed
    explicitly and by its finalizer: the after thunk runs, the before
    thunk never;
  - a dyn-wind inside a parameterize inside a dyn-wind around a yield,
    each thunk seeing the parameter value of its own position;
  - a yield-from whose inner generator holds a dyn-wind;
  - a Python `next` whose before thunk awaits.
- with, try and finally still run nothing at a yield.
- A shift across a coroutine spliced from a Python `send` captures; the
  continuation applies twice and drops; the coroutine reports done from
  the splice and refuses `send`, `throw` and a second await; capture
  across a real await suspension is refused.
- A continuation applied once and dropped, and applied several times,
  give the same results before and after stage 9.

## 9. Expectations

### Performance, on instructions minus the bootstrap floor

| Stage | Kernels | Budget |
| --- | --- | --- |
| 1 | `binary_trees`, `tail`, `call_kw`, `method`, `tree` | at most +0.5% each |
| 2 | `yieldfrom`, `async_generators`, `list_each`, `gen_depth` | no regression; `gen_depth` flat in N |
| 3, 4 | all | at most +0.3% |
| 5, per walk | raise: `raise_unwind`; return: `tail`, `binary_trees`; loop: `loop`, `list_each`; abort: `shift_abort` | at most +0.3%, target an improvement |
| 6 | all | no regression |
| 7 | the stage 2 kernels | unchanged from stage 2; `gen_wind` costs its thunks plus O(1) per winder |
| 8 | `coro_await`, `coroutines` | no regression |
| 9 | `shift_generator`, `shift_abort` | an improvement; no regression on `shift_amb` |

No stage may regress any kernel by more than 1%.

### Memory

| Shape | Baseline | After |
| --- | --- | --- |
| Major node | 232 B | 232 B: stages 3 and 4 kept WITH at 104 B and shrank WIND to 48 B |
| PROMPT, CC, GEN | 232 B | 56 B |
| Resting generator, no regions | 464 B | 288 B |
| reset body (PROMPT and frame) | 464 B | 288 B |
| `KosGenState` | 96 B | 136 B: 120 B after stage 2, and 16 B for the winders' records in stage 7 |
| Freelists at most | 29.7 KB | 33.3 KB |

Report how often the effect list leaves its inline pair on the kernels.

### Walks

| Operation | Baseline | After |
| --- | --- | --- |
| Yield | every span node | skip-bit nodes of the span |
| Re-entry | none, or every span node | the effect list |
| Context move room check | every node | one depth read |
| Context move detach | every node | skip-bit nodes |
| Context move attach | every frame | unchanged, with the tail repair |
| Abort prompt seek | every node to the prompt | skip-bit nodes |
| Shift seek and keep | every node, twice | unchanged |
| raise, return, break, continue | every node to the stop | same count; one test on a node with no skip bit |
| Delimited apply | clone, then fix-ups | unchanged: stage 9 was dropped |
| Yield and re-entry with k dynamic extents | not wound | k leaves and k enters, their thunks, one cut or attach per winder; the yield also walks the plain nodes inside the outermost winder |
| Await | none | none |

## 10. Decisions

| Id | Decision |
| --- | --- |
| D1 | dyn-wind, dyn-with and parameterize wind out at a yield and in at re-entry |
| D2 | The context move keeps its frame renumber and demand-bit loop; revisiting them is deferred |
| D3 | A real await suspension cannot be captured across. Flattening into a Koskari coroutine is a call and can |
| D4 | Continuations are never restricted. The template move applies only where no later call is possible, or is dropped |
| D5 | call/cc capture stays an eager copy |
| D6 | Two node sizes, minor and major |
| D7 | A chain moved between contexts is a context switch: effects only, no thunk |
| D8 | No copy on return for delimited apply in this work |
| D9 | A node is found by a bit. `FRAME`, `GEN` and `CC` are bits |
| D10 | A node's size is not stored; `MAJOR` selects the freelist |
| D11 | `KosDynSpan` and `KosGenState` stay separate records |
| D12 | A span's frame count is computed from depths |
| D13 | The freeze collects with a predicate that changes nothing, then acts |
| D14 | A generator state keeps its effect array across yields |
| D15 | The context move lives outside the generator code and calls only the effect functions |
| D16 | A spliced coroutine is finished at the splice, as a fresh one is at the take; its frame's resume holds no state |
| D17 | Yield-from needs no winding of its own |

## 11. Follow-ups, not part of this work

- Copy-on-write continuation capture, measured against the eager copy
  once every stage is complete.
- Copy on return for delimited apply; its cost is a shared-flag test on
  every frame return.
- The frame renumber and demand-bit loop of the context move (D2).
- An await of a Koskari awaitable without the await frame. It is
  harmless where it stays, under a spliced chain (section 6.8), and a
  fresh coroutine's frame already replaces it; the aim is not to push
  it at all.
- The cost of an aside frame, for each segment of every dyn-wind, in
  a generator or not: about 120 instructions to allocate, fill and
  install, 85 to release, and 124 on the return route. `wind_plain`
  measures it alone, at about 1,900 instructions a step. Keeping the
  frame on the wind node saves little (section 6.7), since most of
  the 120 is the install; a gain would come from a segment that runs
  without a frame of its own, or from a shorter return route.
