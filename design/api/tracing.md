# Koskari Tracing

Compile-flag enabled tracing for profiling freelist behaviour, event counts,
and timing. The tracing infrastructure is always present;
the Python API always returns a report dict; but the report contents reflect
which tracing features were enabled at build time.

## Overview

Koskari uses several freelists, interned structures, and hot paths. Without
tracing, there is no visibility into allocation patterns, cache hit rates,
underruns (alloc when freelist empty), or overruns (return when freelist at
cap). The tracing module provides:

- **Compile-time flags** to enable specific features (each has a cost)
- **Macros** that expand to noops when disabled (zero runtime cost)
- **Python API** `koskari.lib.get_trace_report()` returning a dict, and
  **`koskari.lib.clear_trace_counters()`** to zero accumulators mid-process

The header `tracing.h` is not included from `lib.h`. Files that use tracing
macros must include it explicitly (same opt-in pattern as `debug.h`).

## Compile Flags

| Flag | Default | Purpose |
|------|---------|---------|
| `KOS_TRACE_FREELIST` | 0 | Freelist alloc/return hit/miss/overrun |
| `KOS_TRACE_EVENTS`   | 0 | VM / dynchain event counters (`events` report key) |
| `KOS_TRACE_TIMING`   | 0 | Per-opcode handler time (`opcode_timing` report key) |

Flags are defaulted in `tracing.h` with `#ifndef`. Override at build time:

- **setup.py**: `KOS_TRACE_FREELIST=1 python setup.py build_ext` (same pattern for `KOS_TRACE_EVENTS=1`, etc.)
- **tests.mk**: `make KOS_TRACE_FREELIST=1`

Counters reset in `kos_trace_init()` / `kos_trace_clear()` (module init and
clear), and optionally via **`koskari.lib.clear_trace_counters()`** after
preload so a benchmark window does not include earlier interpreter work. For a
tracing-enabled build, run code then call `get_trace_report()` in the same
process to read totals.

### Churn demo (`tox -e churn-trace`)

The **`churn-trace`** tox environment rebuilds the native module with
`KOS_TRACE_EVENTS=1` and runs [`examples/churn/churn.py`](../../examples/churn/churn.py).
After the benchmarks, that script prints `get_trace_report()` as JSON when the
extension was built with event tracing. Wall-clock per phase still comes from
the churn script's own output; the report **`events`** values are **cumulative
counts** (not seconds). Use `tox -r -e churn-trace` if you need to force a clean
rebuild after toggling trace flags.

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
lib.clear_trace_counters()  # optional: zero freelist/events/opcode accumulators
```

**`clear_trace_counters()`** takes no arguments, returns `None`, and calls the
same reset as module init and `kos_trace_clear()` (all enabled trace slots).
Use it immediately before a timed region when import-time work would otherwise
pollute `get_trace_report()`.

## Report Structure

### When all tracing is disabled

```python
{
    "flags": {
        "freelist": False,
        "events": False,
        "timing": False
    }
}
```

The `flags` dict always reflects the build-time state of each feature. When
all optional sections are disabled, no `freelists`, `events`,
`opcode_timing`, `opcode_timing_summary`, `post_handler_timing`, or
`post_handler_timing_summary` keys are present (only `flags`).

### When freelist tracing is enabled (`KOS_TRACE_FREELIST=1`)

```python
{
    "flags": {"freelist": True, "events": False, "timing": False},
    "freelists": {
        "dyn_node":     {"alloc_hits": N, "alloc_misses": N, "return_hits": N, "overruns": N},
        "continuation": {...},
        "context":      {...},
        "stack_block":  {...},
        "pair":         {...}
    }
}
```

### When event tracing is enabled (`KOS_TRACE_EVENTS=1`)

The report includes an **`events`** dict mapping string keys to non-negative
integer counts (implemented as `uint64_t` in C, exposed as Python `int`).

| Key | Meaning |
|-----|---------|
| **dyn_handle_enter** | Calls to `kos_dyn_handle` (control transfer offer to a chain node). |
| **dyn_unwind_step** | Steps in `kos_dyn_pop_one_unwind_walk` (unwind/return walks that pop one node). |
| **transfer_plan_build** | Calls to `kos_build_transfer_plan`. |
| **raise_local_walk_iter** | Iterations of the walk in `kos_dyn_try_local_raise_body` (local exception handler search). |

Add new counters in `tracing.h` (`KosTraceEventId`) and `tracing.c` (names
array) only by appending entries; do not reorder existing ids.

C code increments via `KOS_TRACE_EVENTS_INC(KOS_TE_...)` from `tracing.h`.

### When opcode timing is enabled (`KOS_TRACE_TIMING=1`)

The report includes **`opcode_timing`** and **`opcode_timing_summary`**.

#### `opcode_timing`

Keys are opcode names (same strings as `kos_opcode_name`). Values are objects
with:

| Field | Type | Meaning |
|-------|------|---------|
| **count** | `int` | How many times that opcode’s handler ran. |
| **total_ns** | `int` | Sum of elapsed time inside the handler (nanoseconds, `CLOCK_MONOTONIC` on POSIX). |
| **avg_ns** | `int` | `total_ns // count` (integer division; only opcodes with `count > 0` appear). |
| **share_of_time** | `float` | `total_ns / total_ns_all` over all timed opcodes, or `0.0` if there is no time recorded. |
| **share_of_executions** | `float` | `count / total_executions` over all timed opcodes, or `0.0` if there are no executions. |
| **ratio_vs_mean** | `float` | `avg_ns / avg_ns_per_dispatch`, where `avg_ns_per_dispatch` is `total_ns_all // total_executions` (see summary); `0.0` if `total_executions` is zero or the mean dispatch time is zero. |

