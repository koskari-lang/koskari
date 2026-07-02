# Koskari Tracing

Compile-flag enabled tracing for profiling freelist behaviour, VM event
counts, eval-state sync cost, and opcode timing. The tracing infrastructure is
always present; the Python API always returns a report dict (JSON-serializable
when dumped). Report contents reflect which tracing features were enabled at
build time.

Use this document both to **enable tracing in builds** and as a **field
reference** for the dict returned by `koskari.lib.get_trace_report()` (built in
`koskari/lib/tracing.c` as `kos_trace_report()`).

## Overview

Koskari uses several freelists, interned structures, and hot paths. Without
tracing, there is no visibility into allocation patterns, cache hit rates,
underruns (alloc when freelist empty), or overruns (return when freelist at
cap). The tracing module provides:

- **Four compile-time flags** to enable specific features (each has a cost)
- **Macros** that expand to noops when disabled (zero runtime cost)
- **Python API** `koskari.lib.get_trace_report()` returning a dict, and
  **`koskari.lib.clear_trace_counters()`** to zero accumulators mid-process

The header `tracing.h` is not included from `lib.h`. Files that use tracing
macros must include it explicitly (same opt-in pattern as `debug.h`).

## Compile Flags

| Flag | Default | Purpose |
|------|---------|---------|
| `KOS_TRACE_FREELIST` | 0 | Freelist alloc/return hit/miss/overrun (`freelists` key) |
| `KOS_TRACE_EVENTS` | 0 | VM / dynchain event counters (`events` key) |
| `KOS_TRACE_TIMING` | 0 | Per-opcode and post-handler timing (`opcode_timing`, `post_handler_timing`, and summaries) |
| `KOS_TRACE_EVAL_SYNC` | 0 | `KosEvalState` sync API call counts and wall time (`eval_state_sync` key) |

Flags are defaulted in `tracing.h` with `#ifndef`. Override at build time:

- **setup.py**: `KOS_TRACE_FREELIST=1 python setup.py build_ext` (same pattern for other flags)
- **tests.mk**: `make KOS_TRACE_FREELIST=1`

Counters reset in `kos_trace_init()` / `kos_trace_clear()` (module init and
clear), and optionally via **`koskari.lib.clear_trace_counters()`** after
preload so a benchmark window does not include earlier interpreter work. For a
tracing-enabled build, run code then call `get_trace_report()` in the same
process to read totals.

### Churn demo (`tox -e churn-trace`)

The **`churn-trace`** tox environment rebuilds the native module with all four
trace flags enabled (`KOS_TRACE_FREELIST`, `KOS_TRACE_EVENTS`,
`KOS_TRACE_TIMING`, `KOS_TRACE_EVAL_SYNC`) and runs
[`examples/churn/churn.py`](../../examples/churn/churn.py). After the
benchmarks, that script prints `get_trace_report()` as JSON. Wall-clock per
phase still comes from the churn script's own output; report values are
**cumulative since process start** (or since the last `clear_trace_counters()`).
Use `tox -r -e churn-trace` if you need to force a clean rebuild after toggling
trace flags.

## API

### C API (always available)

```c
void kos_trace_init(void);       /* Reset counters; called at module init */
void kos_trace_clear(void);      /* Reset counters; called at module clear */
PyObject *kos_trace_report(void);/* Returns new ref to dict */
```

### Python API

```python
import koskari.lib as lib
report = lib.get_trace_report()
lib.clear_trace_counters()  # optional: zero all enabled trace accumulators
```

**`clear_trace_counters()`** takes no arguments, returns `None`, and calls the
same reset as module init and `kos_trace_clear()` (freelist, events, timing,
post-handler timing, and eval_state_sync slots when enabled). Use it
immediately before a timed region when import-time work would otherwise pollute
`get_trace_report()`.

## Reading a trace report

1. **Check `flags` first.** It records which features were compiled in. Top-level
   sections absent from the dict were not enabled at build time (not merely
   empty).
