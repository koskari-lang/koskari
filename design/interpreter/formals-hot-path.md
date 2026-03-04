# Formals Hot-Path Optimization Plan (`koskari/lib/formals.c`)

This note describes a concrete implementation approach for optimizing the
`KosFormals_VectorApply` kwargs path.

## Problem statement

Today, `KosFormals_VectorApply` eagerly materializes an intermediate kwargs dict
from `names + args[]` before applying arguments:

- Build `kwargs = PyDict_NewPresized(k)`
- Insert every keyword pair into it
- Pass dict into `apply_formals_kwargs_phase`

This is called out inline as a TODO and adds allocation/hash churn to the
vectorcall fast path.

## Current hot code points

- `KosFormals_VectorApply` creates the dict unconditionally when `names` exists.
- `apply_formals_kwargs_phase` expects a dict and mutates it (`PyDict_DelItem`) to
  track consumption.
- `**kwargs` handling currently reuses that same dict object.

## Goal

Keep semantics identical while avoiding kwargs dict allocation when:

1. no `**kwargs` formal is present, and
2. we only need direct key lookup/consumption for known formals.

## Proposed architecture

### 1) Keep dict-based path for `KosFormals_Apply` untouched

`KosFormals_Apply` already receives a real kwargs dict from Python-level calls.
No behavior change needed.

### 2) Add a vector-specific kwargs phase

Add a new internal helper dedicated to vectorcall payloads:

```c
static int apply_formals_kwargs_phase_vector(
    PyObject *namespace,
    PyObject *pos,
    PyObject *pos_only,
    PyObject *kwd,
    PyObject *kwd_only,
    PyObject *starstar,
    PyObject *defaults,
    PyObject *const *kw_values,  /* args + l */
    PyObject *kw_names,          /* tuple from vectorcall */
    ssize_t ipos,
    ssize_t ipos_only,
    ssize_t ikwd,
    ssize_t ikwd_only);
```

Behavior:
- Use `kw_names` + `kw_values` directly.
- Track consumed keyword entries using a compact `uint8_t consumed[k]` bitmap.
- Bind required formals by searching keyword names without dict construction.
- Only materialize a dict for leftover kwargs if `starstar` exists.
- If `starstar` is absent and leftovers remain, raise the same TypeError.

### 3) Consumption tracking

Instead of deleting keys from dict:
- `consumed[i] = 1` marks keyword `kw_names[i]` as used.
- Unknown keyword detection = any `consumed[i] == 0` at the end.

Memory strategy:
- stack buffer for small `k` (e.g., up to 64)
- heap fallback for larger `k` via `PyMem_Calloc`

### 4) Name lookup strategy

Phase 1 (simple, safe): linear scan over `kw_names` for each formal key.
- Complexity: `O(formals * k)`
- Usually acceptable because both are small in normal call sites.

Phase 2 (optional, only if profiling says so):
- Build a temporary index dict `{name -> i}` once when `k` exceeds threshold.
- Still avoids constructing full kwargs values dict.

### 5) `**kwargs` behavior

When `starstar` formal exists:
- Build a new dict sized to number of unconsumed keywords only.
- Populate with leftover key/value pairs.
- Store into namespace.

When `starstar` formal does not exist:
- If leftovers exist, raise `TypeError("unexpected keyword arguments %R", ... )`.
- For parity of error formatting, create a tiny temporary dict only in error case.

## Correctness constraints to preserve

- Required positional/keyword/keyword-only errors must match existing behavior.
- Default fallback semantics must be unchanged.
- Keyword consumption order should remain equivalent.
- Duplicate keyword handling should remain consistent with incoming vectorcall
  guarantees (or explicitly validated if needed).

## Rollout strategy

1. Introduce `apply_formals_kwargs_phase_vector` alongside existing helper.
2. Wire only `KosFormals_VectorApply` to new helper.
3. Keep old dict-based path as fallback behind a local compile-time toggle during
   initial verification (optional).
4. Remove fallback after perf + correctness pass.

## Test plan

Focus tests on argument-shape matrices in `tests/lib/test_formals.py` and opcode
call pathways that exercise vectorcall:

- no kwargs
- kwargs fully matching known names
- missing required kw/kw-only
- unknown kwargs with and without `**`
- defaults + mixed positional/keywords

Add regression tests for error message text where stable.

## Perf validation

Use `pyperf` microbenchmarks (or existing harness) with scenarios:

- many small function calls with keyword args (`k=1..4`)
- mixed positional + keyword calls
- calls with/without `**kwargs` formal

Expected result: reduced allocations and lower wall time in keyword-heavy
vectorcall paths.

## Suggested first patch scope (small, reviewable)

- Add new vector kwargs helper
- Keep linear scans only
- Add/adjust tests for behavior parity
- No broader refactors

This delivers the main allocation win with minimal semantic risk.
