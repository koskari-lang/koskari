# compile/macroexpand-1

**Syntax:** `(macroexpand-1 FORM)`, `(macroexpand-1 FORM NS)`

**Behaviour:** Expands to a call to the
[`macroexpand-1`](../runtime/macroexpand-1.md) runtime function, which
applies one expansion step to `FORM`. With one argument the namespace
is `(globals)`; with two, the given namespace. `FORM` is evaluated, so
quote it.

**Examples:**

```koskari
>>> (macroexpand-1 '(when a b))
(cond [a b] [else: None])
>>> (macroexpand-1 '(decorate (d1) (define foo [x] x)))
(define foo (decorate [d1] (function foo [x] x)))
```

**See also:** [`macroexpand`](compile-macroexpand.md),
[`macroexpandq-1`](compile-macroexpandq-1.md).
