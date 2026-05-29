# compile/raise

```{admonition} Reference status
:class: note

Adapted from design/SPECIALS.md. Stability: evolving unless noted otherwise.
```

**Symbol:** `compile/raise`

**Syntax:** `(compile/raise)` or `(compile/raise EXPR)`

**Use-case:** Re-raise the current exception (no arg) or raise a new exception
(EXPR). Used internally by try for re-raise on uncaught.

**Behaviour:** With no arg, emits RAISE 0 (re-raise). With EXPR, compiles EXPR,
emits RAISE 1 (raise the value as exception).

**Examples:**

```scheme
(compile/raise)
(compile/raise (ValueError "invalid"))
```

---

Source: [design/SPECIALS.md](../../design/SPECIALS.md)