Timing is recorded around each `op_*` call in both eval dispatch loops
(computed goto and switch); it does not include `KOS_EVAL_PREPARE_NEXT_INSN` or
`eval_phase_post_opcode` work.

C code uses `KOS_TRACE_OP_WRAP(name, op_out = op_##name(...))` in `eval.c`
(via `tracing.h`).

#### `opcode_timing_summary`

Aggregates and ranked lists (top **10** entries per list, constant
`KOS_TRACE_OPCODE_TOP_N` in `tracing.c`):

| Field | Type | Meaning |
|-------|------|---------|
| **total_executions** | `int` | Sum of all per-opcode `count` values. |
| **total_ns** | `int` | Sum of all per-opcode `total_ns` values (same as `total_ns_all` used in shares). |
| **avg_ns_per_dispatch** | `int` | `total_ns // total_executions`, or `0` if `total_executions` is `0`. |
| **opcode_count_nonzero** | `int` | Number of opcode indices with at least one execution. |
| **max_total_ns_op** | `str` | Name of the opcode with the largest `total_ns` (empty string if none). |
| **max_total_ns** | `int` | That largest `total_ns` value (`0` if none). |
| **max_avg_ns_op** | `str` | Name of the opcode with the largest `avg_ns` among ops with `count > 0` (empty string if none). |
| **max_avg_ns** | `int` | That largest `avg_ns` (`0` if none). |
| **top_by_total_ns** | `list` | Up to 10 `{"op": str, "total_ns": int}` objects, sorted by `total_ns` descending. |
| **top_by_avg_ns** | `list` | Up to 10 `{"op": str, "avg_ns": int}` objects, sorted by `avg_ns` descending. |

### Post-opcode handler timing (same flag: `KOS_TRACE_TIMING=1`)

When opcode timing is enabled, the report also includes **`post_handler_timing`**
and **`post_handler_timing_summary`**. These record wall time for
`eval_phase_post_opcode` and named sub-handlers (nanoseconds, same clock as
opcode timing).

**Rollup (`rollup` row):** Wall time for each `eval_phase_post_opcode`
invocation (`KOS_TRACE_POST_OP_ENTER` / `EXIT`). There is no nested C
re-entrancy; a deferred post-op pass (native parent-return path) is queued as
`KOS_EV_POST_OPCODE` on `out_ev` and runs as a **second** `eval_phase_post_opcode`
call on the next dispatch prologue, so one
logical chain can produce two rollup samples.

**Per-handler rows** use the same per-entry fields as `opcode_timing` (`count`,
`total_ns`, `avg_ns`, `share_of_time`, `share_of_executions`, `ratio_vs_mean`).
Shares are computed over all non-zero post-handler buckets including `rollup`.
Handler times can overlap logically (for example `parent_return` vs successive
`rollup` samples); sums of handler rows are not expected to match a single
rollup interval.

| Key | Meaning |
|-----|---------|
| **rollup** | Wall time inside `eval_phase_post_opcode` (ENTER/EXIT). |
| **parent_return** | `eval_parent_return_deliver` (impl body), wrapped once at the wrapper. |
| **control_returned** | Each `kos_dynnode_on_control_returned` call in `eval_phase_post_opcode` and in `eval_parent_return_deliver_impl`. |
| **event_raise** | Post-opcode tail path only (`eval_event_raise` from `eval_phase_post_opcode_tail`). Does not include the loop prologue `eval_phase_pending_exception` path. |
| **event_await** | `KOS_EV_AWAIT` branch in `eval_phase_post_opcode_tail`. |
| **event_yield** | `eval_event_yield` from the tail. |
| **return_boundary** | `eval_event_return_deliver_to_boundary` from the tail. |

#### `post_handler_timing_summary`

