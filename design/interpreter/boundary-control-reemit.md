# Boundary Control Re-emit Model

This document proposes an event-driven boundary model where frame-local
outcomes are emitted first, then re-emitted as boundary control events after
crossing a frame boundary.

Companion docs:
- [event-driven-eval-event-handlers.md](event-driven-eval-event-handlers.md)
- [event-driven-node-delivery.md](../archive/event-driven-node-delivery.md)
- [generic-dynnode-protocol.md](generic-dynnode-protocol.md)
- [evaluator.md](evaluator.md)


## 1. Problem statement

Current code still contains paths that combine two responsibilities in one
flow:

1) Transfer out of a frame (unwind/pop/freeze across frame and GEN boundaries)
2) Immediate consumer delivery (stack push, PC rewrite, boundary node semantics)

This coupling appears in:
- `koskari/lib/opcode/RETURN.c` (callee unwind/pop and parent delivery)
- `koskari/lib/dynchain/dyn_next.c` return delivery helpers
- `koskari/lib/dynchain/dyn_yield_from.c` inner-return delivery

This violates the intended event-driven split where producer and consumer
stages are separate.


## 2. Core model

Boundary outcomes are handled in two discrete stages.

### Stage A: signal out in the current frame

The opcode and local transition logic emit a local event:
- `RETURN`
- `YIELD`
- `RAISE`
- optional `AWAIT` (if resume-from-await is unified into this channel)

Local unwind/extent logic runs in the current frame context only.

### Stage B: handle re-emitted boundary event in parent context

After frame boundary crossing:
- parent frame becomes active
- evaluator re-emits a boundary-control event
- dynnode dispatch handles it using current active chain state
- default fallback applies if no dynnode consumes

This ensures "send signal out" and "handle signalled result" are distinct
operations.


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

`eval_phase_post_opcode` and helper functions should own this pipeline.

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


## 6. Current deviations to remove

These are concrete places to migrate:

- `koskari/lib/opcode/RETURN.c`
  - currently does unwind/pop + immediate parent/boundary delivery

- `koskari/lib/dynchain/dyn_next.c`
  - return delivery helpers cross GEN/frame boundary and immediately apply
    NEXT/FOREACH consumer semantics

- `koskari/lib/dynchain/dyn_yield_from.c`
  - inner-return helper crosses boundary and immediately performs YIELD_FROM
    consumer semantics


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

1. Introduce boundary-control event kinds and dispatch helper contracts
2. Move RETURN parent-delivery logic out of opcode and into boundary dispatcher
3. Route NEXT/FOREACH/YIELD_FROM return consumers through new dispatcher
4. Align YIELD boundary routing to same dispatcher surface
5. Normalize RAISE routing under same staged model
6. Remove obsolete ad-hoc paths and compatibility shims


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

