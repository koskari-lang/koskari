# compile/?

**Symbol:** `compile/?` (surface: `?`)

**Syntax:** `(? COND THEN ELSE)`, `(? COND THEN)` with an else arm of
`False`, or `(? COND)`, a boolean test only.

**Behaviour:** The two- and three-argument forms expand to
[`cond`](../specials/compile-cond.md) with an `else:` arm. The unary
form expands to `(truth COND)`.

**Examples:**

```koskari
>>> (? 1 "y" "n")
"y"
>>> (? 0 "y")
False
>>> (? "x")
True
```

**See also:** [`if`](compile-if.md), [`bool`](compile-bool.md).
