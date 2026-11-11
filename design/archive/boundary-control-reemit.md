# Boundary Control Re-emit Model

**Status: COMPLETE and ARCHIVED (2026-07-18).**  Every migration step
is complete: the frame-crossing vs consumer-delivery split (thin **`RETURN`**,
**`eval_event_*`**, **`apply_kos_op_result`**), the unified RAISE ingress
(§5.3), and finally the generic dynnode boundary dispatch (§5.2) as the
capability-keyed dispatcher in `koskari/lib/dynchain/dyn_boundary.c`.
The speculative follow-on is
[gen-yieldfrom-passthru.md](gen-yieldfrom-passthru.md)
(archived, superseded by the node-style yield-from).

This document describes the boundary model where frame-local outcomes are
signaled first, then delivered to parent consumers after crossing a frame
boundary.

Companion docs:
- [kosevent-elimination.md](kosevent-elimination.md) (archived; implemented)
- [event-driven-node-delivery.md](event-driven-node-delivery.md)
- [generic-dynnode-protocol.md](generic-dynnode-protocol.md)
- [EVALUATOR.md](../EVALUATOR.md)


## 1. Problem statement

Boundary outcomes combine two responsibilities:

1) Transfer out of a frame (unwind/pop/freeze across frame and GEN boundaries)
2) Consumer delivery (stack push, PC rewrite, boundary node semantics)

**Opcode and eval staging (implemented):** `RETURN.c` pops operands and returns
`EVAL_RETURN(val)`; `eval_event_return` and `eval_parent_return_deliver` own
unwind, frame pop, and parent delivery. `YIELD` returns `EVAL_YIELD(val)`;
`eval_event_yield` owns walk/freeze and boundary delivery.

**Consumer routing (implemented):** NEXT/FOREACH/YIELD_FROM consumer
selection and delivery route through the single capability-keyed dispatcher
(§5.2); the per-kind semantics remain in their dyn helpers but are only
reachable through it.


## 2. Core model

Boundary outcomes are handled in two discrete stages.

### Stage A: signal out in the current frame

The opcode and local transition logic emit a local event:
- `RETURN`
- `YIELD`
- `RAISE`

Local unwind/extent logic runs in the current frame context only. Flattened
`AWAIT` remains evaluator suspension and is not folded into this staging
channel (see constraints in section 4).

### Stage B: handle re-emitted boundary event in parent context

After frame boundary crossing:
- parent frame becomes active
- evaluator re-emits a boundary-control event
- dynnode dispatch handles it using current active chain state
- default fallback applies if no dynnode consumes

This ensures "send signal out" and "handle signalled result" are distinct
operations.


### Implementation notes (eval-owned staging)

Boundary results use **`KosOpResult`** (`KOS_RES_*`). Opcodes return **`EVAL_*()`**
macros; **`apply_kos_op_result`** routes non-`NORMAL` results to **`eval_event_*`**
helpers. **`RETURN`** pops the value and returns **`EVAL_RETURN(val)`**;
**`eval_event_return_ng`** runs unwind, frame pop, and delivery.  A gen
consumer (the receiving boundary above the returning GEN) is delivered via
**`kos_dyn_iter_boundary_deliver_return`** (§5.2); other parents via
**`eval_parent_return_deliver`** (native handler or stack push, then
**`kos_dynnode_on_control_returned`**). **`YIELD`** returns
**`EVAL_YIELD(val)`**; **`eval_event_yield`** runs
**`eval_yield_handle_walk_and_freeze`**, then **`kos_dyn_find_iter_boundary`**
and, when a receiver is active, **`kos_dyn_iter_boundary_deliver_yield`**
(§5.2). Active **`PyErr`** flows through **`process_raise`** (the unified
RAISE ingress).


## 3. Boundary control event kinds

Required kinds:
- `BOUNDARY_RETURN` with payload `value`
- `BOUNDARY_YIELD` with payload `value`
- `BOUNDARY_RAISE` with exception payload