| Field | Type | Meaning |
|-------|------|---------|
| **total_executions** | `int` | Sum of per-handler `count` values (includes `rollup` and named handlers). |
| **total_ns** | `int` | Sum of all per-handler `total_ns` values. |
| **avg_ns_per_dispatch** | `int` | `total_ns // total_executions`, or `0` if none. |
| **handler_count_nonzero** | `int` | Number of handler keys with at least one sample. |
| **max_total_ns_handler** / **max_total_ns** | `str` / `int` | Largest `total_ns` bucket (empty string / `0` if none). |
| **max_avg_ns_handler** / **max_avg_ns** | `str` / `int` | Largest `avg_ns` among buckets with `count > 0`. |
| **rollup_invocations** | `int` | `count` for the `rollup` bucket (`eval_phase_post_opcode` completions). |
| **rollup_total_ns** | `int` | `total_ns` for `rollup`. |
| **rollup_avg_ns** | `int` | Average duration per rollup invocation. |
| **top_by_total_ns** | `list` | Up to 10 `{"handler": str, "total_ns": int}`, sorted by `total_ns` descending. |
| **top_by_avg_ns** | `list` | Up to 10 `{"handler": str, "avg_ns": int}`, sorted by `avg_ns` descending. |

C macros in `tracing.h`: `KOS_TRACE_POST_OP_ENTER` / `KOS_TRACE_POST_OP_EXIT`
(rollup around `eval_phase_post_opcode`), and `KOS_TRACE_POST_OP_WRAP` for
named handlers. All expand to no-ops when `KOS_TRACE_TIMING` is 0.

## Freelist Counter Meanings

Each freelist entry has four counters:

| Counter | Meaning |
|---------|---------|
| **alloc_hits** | Allocations satisfied from the freelist (cache hit). The freelist was non-empty and an object was reused. |
| **alloc_misses** | Allocations that required `PyMem_Malloc` or `PyObject_GC_New` because the freelist was empty (underrun). |
| **return_hits** | Returns where the object was cached on the freelist. The freelist was below its cap; the object will be reused. |
| **overruns** | Returns where the freelist was at its cap; the object was freed instead of cached. The freelist discarded a reusable object. |

### Interpretation

- **alloc_hits / (alloc_hits + alloc_misses)** — Cache hit rate on allocation. Higher is better; fewer malloc calls.
- **return_hits** — How many objects were successfully cached. Should roughly match total allocations minus those still in use.
- **overruns** — Indicates the freelist cap may be too low for the workload, or a burst of returns exceeded the cap. Non-zero overruns mean some objects that could have been reused were freed instead.

### Per-freelist descriptions

| Name | Object type | Used for |
|------|-------------|----------|
| **dyn_node** | `KosDynNode` | Dynamic chain nodes (frames, prompts, regions, with-blocks, etc.). Max 128 cached. |
| **continuation** | `KosContinuation` | Delimited and undelimited continuation structures. Max 64 cached. |
| **context** | `KosContext` | Evaluator context (one per coroutine). Max 16 cached. |
| **stack_block** | `KosStackBlock` | Operand stack blocks. Max 256 cached. |
| **pair** | `KosPair` | Cons cells. Max 256 cached. |

## Instrumentation Points

**Freelist** — files that include `tracing.h` and use `KOS_TRACE_FL_ALLOC` / `KOS_TRACE_FL_RETURN`:

- **dynchain.c** — `kos_dynnode_alloc`, `kos_dynnode_return_to_freelist`; `kos_continuation_create`, `kos_continuation_free_exclude`, `xfer_plan_release_invoke_clone`
- **eval.c** — `kos_context_new`, `kos_context_free`, and context release on allocation failure
- **stack.c** — `KosStackBlock_New`, `KosStackBlock_NewSized`, `KosStackBlock_ReleaseExclude`
- **pair.c** — `KosPair_New`, `pair_dealloc`

**Events** — `KOS_TRACE_EVENTS_INC` in **dynchain.c** at `kos_dyn_handle`,
`kos_dyn_pop_one_unwind_walk`, `kos_build_transfer_plan`, and
`kos_dyn_try_local_raise_body` (see table above).

**Opcode timing** — `KOS_TRACE_OP_WRAP` in **eval.c** dispatch
(`koskari_eval_dispatch_computed_goto` and `koskari_eval_dispatch_switch`).

**Post-opcode handler timing** — `KOS_TRACE_POST_OP_ENTER` / `EXIT` and
`KOS_TRACE_POST_OP_WRAP` in **eval.c** (`eval_phase_post_opcode`,
`eval_parent_return_deliver`, `eval_phase_post_opcode_tail`).

## Future Phases

- **KOS_TRACE_EVENTS**: Additional counters as needed
