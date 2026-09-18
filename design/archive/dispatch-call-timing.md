# Example harness: dispatch call timing

## Status

Archived 2026-09-18, superseded: the churn harness (`examples/churn`,
`tox -qe churn`) times Koskari against CPython over eighteen kernels
with cross-checked answers, including method, coroutine, async-tree
and yield-from calls, and writes a metrics payload. A cell this
proposal names that the harness lacks is a kernel there, not a second
harness.

## Summary

- Add **dispatch timers** to **examples** so measurements show elapsed time
  from **calling a form** → **entering the callee** → **returning from the
  callee**.
- Implement the same trivial callee logic as several **function types**:
  **Python**, **async Python**, **Koskari**, **demand Koskari**.
- Invoke each callee type from **Python** and from **Koskari** (full cross
  product of caller × callee where applicable).
- Per call, record three timestamps:
  1. Immediately **before** the call.
  2. Immediately **inside** the callee at entry (this timestamp is returned /
     surfaced as the “inside” sample).
  3. Immediately **after** the call returns.
- Timestamp reads add overhead; **pre-measure** the cost of fetching a
  timestamp and **subtract** that baseline from the stacked intervals.
- Run each variation on the order of **~1000** iterations; **aggregate
  averages** to compare which **caller/callee** stacks are cheapest vs most
  expensive.

## Related

- [../api/tracing.md](../api/tracing.md) -- existing tracing and timing hooks.
- [gil-cooperation.md](gil-cooperation.md) --
  cooperation and timing noise context.
