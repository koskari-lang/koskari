# Boundary Control Re-emit Model

**Status:** Frame-crossing vs consumer-delivery split is implemented for opcodes
and eval helpers: thin **`RETURN`**, **`eval_event_*`**, and
**`apply_kos_op_result`** own unwind/pop and boundary routing. **Open:** generic
dynnode boundary dispatch (§5.2).

This document describes the boundary model where frame-local outcomes are
signaled first, then delivered to parent consumers after crossing a frame
boundary.

Companion docs:
- [kosevent-elimination.md](../archive/kosevent-elimination.md) (archived; implemented)
- [event-driven-node-delivery.md](../archive/event-driven-node-delivery.md)
- [generic-dynnode-protocol.md](generic-dynnode-protocol.md)
- [evaluator.md](evaluator.md)


## 1. Problem statement

Boundary outcomes combine two responsibilities:

1) Transfer out of a frame (unwind/pop/freeze across frame and GEN boundaries)
2) Consumer delivery (stack push, PC rewrite, boundary node semantics)

**Opcode and eval staging (implemented):** `RETURN.c` pops operands and returns
`EVAL_RETURN(val)`; `eval_event_return` and `eval_parent_return_deliver` own
unwind, frame pop, and parent delivery. `YIELD` returns `EVAL_YIELD(val)`;
`eval_event_yield` owns walk/freeze and boundary delivery.

**Remaining coupling:** dyn helpers still cross frame boundaries and apply
NEXT/FOREACH/YIELD_FROM consumer semantics inline instead of routing through a
single generic boundary dispatcher (§5.2).


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
**`eval_event_return`** runs unwind, frame pop, and delivery. Parent-frame delivery
uses **`eval_parent_return_deliver`** (iterator boundary via
**`kos_dyn_try_deliver_iterator_boundary_after_pop`**, native handler or stack
push, then **`kos_dynnode_on_control_returned`**). Shared iterator return delivery
mapping lives in **`eval_deliver_iterator_boundary_return_step`**. **`YIELD`**
returns **`EVAL_YIELD(val)`**; **`eval_event_yield`** runs
**`eval_yield_handle_walk_and_freeze`** then **`eval_event_yield_deliver_to_boundary`**.
Active **`PyErr`** flows through **`eval_boundary_raise_dispatch`** (wraps
**`process_raise`**).


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

Introduce or repurpose a single entry point that consumes boundary-control
events (instead of ad-hoc direct calls in opcode/dyn helper paths).

Candidate direction:
- evolve `kos_dynnode_on_control_returned(...)` into a real event consumer
  (currently too narrow and mostly WIND/WITH oriented)
- or add a new `kos_dynnode_on_boundary_event(...)`

Either way, one generic dispatcher should drive NEXT / FOREACH / YIELD_FROM
consumers from active chain state.

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

| Location | Issue |
|----------|-------|
| `koskari/lib/dynchain/dyn_next.c` | Return delivery helpers cross GEN/frame boundary and immediately apply NEXT/FOREACH consumer semantics instead of a generic boundary dispatcher |
| `koskari/lib/dynchain/dyn_yield_from.c` | Inner-return helper crosses boundary and immediately performs YIELD_FROM consumer semantics |
| `eval.c` (`eval_parent_return_deliver`) | Calls `kos_dyn_try_deliver_iterator_boundary_after_pop` and `kos_dynnode_on_control_returned` directly; should converge on §5.2 dispatcher |
| `eval.c` (exception paths) | `process_raise`, `eval_phase_entry`, `run_transfer_plan_to_completion`, and `KOS_MODE_RAISE` embed parallel RAISE policy (§5.3) |

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
3. Route NEXT/FOREACH/YIELD_FROM return consumers through generic dispatcher (§5.2)
4. Align YIELD boundary routing to same dispatcher surface — *partially done*:
   `eval_event_yield_deliver_to_boundary` now hands off uniformly for every
   boundary kind (NEXT / FOREACH / YIELD_FROM all notify their receiver and
   `EVAL_REPROLOGUE`), though still via the kind-specific helpers rather than a
   single §5.2 generic dispatcher.
5. Normalize RAISE routing under one ingress contract (§5.3)
6. Remove obsolete ad-hoc delivery paths and compatibility shims


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

