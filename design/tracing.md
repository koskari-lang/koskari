# Koskari Tracing

Compile-flag enabled tracing for profiling freelist behaviour, interned atom
usage, event counts, and timing. The tracing infrastructure is always present;
the Python API always returns a report dict; but the report contents reflect
which tracing features were enabled at build time.

## Overview

Koskari uses several freelists, interned structures, and hot paths. Without
tracing, there is no visibility into allocation patterns, cache hit rates,
underruns (alloc when freelist empty), or overruns (return when freelist at
cap). The tracing module provides:

- **Compile-time flags** to enable specific features (each has a cost)
- **Macros** that expand to noops when disabled (zero runtime cost)
- **Python API** `koskari.lib.get_trace_report()` returning a dict

The header `tracing.h` is not included from `lib.h`. Files that use tracing
macros must include it explicitly (same opt-in pattern as `debug.h`).

## Compile Flags

| Flag | Default | Purpose |
|------|---------|---------|
| `KOS_TRACE_FREELIST` | 0 | Freelist alloc/return hit/miss/overrun |
| `KOS_TRACE_INTERN`   | 0 | Atom intern hit/miss (phase 2) |
| `KOS_TRACE_EVENTS`   | 0 | Event counting (phase 2) |
| `KOS_TRACE_TIMING`   | 0 | Timing data (phase 2) |

Flags are defaulted in `tracing.h` with `#ifndef`. Override at build time:

- **setup.py**: `KOS_TRACE_FREELIST=1 python setup.py build_ext`
- **tests.mk**: `make KOS_TRACE_FREELIST=1`

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
```

## Report Structure

### When all tracing is disabled

```python
{
    "flags": {
        "freelist": False,
        "intern": False,
        "events": False,
        "timing": False
    }
}
```

The `flags` dict always reflects the build-time state of each feature. No
other keys are present when all features are off.

### When freelist tracing is enabled (`KOS_TRACE_FREELIST=1`)

```python
{
    "flags": {"freelist": True, "intern": False, "events": False, "timing": False},
    "freelists": {
        "dyn_node":     {"alloc_hits": N, "alloc_misses": N, "return_hits": N, "overruns": N},
        "continuation": {...},
        "context":      {...},
        "stack_block":  {...},
        "pair":         {...}
    }
}
```

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

Files that include `tracing.h` and use `KOS_TRACE_FL_ALLOC` / `KOS_TRACE_FL_RETURN`:

- **dynchain.c** — `kos_dynnode_alloc`, `kos_dynnode_return_to_freelist`; `kos_continuation_create`, `kos_continuation_free_exclude`, `xfer_plan_release_invoke_clone`
- **eval.c** — `kos_context_new`, `kos_context_free`, and context release on allocation failure
- **stack.c** — `KosStackBlock_New`, `KosStackBlock_NewSized`, `KosStackBlock_ReleaseExclude`
- **pair.c** — `KosPair_New`, `pair_dealloc`

## Future Phases

- **KOS_TRACE_INTERN**: Symbol and keyword intern cache hit/miss
- **KOS_TRACE_EVENTS**: Opcode dispatch counts, Python call counts, etc.
- **KOS_TRACE_TIMING**: Wall-clock or cycle timing for eval runs, per-opcode sampling
