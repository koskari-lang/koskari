# compile/assert

**Symbol:** `compile/assert` (surface: `assert`)

**Syntax:**

- `(assert EXPR)` — raise `AssertionError` when `EXPR` is falsey.
- `(assert EXPR MESSAGE)` — the same, with `MESSAGE` as the exception argument.

**Behaviour:** Expands to a falsey-check that raises **`AssertionError`** —
effectively `(unless EXPR (raise (AssertionError MESSAGE?)))`. `MESSAGE` is an
ordinary expression and is evaluated only when the assertion fails.

The expansion is gated by the **`compiler/enable-asserts`** pragma setting
(default on). When disabled, an `assert` form expands to nothing at all:
neither the condition nor the message is compiled or evaluated, mirroring
Python's `-O` behavior. The gate is consulted as each `assert` form is
compiled, so a pragma change is forward-only — it affects the asserts that
appear after it, and an assert already compiled away stays gone even if
asserts are re-enabled later in the load.

```koskari
(assert (< x limit))
(assert (< x limit) f"x too large: {x}")

(pragma [compiler/enable-asserts False])
(assert (expensive-invariant-check))   ; compiles away entirely
```

See [Pragmas and feature settings](../../guide/pragma.md) for how pragma
forms work.
