# Example harness: dispatch call timing

## Status

Proposal — not implemented. Example / benchmark code under examples (exact
tree location TBD when implemented).

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

- [../api/tracing.md](../api/tracing.md) — existing tracing and timing hooks.
- [../interpreter/gil-cooperation.md](../interpreter/gil-cooperation.md) —
  cooperation and timing noise context.
