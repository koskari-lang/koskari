# KosEvent elimination (archived)

**Status:** Implemented (branch `eval_event_again`, 2026-06). This document
consolidates and archives the design work formerly spread across
`kosevent-elimination-merged.md`, `kosevent-elimination-simplification-response.md`,
and `event-driven-eval-event-handlers.md`.

**Live follow-on:** [boundary-control-reemit.md](boundary-control-reemit.md)
(generic boundary dynnode dispatch, RAISE staging). Open work is tracked in
[TODO.md](../TODO.md).

---

## Goal

Delete `KosEvent` / `KosEventKind` / `KOS_EV_*` entirely. Replace with
`KosOpResult` returned by value throughout — no replacement host struct, no
`out_ev` pointer threading. Every function that can produce a boundary signal
returns `KosOpResult`; callers relay or act directly.

Paired goal: one eval-loop story — **`apply_kos_op_result`** routes every
non-`NORMAL` result; **`NORMAL`** is the hot-path tail dispatch.

---

## What was deleted

| Item | Location |
|------|----------|
| `KosEvent` struct, `KosEventKind` enum, `KOS_EV_*` constants | `eval.h` |
| `KosEvent_KIND`, `KosEvent_PAYLOAD`, `KosEvent_EXC_TYPE` macros | `eval.h` |
| `struct KosEvent` forward declarations | `lib.h`, `callable.h` |
| `kos_eval_emit_raise_from_pyerr` | `eval.c`, `eval.h` |
| `kos_op_result_from_int_step`, `kos_op_result_from_call_int` | `eval_utils.c` |
| `KosEvalResult` / `KosEvalResultKind` (unused) | `eval.h` |
| `eval_dispatch_op_result`, `KOS_EVAL_DISPATCH_STEP` | `eval.c`, `eval.h` |
| `KosContext::unwind_target_frame` / `SetUnwindTargetFrame` | `eval.h`, `POP_WIND.c` |

`KOS_DYN_UNWIND_ASYNC_STARTED` remains an internal walk return code in
`dynchain/`; callers translate it to `EVAL_AWAIT` or `EVAL_REPROLOGUE` as
appropriate.

---

## Architectural outcomes

### `KosOpResult` end-to-end

`KosOpResult { KosOpResultKind kind; PyObject *val }` with `KOS_RES_*` kinds
(`NORMAL`, `REPROLOGUE`, `RAISE`, `YIELD`, `AWAIT`, `RETURN`, `PARENT_RETURN`,
`CONTROL_RETURNED`, `FATAL`). Opcodes return `EVAL_*()` macros; drivers and
nested eval clients switch on `kind` directly.

### `apply_kos_op_result`

Single static router in `eval.c`. Non-`NORMAL` kinds dispatch to:

| Kind | Handler |
|------|---------|
| `KOS_RES_RAISE` | `eval_event_raise` → `process_raise` |
| `KOS_RES_YIELD` | `eval_event_yield` |
| `KOS_RES_AWAIT` | `eval_event_await` |
| `KOS_RES_RETURN` | `eval_event_return` (unwind, pop, parent delivery) |
| `KOS_RES_PARENT_RETURN` | `eval_parent_return_deliver` |
| `KOS_RES_CONTROL_RETURNED` | `kos_dynnode_on_control_returned` hook |

### Thin `RETURN`

`RETURN.c` pops operands, handles delimited `k(value)` via
`KosEvalState_KReturnDelimited`, otherwise returns `EVAL_RETURN(val)`.
Unwind, frame pop, iterator-boundary delivery, native-handler invoke, and root
termination live in `eval_event_return` / `eval_parent_return_deliver`.

`eval_event_return` skips `KOS_TRANS_WALK_RETURN_TO_FRAME` when
`dyn_top == frame` (no nested extents between callee and parent).

### RAISE invariant

For every `KOS_RES_RAISE` at any call boundary: **`PyErr` is set on the
thread.** No `exc_type` struct field. `kos_eval_prepare_boundary_raise` flushes
live registers, injects traceback, marks context dead.

### Driver API

`koskari_eval` returns `KosOpResult` (no `KosEvent *out_ev`). Hosts (`coro.c`,
`utils.c`, `YIELD_FROM.c`, native tests) switch on `KOS_RES_*` and propagate
`PyErr` from `KOS_RES_RAISE`.

### Dynchain and callables

Wind/unwind/pop, transfer-plan steps, `KosInstallFunc`, `KosCallable_Call*`,
`push_or_await_common`, and `revive_await` all return `KosOpResult`.

---

## Verification

```bash
# No KosEvent in library C sources
grep -rn 'KosEvent\|KOS_EV_\b' koskari/lib/ --include='*.c' --include='*.h'
# must return nothing

# RETURN.c does not run frame walks
grep -n 'KOS_TRANS_WALK_RETURN_TO_FRAME' koskari/lib/opcode/RETURN.c
# must return nothing

tox -qe test
```

---

## Source documents (merged into this archive)

| Former path | Role |
|-------------|------|
| `archive/kosevent-elimination-merged.md` | Normative step-by-step implementation plan (S1–S8) |
| `archive/kosevent-elimination-simplification-response.md` | Simplification feedback merged into the plan |
| `archive/event-driven-eval-event-handlers.md` | Thin RETURN, `eval_event_*` routing spec |
| `archive/kosevent-elimination.md` (original) | Initial elimination proposal |

Related but separate: [event-driven-node-delivery.md](event-driven-node-delivery.md)
(boundary ideology for NEXT/FOREACH), [eval-result-unification.md](eval-result-unification.md)
(`KosOpOutcome` → `KosOpResult` precursor).

---

## Remaining evaluator work (not part of this archive)

These items were identified during elimination but are **follow-on** architecture:

1. **Generic boundary dynnode dispatch** — evolve `kos_dynnode_on_control_returned`
   or add `kos_dynnode_on_boundary_event` so NEXT / FOREACH / YIELD_FROM
   consumers route through one capacity-driven dispatcher instead of scattered
   calls in `dyn_next.c`, `dyn_yield_from.c`, and `eval_parent_return_deliver`.
   See [boundary-control-reemit.md](boundary-control-reemit.md) §5.2.

2. **RAISE ingress unification** — `process_raise` is central, but
   `eval_phase_entry` (`KOS_MODE_EXCEPTION`), `run_transfer_plan_to_completion`,
   and `KOS_MODE_RAISE` still embed parallel exception policy.

3. **Optional Track C** — `KosEvalStep` / phase-helper cleanup inside `eval.c`
   (low priority; helpers carry no value).
