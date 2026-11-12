# Extent contract and the unlink primitive

How a dynamic extent enters and leaves the chain, for plain pops, the
unwind walks (return, raise, break/continue, yield), and transfer goals
alike. Behaviour is identical whatever brought control to the node: the
node answers the same questions and never reads why it is being asked.

---

## The extent contract (`KOS_DYN_CAP_CONTROL_HOOK`: WIND, WITH)

Four entry points on the node, dispatched by kind (`dyn_wind.c`,
`dyn_with.c`):

| Call | Meaning | Answers |
|---|---|---|
| `kos_dyn_extent_enter(es, node)` | establish the extent: run the before thunk, call `__enter__` | NORMAL, REPROLOGUE, AWAIT, RAISE |
| `kos_dyn_extent_leave(es, node, ev)` | tear it down: run the after thunk if entered, call `__exit__` with the exception the node holds | NORMAL, REPROLOGUE, AWAIT, RAISE |
| `kos_dyn_extent_settle(es, node)` | the pending call returned; take its result off the live block and advance the phase | 0 / -1 |
| `kos_dyn_extent_abort(node)` | the pending call died; advance without a result | -- |

NORMAL means the obligation is discharged. REPROLOGUE means a thunk or
`__enter__`/`__exit__` frame was installed above the node and the caller
should let it run. AWAIT (payload in `.val`) means the call suspended on an
awaitable. RAISE means the node's own code raised.

The contract is **total and re-entrant**: asking again is always legal. A
node in a pending phase (WIND `PRE_BEGIN` / `PRE_AFTER`, WITH
`PENDING_ENTER_AWAIT` / `PENDING_EXIT_AWAIT`) is **in flight**; enter and
leave answer REPROLOGUE while its thunk frame sits above it, and leave
aborts it once the node is `dyn_top`, since nothing is above it and the
thunk is gone (it raised, or a walk crossed it). The node never unlinks
itself and never reads the transfer state.

Phases: WIND `IDLE -> PRE_BEGIN -> POST_BEGIN -> PRE_AFTER -> POST_AFTER`;
WITH `IDLE -> PENDING_ENTER_AWAIT? -> (body) -> PENDING_EXIT_AWAIT? ->
LEFT`. A plain `with` is local to its body: it carries no
`KOS_DYN_CAP_WINDS`, so a transfer unlinks it without asking it to leave
(a call of its in flight is aborted) and never re-enters it. A `dyn-with`
carries the cap: a transfer leaves it and re-enters it on every
attachment.

