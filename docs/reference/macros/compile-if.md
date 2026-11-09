# compile/if

**Syntax:** `(if COND THEN ELSE)`, `(if COND then: THEN else: ELSE)`, or
`(if COND THEN)`, whose else arm is `None`.

**Behaviour:** Evaluates `COND`; when it is truthy the value is `THEN`,
otherwise `ELSE`, and only the chosen arm is evaluated, as in a two-arm
[`cond`](../specials/compile-cond.md). The layout
form `if ...:` with `elif` and `else` peers reads directly to `cond`
through [`layout/if`](../layouts/layout-if.md), so a multi-line `if`
never passes through this macro.

**Examples:**

```koskari
(if (< n 0) "negative" "non-negative")
(if ready then: (go) else: (wait))
(if ready (go))                  ;; None when not ready
```

**See also:** [`cond`](../specials/compile-cond.md),
[`when`](compile-when.md), [`?`](compile-q.md).

**Also:** layout handler [`if`](../layouts/layout-if.md).
