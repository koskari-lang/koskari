# compile/raise
**Symbol:** `compile/raise`

**Syntax:** `(raise)` or `(raise EXPR)`

**Use-case:** Re-raise the current exception (no arg) or raise a new exception
(EXPR). Used internally by try for re-raise on uncaught.

**Behaviour:** With no arg, emits RAISE 0 (re-raise). With EXPR, compiles EXPR,
emits RAISE 1 (raise the value as exception).

**Examples:**

```scheme
(raise)
(raise (ValueError "invalid"))
```

