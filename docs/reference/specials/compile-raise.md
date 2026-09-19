# compile/raise
**Symbol:** `compile/raise`

**Syntax:** `(raise)` or `(raise EXPR)`

**Use-case:** Re-raise the current exception (no arg) or raise a new exception
(EXPR). Used internally by try for re-raise on uncaught.

**Bare form:** the lone symbol `raise` expands to `(raise)`, giving Python's
bare-`raise` re-raise spelling inside a handler. Governed by the
[`compiler/bare-control-words`](../../guide/pragma.md#compiler) pragma of the
module being compiled (`convert` by default, or `warn` / `error`); because the
bare symbol always expands, `raise` can never name a variable.

**Behaviour:** With no arg, emits RAISE 0 (re-raise). With EXPR, compiles EXPR,
emits RAISE 1 (raise the value as exception).

**Examples:**

```koskari
(raise)
(raise (ValueError "invalid"))
```