The evaluator owns normalization and ownership transitions for these payloads.


## 4. Semantics constraints

1) `pc` vs `acting_pc`
- `pc` is "next instruction target"
- `acting_pc` is "current instruction being executed"
- boundary handlers must use this convention consistently

2) Active chain is source of truth
- after freeze/unwind/pop, dispatch uses current `dyn_top` / `frame_top`
- no cached consumer-frame pointers on YIELD_FROM nodes

3) Flattened Koskari await behavior
- inner flattened `AWAIT` is evaluator suspension
- it is not rerouted as YIELD_FROM-local delegation state
- await behavior remains unchanged in this phase and is not part of
  boundary-control re-emit

4) YIELD_FROM parity target
- for flattened Koskari delegates, behavior should be as close as possible to:
  - NEXT-style boundary handling
  - plus delegation forwarding (`send`, `throw`, `close`)


## 5. Proposed architecture changes

### 5.1 Eval dispatch structure

Add explicit boundary re-emit handling in eval loop:

1. receive local event from opcode/transition
2. perform frame-boundary crossing work
3. emit boundary-control event kind
4. dispatch to dynnode consumer in parent context
5. apply fallback behavior when unconsumed

`apply_kos_op_result` and helper functions should own this pipeline.

### 5.2 Dynnode dispatch API

**Implemented** (`koskari/lib/dynchain/dyn_boundary.c`): one generic
dispatcher drives NEXT / FOREACH / YIELD_FROM consumers from active chain
state, keyed on **`KOS_DYN_CAP_RECEIVES_ITER_BOUNDARY`** only -- no
node-kind checks in the routing layer:

- **`kos_dyn_find_iter_boundary`** -- the single receiver walk (dyn_top
  preferred, else nearest receiver outward from the running frame).  It
  replaced the three per-kind finders and
  `kos_dyn_need_kos_agen_boundary_deliver`.
- **`kos_dyn_iter_boundary_deliver_yield`** / **`_deliver_return`** -- the
  delivery entries; per-kind semantics stay in the dyn helpers behind them.
  `_deliver_return` returns `KOS_ITER_BOUNDARY_DELIVERED` / `_ERROR` /
  `_RAISE` so the eval loop keeps ownership of raise-ingress routing.
- **`kos_dyn_iter_boundary_agen`** / **`_abort`** -- error-path teardown
  used by `clear_kos_agen_boundary_on_error`.

NEXT and flattened-delegate YIELD_FROM nodes carry the capability for
their lifetime; FOREACH toggles it in lockstep with its ITER_NEXT step
phase via `kos_dyn_foreach_set_iter_step_phase`.  A future receiver kind
(e.g. a GEN node carrying a yield-from passthru field) joins by carrying
the capability and adding a delivery case -- the finder and the eval loop
do not change.

`kos_dynnode_on_control_returned` remains the separate WIND/WITH
control-returned hook; it was not folded in.

### 5.3 RETURN/YIELD/RAISE refactor

- `RETURN` opcode:
  - keep value construction
  - stop doing consumer delivery inline
  - re-emit boundary return after crossing

- `YIELD` path:
  - keep local signal and required freeze/walk logic
  - route consumer handling through boundary event dispatcher

- `RAISE` path:
  - normalize into boundary-raise event handling with one ordering contract
  - avoid split responsibilities between opcode and separate exception paths


## 6. Remaining deviations

None for iterator-boundary routing.  The former deviations -- per-kind
finders and direct consumer-semantics calls in `dyn_next.c`,
`dyn_yield_from.c`, and the gen-consumer switch in `eval.c`
(`eval_event_return_at_frame`) -- converged on the §5.2 dispatcher.  The
per-kind delivery helpers survive as its internals.