2. **Counters are cumulative** since the last `kos_trace_init()`,
   `kos_trace_clear()`, module init, or `clear_trace_counters()`. They are not
   wall-clock benchmark results by themselves.
3. **`events`** values are **invocation counts** (`uint64_t` in C), not seconds.
4. **Timing sections** use nanoseconds from `CLOCK_MONOTONIC` on POSIX. Opcode
   timing wraps only the `op_*` handler body in the dispatch loop; it excludes
   `KOS_EVAL_PREPARE_NEXT_INSN` and `eval_phase_post_opcode`. Post-handler rows
   can overlap logically; sums of named handler rows need not equal one `rollup`
   interval.
5. **`opcode_timing` and `post_handler_timing`** list only buckets with
   `count > 0`.

## Report envelope

Top-level keys emitted by `kos_trace_report()`:

| Key | When present | Type |
|-----|----------------|------|
| `flags` | always | object |
| `freelists` | `flags.freelist` is true | object |
| `events` | `flags.events` is true | object |
| `eval_state_sync` | `flags.eval_state_sync` is true | object |
| `opcode_timing` | `flags.timing` is true | object |
| `opcode_timing_summary` | `flags.timing` is true | object |
| `post_handler_timing` | `flags.timing` is true | object |
| `post_handler_timing_summary` | `flags.timing` is true | object |

### `flags`

Always present. Four booleans mirror compile-time flags (see `kos_trace_report`
in `tracing.c`):

| Field | Meaning |
|-------|---------|
| **freelist** | `KOS_TRACE_FREELIST` was 1 at build time |
| **events** | `KOS_TRACE_EVENTS` was 1 at build time |
| **timing** | `KOS_TRACE_TIMING` was 1 at build time |
| **eval_state_sync** | `KOS_TRACE_EVAL_SYNC` was 1 at build time |

### Minimal examples

When all tracing is disabled at build time:

```python
{
    "flags": {
        "freelist": False,
        "events": False,
        "timing": False,
        "eval_state_sync": False,
    },
}
```

When all four flags are enabled (outline only; numeric values are workload-specific):

```python
{
    "flags": {
        "freelist": True,
        "events": True,
        "timing": True,
        "eval_state_sync": True,
    },
    "freelists": { "...": {"alloc_hits": 0, "alloc_misses": 0, "return_hits": 0, "overruns": 0} },
    "events": { "dyn_handle_enter": 0, "...": 0 },
    "eval_state_sync": { "init": {"count": 0, "total_ns": 0}, "...": {"count": 0, "total_ns": 0} },
    "opcode_timing": { "POP": {"count": 0, "total_ns": 0, "...": 0.0} },
    "opcode_timing_summary": { "total_executions": 0, "total_ns": 0, "...": [] },
    "post_handler_timing": { "rollup": {"count": 0, "...": 0.0} },
    "post_handler_timing_summary": { "rollup_invocations": 0, "...": [] },
}
```

When all optional sections are disabled, only `flags` appears.

## Shared field shapes

### Freelist entry

