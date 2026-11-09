# compile/macroexpand-1

**Syntax:** `(macroexpand-1 FORM)`, `(macroexpand-1 FORM NS)`

**Behaviour:** Returns `FORM` after one expansion step, as the
[`macroexpand-1`](../runtime/macroexpand-1.md) runtime function does.
With one argument the namespace
is `(globals)`; with two, the given namespace. `FORM` is evaluated, so
quote it.

**Examples:**

```koskari
>>> (macroexpand-1 '(when a b))
(cond [a b] [else: None])
>>> (macroexpand-1 '(decorate (d1) (def foo [x] x)))
(var foo (decorate [d1] (function foo [x] x)))
```

**See also:** [`macroexpand`](compile-macroexpand.md),
[`macroexpandq-1`](compile-macroexpandq-1.md).
