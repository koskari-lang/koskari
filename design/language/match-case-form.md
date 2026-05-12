# Runtime `match` form and `match/case` layout

## Status

Proposal — not implemented. Intended as the user-facing **`match`**
expression (match/case style, distinct from **`switch`** / equality
dispatch). Related matcher engine work is tracked in
[matcher-ng-rollup.md](matcher-ng-rollup.md) (runtime `match/case` phase).

## Summary

- Add a **`match`** form used like **match/case** rather than **switch/case**.
- Add a **reader layout** for **match/case**, parallel in role to the existing
  **switch/case** layout (`layout/switch` in `koskari/reader/layouts`).
- Each **case** carries **match syntax** (the same matcher surface used
  elsewhere in the matcher story).
- **Compilation:** for each case, **parse** and **lower** the case matcher;
  emit a **branching match applier** analogous to the existing
  **`SWITCH_EQ`** chain (see `koskari/compile/specials/conditionals.py`,
  opcode tests under `tests/lib/opcode/test_switch.py`).
- Introduce a dedicated opcode family (name TBD, e.g. **`MATCH_CASE`**) that
  loads the constants needed per arm, including **names** resolved from the
  **local environment** for **externs** where the lowered matcher requires them.
- When an arm **commits**, apply **syntax-rules-style application logic**:
  map matched values into a **new namespace** for the arm body (same conceptual
  move as the application step for syntax rules, at runtime over values rather
  than compile-time template keys alone).

## Dependencies

- **In-scope seq-values with kwargs** (in-scope formals apply) is called out as
  a separate foundation; see
  [seq-values-kwargs-formals-apply.md](seq-values-kwargs-formals-apply.md).

## Related

- [matcher-ng-rollup.md](matcher-ng-rollup.md) — unified matcher, Phase 8
  runtime matching.
- [../archive/switch-eq-opcodes.md](../archive/switch-eq-opcodes.md) —
  `SWITCH_EQ` design background.
- [../LAYOUTS.md](../LAYOUTS.md) — layout conventions.
