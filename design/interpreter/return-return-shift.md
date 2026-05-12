# `RETURN` as frame boundary only; `return-shift` for delim continuations

## Status

Proposal — not implemented.

## Summary

- Narrow **`RETURN`** so it is **only** a **frame-boundary** operation (leave
  the frame / ordinary return path without carrying delimited-continuation
  semantics on the same opcode).
- **Remove** delimited-continuation behavior from **`RETURN`** (whatever is
  bundled there today moves off that path).
- Add a separate **`return-shift`** (exact surface name TBD) used **explicitly**
  for the **delimited continuation rebound** case.

## Related

- [EVALUATOR.md](../EVALUATOR.md) — tail calls, returns, phase flow.
- [callcc.md](callcc.md) — continuation invoke and related notes.
- [extent-wind-unwind.md](extent-wind-unwind.md) — boundary behavior.