Used under `freelists` for each of the five fixed pool names. See
[Freelist counter meanings](#freelist-counter-meanings) for interpretation.

| Field | Type | Meaning |
|-------|------|---------|
| **alloc_hits** | `int` | Allocation satisfied from freelist (reuse) |
| **alloc_misses** | `int` | Allocation required malloc/GC new (freelist empty) |
| **return_hits** | `int` | Return cached on freelist (below cap) |
| **overruns** | `int` | Return dropped because freelist was at cap |

### Timing entry

Used for each opcode in `opcode_timing` and each handler in
`post_handler_timing`. Shares and ratio use totals over **all non-zero buckets**
in that same section (including `rollup` for post-handler timing).

| Field | Type | Meaning |
|-------|------|---------|
| **count** | `int` | Number of timed invocations |
| **total_ns** | `int` | Sum of elapsed nanoseconds |
| **avg_ns** | `int` | `total_ns // count` |
| **share_of_time** | `float` | `total_ns / total_ns_all` for the section, or `0.0` if no time |
| **share_of_executions** | `float` | `count / total_executions` for the section, or `0.0` if no executions |
| **ratio_vs_mean** | `float` | `avg_ns / avg_ns_per_dispatch` for the section summary, or `0.0` if mean is zero |

### Timing summary (common pattern)

Opcode and post-handler summaries share the same aggregate style; post-handler
adds rollup-specific fields (listed in its subsection).

| Field | Type | Meaning |
|-------|------|---------|
| **total_executions** | `int` | Sum of per-bucket `count` values in the section |
| **total_ns** | `int` | Sum of per-bucket `total_ns` (denominator for `share_of_time`) |
| **avg_ns_per_dispatch** | `int` | `total_ns // total_executions`, or `0` if `total_executions` is `0` |
| **top_by_total_ns** | `list` | Up to 10 rows, sorted by `total_ns` descending |
| **top_by_avg_ns** | `list` | Up to 10 rows, sorted by `avg_ns` descending |

Top-N limit is `KOS_TRACE_OPCODE_TOP_N` / `KOS_TRACE_POST_HANDLER_TOP_N` (10) in
`tracing.c`.

## Per-section reference

### `freelists`

Present when `flags.freelist` is true. Fixed keys (always all five):

| Key | Object type | Used for |
|-----|-------------|----------|
| **dyn_node** | `KosDynNode` | Dynamic chain nodes. Max 128 cached. |
| **continuation** | `KosContinuation` | Continuation structures. Max 64 cached. |
| **context** | `KosContext` | Evaluator context (one per coroutine). Max 16 cached. |
| **stack_block** | `KosStackBlock` | Operand stack blocks. Max 256 cached. |
| **pair** | `KosPair` | Cons cells. Max 256 cached. |

Each value is a [freelist entry](#freelist-entry).

Example with only freelist tracing enabled:

```python
{
    "flags": {
        "freelist": True,
        "events": False,
        "timing": False,
        "eval_state_sync": False,
    },
    "freelists": {
        "dyn_node": {"alloc_hits": N, "alloc_misses": N, "return_hits": N, "overruns": N},
        "continuation": {"alloc_hits": N, "alloc_misses": N, "return_hits": N, "overruns": N},
        "context": {"alloc_hits": N, "alloc_misses": N, "return_hits": N, "overruns": N},
        "stack_block": {"alloc_hits": N, "alloc_misses": N, "return_hits": N, "overruns": N},
        "pair": {"alloc_hits": N, "alloc_misses": N, "return_hits": N, "overruns": N},
    },
}
```

### `events`

Present when `flags.events` is true. Maps string keys to non-negative integer
counts (`uint64_t` in C).

| Key | Meaning |
|-----|---------|
| **dyn_handle_enter** | Calls to `kos_dyn_handle` (control transfer offer to a chain node). |
| **dyn_unwind_step** | Steps in `kos_dyn_pop_one_unwind_walk` (unwind/return walks that pop one node). |
| **transfer_plan_build** | Calls to `kos_build_transfer_plan`. |
| **raise_local_walk_iter** | Iterations of the walk in `kos_dyn_try_local_raise_body` (local exception handler search). |

Add new counters in `tracing.h` (`KosTraceEventId`) and `tracing.c` (`kos_te_names`)
only by **appending** entries; do not reorder existing ids.

C code increments via `KOS_TRACE_EVENTS_INC(KOS_TE_...)` in **dynchain.c**.

### `eval_state_sync`

Present when `flags.eval_state_sync` is true. Maps fixed API names to objects
with only **`count`** and **`total_ns`** (no share fields). Wall time is
`CLOCK_MONOTONIC` nanoseconds per call, recorded with `KOS_TRACE_EVAL_SYNC_BEGIN`
/ `KOS_TRACE_EVAL_SYNC_END` in `tracing.h`.

| Key | Instrumented API / site |
|-----|-------------------------|
| **init** | `KosEvalState_Init` (`eval/eval_state.c`) |
| **unused_sync_from_context** | Reserved slot (removed API; never incremented) |
| **flush_live_regs** | `KosEvalState_FlushLiveRegsToFrame` (inline in `eval.h`) |
| **unused_apply_dyn_caches** | Reserved slot (removed API; never incremented) |
| **rebuild_live_from_chain** | `kos_dyn_rebuild_live_from_chain` (`dynchain.c`) |
| **retarget_after_push** | `kos_dyn_retarget_after_push` (`dynchain.c`) |

Add new ids in `tracing.h` (`KosTraceEvalSyncId`) and `tracing.c` (`kos_tevs_names`)
only by **appending** entries. Renaming existing slot strings in place is allowed
when enum indices stay fixed.

Example entry:

```python
"rebuild_live_from_chain": {"count": 128643000, "total_ns": 3472847736}
```

### `opcode_timing`

Present when `flags.timing` is true. Keys are opcode name strings (same as
`kos_opcode_name`). Values are [timing entries](#timing-entry). Only opcodes
with at least one execution appear.

Timing is recorded around each `op_*` call in both eval dispatch loops
(computed goto and switch) via `KOS_TRACE_OP_WRAP` in **eval.c**.

### `opcode_timing_summary`

Present when `flags.timing` is true. Aggregates over all timed opcodes.

| Field | Type | Meaning |
|-------|------|---------|
| **total_executions** | `int` | Sum of all per-opcode `count` values |
| **total_ns** | `int` | Sum of all per-opcode `total_ns` |
| **avg_ns_per_dispatch** | `int` | `total_ns // total_executions`, or `0` |
| **opcode_count_nonzero** | `int` | Number of opcode indices with `count > 0` |
| **max_total_ns_op** | `str` | Opcode with largest `total_ns` (empty if none) |
| **max_total_ns** | `int` | That `total_ns` (`0` if none) |
| **max_avg_ns_op** | `str` | Opcode with largest `avg_ns` among executed ops |
| **max_avg_ns** | `int` | That `avg_ns` (`0` if none) |
| **top_by_total_ns** | `list` | Up to 10 `{"op": str, "total_ns": int}` |
| **top_by_avg_ns** | `list` | Up to 10 `{"op": str, "avg_ns": int}` |

### `post_handler_timing`

Present when `flags.timing` is true. Same [timing entry](#timing-entry) shape
as opcodes. Fixed handler keys from `kos_tp_names` in `tracing.c`:

| Key | Meaning |
|-----|---------|
| **rollup** | Wall time inside `eval_phase_post_opcode` (`KOS_TRACE_POST_OP_ENTER` / `EXIT`). A deferred post-op pass can produce a second rollup sample on the next dispatch prologue. |
| **parent_return** | `eval_parent_return_deliver` (impl body), wrapped at the wrapper. |
| **control_returned** | Each `kos_dynnode_on_control_returned` in `eval_phase_post_opcode` and `eval_parent_return_deliver_impl`. |
| **event_raise** | `process_raise` from `eval_phase_post_opcode_tail` only (not loop prologue pending-exception path). |
| **event_await** | `KOS_EV_AWAIT` branch in `eval_phase_post_opcode_tail`. |
| **event_yield** | `eval_event_yield` from the tail. |
| **return_boundary** | `eval_event_return_deliver_to_boundary` from the tail. |

Shares are over all non-zero post-handler buckets including `rollup`. Handler
times can overlap; row sums are not expected to match one rollup interval.

Macros: `KOS_TRACE_POST_OP_ENTER` / `EXIT`, `KOS_TRACE_POST_OP_WRAP` in
**eval.c** (`eval_phase_post_opcode`, `eval_parent_return_deliver`,
`eval_phase_post_opcode_tail`).

### `post_handler_timing_summary`

Present when `flags.timing` is true.

| Field | Type | Meaning |
|-------|------|---------|
| **total_executions** | `int` | Sum of per-handler `count` (includes `rollup`) |
| **total_ns** | `int` | Sum of all per-handler `total_ns` |
| **avg_ns_per_dispatch** | `int` | `total_ns // total_executions`, or `0` |
| **handler_count_nonzero** | `int` | Handler keys with at least one sample |
| **max_total_ns_handler** | `str` | Handler with largest `total_ns` |
| **max_total_ns** | `int` | That `total_ns` |
| **max_avg_ns_handler** | `str` | Handler with largest `avg_ns` among sampled handlers |
| **max_avg_ns** | `int` | That `avg_ns` |
| **rollup_invocations** | `int` | `count` for the `rollup` bucket |
| **rollup_total_ns** | `int` | `total_ns` for `rollup` |
| **rollup_avg_ns** | `int` | `rollup_total_ns // rollup_invocations`, or `0` |
| **top_by_total_ns** | `list` | Up to 10 `{"handler": str, "total_ns": int}` |
| **top_by_avg_ns** | `list` | Up to 10 `{"handler": str, "avg_ns": int}` |

## Freelist counter meanings

Each freelist entry has four counters:

| Counter | Meaning |
|---------|---------|
| **alloc_hits** | Allocations satisfied from the freelist (cache hit). The freelist was non-empty and an object was reused. |
| **alloc_misses** | Allocations that required `PyMem_Malloc` or `PyObject_GC_New` because the freelist was empty (underrun). |
| **return_hits** | Returns where the object was cached on the freelist. The freelist was below its cap; the object will be reused. |
| **overruns** | Returns where the freelist was at its cap; the object was freed instead of cached. |

### Interpretation

- **alloc_hits / (alloc_hits + alloc_misses)** — Cache hit rate on allocation. Higher is better; fewer malloc calls.
- **return_hits** — How many objects were successfully cached. Should roughly match total allocations minus those still in use.
- **overruns** — Indicates the freelist cap may be too low for the workload, or a burst of returns exceeded the cap. Non-zero overruns mean some objects that could have been reused were freed instead.

## Instrumentation points

**Freelist** — `KOS_TRACE_FL_ALLOC` / `KOS_TRACE_FL_RETURN` in:

- **dynchain.c** — `kos_dynnode_alloc`, `kos_dynnode_return_to_freelist`; continuation alloc/free paths
- **eval.c** — `KosContext_New`, `KosContext_Free`, context release on allocation failure
- **stack.c** — `KosStackBlock_New`, `KosStackBlock_NewSized`, release paths
- **pair.c** — `KosPair_New`, `pair_dealloc`

**Events** — `KOS_TRACE_EVENTS_INC` in **dynchain.c** (see `events` table).

**Eval state sync** — `KOS_TRACE_EVAL_SYNC_BEGIN` / `END` in **eval/eval_state.c**
and `KosEvalState_FlushLiveRegsToFrame` in **eval.h**.

**Opcode timing** — `KOS_TRACE_OP_WRAP` in **eval.c** dispatch
(`koskari_eval_dispatch_computed_goto` and `koskari_eval_dispatch_switch`).

**Post-opcode handler timing** — `KOS_TRACE_POST_OP_ENTER` / `EXIT` and
`KOS_TRACE_POST_OP_WRAP` in **eval.c**.

## Maintenance

When adding trace counters in `tracing.h`, append new enum values only; do not
reorder existing ids. Update the matching names array in `tracing.c` and the
corresponding section in this document.

## Agent prompt blurb

Copy when asking a tool to analyze attached trace JSON:

```
Analyze the attached Koskari trace report JSON using design/api/tracing.md as
the field glossary. Read flags first to see which sections apply. Treat values
as cumulative since the last clear_trace_counters() unless stated otherwise.
events are counts not seconds. For timing sections use share_of_time and
ratio_vs_mean relative to the section summary; do not sum post_handler_timing
rows expecting them to equal rollup.
```

## Future phases

- Additional `KOS_TRACE_EVENTS` counters as needed
- Further `eval_state_sync` APIs if new hot sync paths are added (append-only ids)
