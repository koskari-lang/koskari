# compile/if

**Syntax:** `(if COND THEN ELSE)`, `(if COND then: THEN else: ELSE)`, or
`(if COND THEN)`, whose else arm is `None`.

**Behaviour:** Expands to `(cond [COND THEN] [else: ELSE])`. The layout
form `if ...:` with `elif` and `else` peers reads directly to `cond`
through [`layout/if`](../layouts/layout-if.md), so a multi-line `if`
never passes through this macro.

**Examples:**

```koskari
(if (< n 0) "negative" "non-negative")
(if ready then: (go) else: (wait))
(if ready (go))                  ;; None when not ready
```

```koskari
>>> (macroexpand-1 '(if a then: b else: c))
(cond [a b] [else: c])
```

**See also:** [`cond`](../specials/compile-cond.md),
[`when`](compile-when.md), [`?`](compile-q.md).