**Done:** §5.2 generic dispatcher (`dynchain/dyn_boundary.c`) -- the
single capability-keyed finder plus yield/return delivery entries; the
three per-kind finders, `kos_dyn_need_kos_agen_boundary_deliver`, and the
eval-side kind switches were removed.

**Done:** `koskari/lib/opcode/RETURN.c` — pop operands, optional delimited
`k(value)`, then `EVAL_RETURN(val)` only.

**Done:** `koskari/lib/dynchain/dyn_yield_from.c` (inner-*yield*) —
`kos_dyn_yield_from_deliver_inner_yield` no longer applies consumer semantics
inline. It hands the delegate's yielded value to the delegating generator's
frame (`KOS_YF_PHASE_EMIT`) and returns control; the eval loop re-dispatches the
YIELD_FROM opcode, which re-emits the value via `yield_from_emit_yield` as a
fresh YIELD event (walk/freeze the delegating GEN, deliver onward). This gives
the YIELD direction NEXT/FOREACH hand-off parity (§4.4) and satisfies "one
crossing, one delivery" (§7.1) for flattened Koskari delegates. The
inner-*return* helper (row above) still applies consumer semantics inline.

**Done:** `eval.c` exception paths (§5.3) — all PyErr / RAISE ingress
converges on `process_raise` (the unified RAISE ingress): post-opcode
results, prologue/signal-poll pending PyErr, revive-throw
(`eval_phase_entry` `KOS_MODE_EXCEPTION`), and transfer-plan step failures
(`run_transfer_plan_to_completion`, which now returns `KosOpResult` so an
async unwind started during abandonment propagates as `EVAL_AWAIT`). The
dead `KOS_MODE_RAISE` entry branch was removed. Two named non-ingress
carve-outs remain in `run_transfer_plan_to_completion`: the foreign
cc-unwind sentinel re-emit (signal delivery; must not see try_local) and a
pending `KoskariContinuationApply` between drain steps (expected in-flight
state). See the ordering-contract comment on `process_raise` in
`koskari/lib/eval.c`.


## 7. Invariants

1) One crossing, one delivery
- For each boundary outcome, frame crossing happens once and delivery happens
  once, in separate phases.

2) Single ownership path
- Event payloads are consumed in exactly one place per path.

3) No stale-frame targeting
- Consumer selection and stack/pc effects must be based on active frame/chain
  after crossing, not cached pointers.

4) No duplicate exception processing
- `RAISE` must not be processed by both opcode-local and boundary paths for the
  same event instance.


## 8. Migration plan (incremental)

1. ~~Introduce `KosOpResult` boundary kinds and `apply_kos_op_result` routing~~
2. ~~Move RETURN parent-delivery logic out of opcode into `eval_event_return`~~
3. ~~Route NEXT/FOREACH/YIELD_FROM return consumers through generic dispatcher (§5.2)~~
4. ~~Align YIELD boundary routing to same dispatcher surface~~ — both
   directions now route through `dyn_boundary.c`
5. ~~Normalize RAISE routing under one ingress contract (§5.3)~~
6. ~~Remove obsolete ad-hoc delivery paths and compatibility shims~~ — the
   three per-kind finders and the need-check probe are gone


## 9. Testing strategy

Must include:
- frame-crossing stage ordering tests (signal stage vs consume stage)
- NEXT and YIELD_FROM parity tests for flattened Koskari delegates
- RETURN payload behavior tests (no leaked delegate return payload where
  semantics require suppression)
- RAISE propagation tests through dyn boundaries
- no AWAIT plan changes in this phase (existing await tests should remain green)

Use existing suites first:
- `tox -qe py -- tests/lib/opcode/test_yield_from.py`
- native eval tests for boundary behavior in `tests/native/test_eval.c`


## 10. Non-goals

- This document does not change language-level semantics for delegation.
- This document does not require introducing new bytecode opcodes.
- This document does not prescribe one exact function name for the new boundary
  dispatcher; naming can follow codebase conventions.

