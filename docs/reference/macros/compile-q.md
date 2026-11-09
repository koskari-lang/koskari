# compile/?

**Symbol:** `compile/?` (surface: `?`)

**Syntax:** `(? COND THEN ELSE)`, `(? COND THEN)` with an else arm of
`False`, or `(? COND)`, a boolean test only.

**Behaviour:** The two- and three-argument forms evaluate `COND`, then
`THEN` when it is truthy and the else arm otherwise, as a
[`cond`](../specials/compile-cond.md) with an `else:` arm does. The
unary form is the truthiness of `COND`, as `True` or `False`.

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
