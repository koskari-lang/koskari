# compile/macroexpand

**Syntax:** `(macroexpand FORM)`, `(macroexpand FORM NS)`

**Behaviour:** Returns `FORM` expanded until its head is no longer a
macro, as the [`macroexpand`](../runtime/macroexpand.md) runtime
function does. With one argument
the namespace is `(globals)`; with two, the given namespace. `FORM` is
evaluated, so quote it.

**Examples:**

```koskari
>>> (macroexpand '(when ready (print "go")))
(cond [ready (print "go")] [else: None])
```

**See also:** [`macroexpand-1`](compile-macroexpand-1.md) for one step,
[`macroexpandq`](compile-macroexpandq.md) to skip the quote.

**Also:** function [`macroexpand-1`, `macroexpand`](../runtime/functions.md#compilation-and-expansion).