Thunks and `__enter__`/`__exit__` bodies are ordinary frames in the same
`koskari_eval` loop. When one returns into the node (a frame return that
delivers into a non-frame parent) or its awaitable revives,
**`kos_dyn_settle_top(es, &out_return)`** settles the node, and when what
settled was a leave, unlinks it (unless a transfer goal is waiting on that
node, in which case the goal's drive does).

---

## Per-kind teardown

Every node leaves through **`kos_dyn_unlink_top(es, ev, allow_barrier)`**:

1. A `CONTROL_HOOK` node is asked to leave; any answer but NORMAL returns
   with the node still on the chain, and the caller waits and asks again.
   Under `KOS_XFER_EVENT_TRANSFER` a node without `KOS_DYN_CAP_WINDS` is
   local and is not asked: a call of its in flight is aborted
   (`kos_dyn_extent_abort`) and nothing else of it runs.
2. The synchronous per-kind teardown runs. A kind whose work must wait
   until the node is freed fills the unlink's after-free hook
   (`KosAfterFree`); a flattened GEN uses it to finish its generator once
   the node is gone, with any exception in flight held across the free.
3. The node's obligation is detached.
4. The parent becomes `dyn_top`; the node is freed.
5. The obligation is acted on by its kind and by `ev` (see Obligations).

| Kind | Enter | Synchronous teardown at unlink |
|---|---|---|
| PARAM | `kos_dyn_param_wind`: bind the cells (also re-bound when a goal re-enters the extent) | `kos_dyn_param_unwind`: restore what they displaced |
| WIND | before thunk (contract) | after thunk if entered (contract), then free |
| WITH | `__enter__` (contract) | `__exit__` (contract; under TRANSFER only for a dyn-with), then free |
| NS | owns the let namespace (set at push) | drop the node; the live namespace follows the chain top |
| TRY / FINALLY | (none) | restore the handler-visible exception state a consumed raise displaced |
| LOOP / FOREACH / COLLECT | (none) | drop the owned segment |
| YIELD_FROM | (none) | close the delegate |
| PROMPT / CC | (none) | bookmark bookkeeping only |
| GEN | (none) | a flattened GEN finishes its generator through the after-free hook; a root GEN refuses a transfer |

`allow_barrier` = 0 refuses a `KOS_DYN_CAP_BARRIER` node (PROMPT) with
RAISE; 1 crosses it.

---

## Obligations

A WIND or WITH owns one **`KosObligation`** slot (`kos_dyn_node_ob`): what
the node owes when it is unlinked.

| Kind | What it holds | Who stores it |
|---|---|---|
| `KOS_OB_VALUE` | the body value at `POP_WIND` or `POP_WITH` | the pop opcode |
| `KOS_OB_EXCEPTION` | the exception `__exit__` is handling, or one stored while the after thunk runs | the raise walk, `POP_WITH` on a raise |
| `KOS_OB_RETURN` | a return, break, or continue value crossing a leave in flight, with its path | the return and loop walks |
| `KOS_OB_SWALLOWED` | nothing: `__exit__` consumed the raise and None stands in for the body value | the with's exit classification |

Release at unlink, by kind and by event:

| Obligation \ event | `NONE` (plain pop, hook finished a leave) | `RAISE` | any transfer event |
|---|---|---|---|
| VALUE | pushed onto the revealed block | dropped | dropped |
| EXCEPTION | re-raised | chained as `__context__` of the exception in flight | dropped |
| RETURN | handed back through `kos_dyn_settle_top` for `KosEval_ResumeReturn` | dropped | dropped |
| SWALLOWED | None pushed | None pushed | None pushed |

A with whose `__exit__` swallowed a raise owes `KOS_OB_SWALLOWED`, and the
unlink puts `None` in place of its body value whatever event brought the
raise. The GC traverses the slot through the node.

---

## Consumers

1. **Plain pops** (`POP_WIND`, `POP_WITH`, `POP_DYN_WITH`, the region pops):
   call `kos_dyn_unlink_top(es, KOS_XFER_EVENT_NONE, 0)`; REPROLOGUE
   continues into the thunk frame, AWAIT goes to the driver, and the settle
   on return or revive finishes the unlink.
2. **Unwind walks** (`kos_dyn_unwind_return` / `_to_frame` / `_to_node` /
   `_to_loop` / `_raise`, `dyn_walk.c`): unlink node by node with the walk's
   event; a node whose leave is in flight stops the walk (reported through
   `out_pending`), and the settle resumes it. The raise walk stores its
   exception on a wind before running the after thunk; the return walk
   stores the value.
3. **Transfer goals** (`xfer_goal.c`): LEAVING unlinks with
   `KOS_XFER_EVENT_TRANSFER` down to the seam; ATTACHING and ENTERING
   alternate, outermost first, ENTERING asking `kos_dyn_extent_enter` of
   each `KOS_DYN_CAP_WINDS` node (WIND, PARAM, dyn-with) with only the
   extents outside it attached. The walks test that one cap, never the
   kind. After a leave or enter that answers REPROLOGUE or AWAIT the goal
   waits on that node. See [EVALUATOR.md §6](../EVALUATOR.md).

Suspension inside a thunk is ordinary: the thunk's own AWAIT reaches the
driver, the revive continues the thunk, and its eventual return settles the
node. Suspension of the protocol call itself (`__aenter__`, `__aexit__`, an
awaitable thunk result) is the AWAIT answer of enter or leave; the revive
finds the node at `dyn_top` in a pending phase and settles it.

**With / dyn-with:** context managers are specified in
[with-dyn-with.md](with-dyn-with.md).
