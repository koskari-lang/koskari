# compile/macroexpandq

**Syntax:** `(macroexpandq FORM)`, `(macroexpandq FORM NS)`

**Behaviour:** Expands to `(call macroexpand (quote FORM) ...)`. The form
is quoted for you, so it is written as surface syntax. With one argument
the namespace is `(globals)`; with two, the given namespace.

**Examples:**

```koskari
>>> (macroexpandq (when a b))
(cond [a b] [else: None])
```

**See also:** [`macroexpand`](compile-macroexpand.md).
